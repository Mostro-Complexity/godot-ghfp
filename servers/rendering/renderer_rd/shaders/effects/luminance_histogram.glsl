#[compute]

#version 450

#VERSION_DEFINES

#define HISTOGRAM_BIN_COUNT 256

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	int sample_stride;
	int bin_count;
	float min_ev;
	float max_ev;
	float low_percentile;
	float high_percentile;
	float center_weight;
	float exposure_adjust;
	float min_luminance;
	float max_luminance;
}
params;

#ifdef BUILD_HISTOGRAM

layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(set = 1, binding = 0, std430) restrict buffer HistogramBuffer {
	uint bins[];
}
histogram;

shared uint local_histogram[HISTOGRAM_BIN_COUNT];

void main() {
	uint lane = gl_LocalInvocationID.y * 16u + gl_LocalInvocationID.x;
	local_histogram[lane] = 0u;
	groupMemoryBarrier();
	barrier();

	ivec2 sample_position = ivec2(gl_GlobalInvocationID.xy) * params.sample_stride + ivec2(params.sample_stride / 2);
	if (all(lessThan(sample_position, params.source_size))) {
		vec3 color = max(texelFetch(source_texture, sample_position, 0).rgb, vec3(0.0));
		float luminance = max(dot(color, vec3(0.2126, 0.7152, 0.0722)), exp2(params.min_ev));
		float normalized_ev = clamp((log2(luminance) - params.min_ev) / max(params.max_ev - params.min_ev, 0.001), 0.0, 0.999999);
		uint bin = min(uint(normalized_ev * float(params.bin_count)), uint(params.bin_count - 1));

		vec2 uv = (vec2(sample_position) + vec2(0.5)) / vec2(params.source_size);
		float radial = clamp(length((uv - vec2(0.5)) * 1.41421356237), 0.0, 1.0);
		float center_response = 1.0 - smoothstep(0.0, 1.0, radial);
		uint weight = 1u + uint(round(7.0 * params.center_weight * center_response));
		atomicAdd(local_histogram[bin], weight);
	}

	groupMemoryBarrier();
	barrier();
	if (local_histogram[lane] != 0u) {
		atomicAdd(histogram.bins[lane], local_histogram[lane]);
	}
}

#else

layout(set = 0, binding = 0, std430) restrict readonly buffer HistogramBuffer {
	uint bins[];
}
histogram;
layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D dest_luminance;
layout(set = 2, binding = 0) uniform sampler2D prev_luminance;

void main() {
	if (gl_GlobalInvocationID.x != 0u || gl_GlobalInvocationID.y != 0u) {
		return;
	}

	uint total = 0u;
	for (int i = 0; i < params.bin_count; i++) {
		total += histogram.bins[i];
	}

	float previous = texelFetch(prev_luminance, ivec2(0), 0).r;
	float target = previous;
	if (total > 0u) {
		uint low_count = uint(floor(float(total) * params.low_percentile));
		uint high_count = max(low_count + 1u, uint(ceil(float(total) * params.high_percentile)));
		high_count = min(high_count, total);
		uint cumulative = 0u;
		uint accepted_total = 0u;
		float accepted_log_luminance = 0.0;
		for (int i = 0; i < params.bin_count; i++) {
			uint bin_begin = cumulative;
			uint bin_end = cumulative + histogram.bins[i];
			uint accepted_begin = max(bin_begin, low_count);
			uint accepted_end = min(bin_end, high_count);
			if (accepted_end > accepted_begin) {
				uint accepted = accepted_end - accepted_begin;
				float bin_ev = mix(params.min_ev, params.max_ev, (float(i) + 0.5) / float(params.bin_count));
				accepted_log_luminance += bin_ev * float(accepted);
				accepted_total += accepted;
			}
			cumulative = bin_end;
		}
		if (accepted_total > 0u) {
			target = exp2(accepted_log_luminance / float(accepted_total));
		}
	}

	target = clamp(target, params.min_luminance, params.max_luminance);
	float adaptation = clamp(params.exposure_adjust, 0.0, 1.0);
	float adapted = mix(previous, target, adaptation);
	imageStore(dest_luminance, ivec2(0), vec4(adapted));
}

#endif
