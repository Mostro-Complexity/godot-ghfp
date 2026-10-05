/**************************************************************************/
/*  render_scene_buffers_rd.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "render_scene_buffers_rd.h"
#include "render_scene_buffers_rd.compat.inc"

#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/environment/fog.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/rendering_device_binds.h"
#include "servers/rendering/rendering_server.h" // IWYU pragma: keep // Needed to bind RSE enums.
#include "servers/rendering/rendering_server_enums.h"

#include <cmath>
#include <cstring>

RenderSceneBuffersRD::RenderSceneBuffersRD() {
}

RenderSceneBuffersRD::~RenderSceneBuffersRD() {
	cleanup();

	data_buffers.clear();

	RendererRD::MaterialStorage::get_singleton()->samplers_rd_free(samplers);
}

void RenderSceneBuffersRD::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cloud_medium", "texture", "far_distance"), &RenderSceneBuffersRD::set_cloud_medium);
	ClassDB::bind_method(D_METHOD("set_cloud_medium_depth", "texture"), &RenderSceneBuffersRD::set_cloud_medium_depth);
	ClassDB::bind_method(D_METHOD("set_cloud_medium_moments", "texture"), &RenderSceneBuffersRD::set_cloud_medium_moments);
	ClassDB::bind_method(D_METHOD("set_cloud_medium_compact_v2", "eye_descriptors"), &RenderSceneBuffersRD::set_cloud_medium_compact_v2);
#ifdef DEV_ENABLED
	ClassDB::bind_method(D_METHOD("debug_validate_cloud_medium_compact_v2_stereo", "eye_descriptors"), &RenderSceneBuffersRD::debug_validate_cloud_medium_compact_v2_stereo);
#endif
	ClassDB::bind_method(D_METHOD("set_directional_contact_shadow", "texture", "direction_world"), &RenderSceneBuffersRD::set_directional_contact_shadow);
	ClassDB::bind_method(D_METHOD("get_local_fog_texture"), &RenderSceneBuffersRD::get_local_fog_texture);
	ClassDB::bind_method(D_METHOD("get_local_fog_range"), &RenderSceneBuffersRD::get_local_fog_range);
	ClassDB::bind_method(D_METHOD("request_local_fog_medium_capture", "enabled"), &RenderSceneBuffersRD::request_local_fog_medium_capture);
	ClassDB::bind_method(D_METHOD("get_local_fog_medium_capture"), &RenderSceneBuffersRD::get_local_fog_medium_capture);
	ClassDB::bind_method(D_METHOD("has_texture", "context", "name"), &RenderSceneBuffersRD::has_texture);
	ClassDB::bind_method(D_METHOD("create_texture", "context", "name", "data_format", "usage_bits", "texture_samples", "size", "layers", "mipmaps", "unique", "discardable"), &RenderSceneBuffersRD::create_texture);
	ClassDB::bind_method(D_METHOD("create_texture_from_format", "context", "name", "format", "view", "unique"), &RenderSceneBuffersRD::_create_texture_from_format);
	ClassDB::bind_method(D_METHOD("create_texture_view", "context", "name", "view_name", "view"), &RenderSceneBuffersRD::_create_texture_view);
	ClassDB::bind_method(D_METHOD("get_texture", "context", "name"), &RenderSceneBuffersRD::get_texture);
	ClassDB::bind_method(D_METHOD("get_texture_format", "context", "name"), &RenderSceneBuffersRD::_get_texture_format);
	ClassDB::bind_method(D_METHOD("get_texture_slice", "context", "name", "layer", "mipmap", "layers", "mipmaps"), &RenderSceneBuffersRD::get_texture_slice);
	ClassDB::bind_method(D_METHOD("get_texture_slice_view", "context", "name", "layer", "mipmap", "layers", "mipmaps", "view"), &RenderSceneBuffersRD::_get_texture_slice_view);
	ClassDB::bind_method(D_METHOD("get_texture_slice_size", "context", "name", "mipmap"), &RenderSceneBuffersRD::get_texture_slice_size);
	ClassDB::bind_method(D_METHOD("clear_context", "context"), &RenderSceneBuffersRD::clear_context);

	// Access to some core buffers so users don't need to know their names.
	ClassDB::bind_method(D_METHOD("get_color_texture", "msaa"), &RenderSceneBuffersRD::_get_color_texture, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_color_layer", "layer", "msaa"), &RenderSceneBuffersRD::_get_color_layer, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_depth_texture", "msaa"), &RenderSceneBuffersRD::_get_depth_texture, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_depth_layer", "layer", "msaa"), &RenderSceneBuffersRD::_get_depth_layer, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_velocity_texture", "msaa"), &RenderSceneBuffersRD::_get_velocity_texture, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_velocity_layer", "layer", "msaa"), &RenderSceneBuffersRD::_get_velocity_layer, DEFVAL(false));

	// Expose a few properties we're likely to use externally
	ClassDB::bind_method(D_METHOD("get_render_target"), &RenderSceneBuffersRD::get_render_target);
	ClassDB::bind_method(D_METHOD("get_view_count"), &RenderSceneBuffersRD::get_view_count);
	ClassDB::bind_method(D_METHOD("get_internal_size"), &RenderSceneBuffersRD::get_internal_size);
	ClassDB::bind_method(D_METHOD("get_target_size"), &RenderSceneBuffersRD::get_target_size);
	ClassDB::bind_method(D_METHOD("get_scaling_3d_mode"), &RenderSceneBuffersRD::get_scaling_3d_mode);
	ClassDB::bind_method(D_METHOD("get_fsr_sharpness"), &RenderSceneBuffersRD::get_fsr_sharpness);
	ClassDB::bind_method(D_METHOD("get_msaa_3d"), &RenderSceneBuffersRD::get_msaa_3d);
	ClassDB::bind_method(D_METHOD("get_texture_samples"), &RenderSceneBuffersRD::get_texture_samples);
	ClassDB::bind_method(D_METHOD("get_screen_space_aa"), &RenderSceneBuffersRD::get_screen_space_aa);
	ClassDB::bind_method(D_METHOD("get_use_taa"), &RenderSceneBuffersRD::get_use_taa);
	ClassDB::bind_method(D_METHOD("get_use_debanding"), &RenderSceneBuffersRD::get_use_debanding);
}

#ifdef DEV_ENABLED
bool RenderSceneBuffersRD::debug_validate_cloud_medium_compact_v2_stereo(TypedArray<Dictionary> p_eye_descriptors) {
	ERR_NOT_ON_RENDER_THREAD_V(false);
	Ref<RenderSceneBuffersRD> probe;
	probe.instantiate();
	probe->view_count = 2;
	probe->internal_size = internal_size;
	return probe->set_cloud_medium_compact_v2(p_eye_descriptors);
}
#endif

bool RenderSceneBuffersRD::set_cloud_medium_compact_v2(TypedArray<Dictionary> p_eye_descriptors) {
	// ClassDB calls this during compositor rendering; descriptors and borrowed RIDs
	// are only mutated on the render thread and remain alive through the last consumer.
	ERR_NOT_ON_RENDER_THREAD_V(false);
	auto reject = [this](const String &p_reason) {
		set_cloud_medium(RID(), 0.0f);
		ERR_PRINT(p_reason);
		return false;
	};
	if (p_eye_descriptors.size() != view_count || p_eye_descriptors.is_empty()) {
		return reject("Compact cloud-medium v2 requires one complete descriptor per RenderSceneBuffersRD view.");
	}

	RD *rd = RD::get_singleton();
	if (rd == nullptr) {
		return reject("Compact cloud-medium v2 requires an active RenderingDevice.");
	}
	Vector<CloudMediumCompactDescriptor> descriptors;
	descriptors.resize(view_count);
	int64_t common_allocation_generation = -1;
	int64_t common_frame_generation = -1;
	int64_t common_weather_generation = -1;
	int64_t common_lighting_generation = -1;
	int32_t common_candidate_layers = -1;

	auto get_integer = [](const Dictionary &p_dictionary, const char *p_key, int64_t &r_value) {
		Variant value = p_dictionary.get(StringName(p_key), Variant());
		if (value.get_type() != Variant::INT) {
			return false;
		}
		r_value = value;
		return true;
	};
	auto get_rid = [](const Dictionary &p_dictionary, const char *p_key, RID &r_value) {
		Variant value = p_dictionary.get(StringName(p_key), Variant());
		if (value.get_type() != Variant::RID) {
			return false;
		}
		r_value = value;
		return true;
	};
	auto get_size = [](const Dictionary &p_dictionary, const char *p_key, Vector2i &r_value) {
		Variant value = p_dictionary.get(StringName(p_key), Variant());
		if (value.get_type() != Variant::VECTOR2I) {
			return false;
		}
		r_value = value;
		return true;
	};
	auto get_extent = [](const Dictionary &p_dictionary, const char *p_key, Vector3i &r_value) {
		Variant value = p_dictionary.get(StringName(p_key), Variant());
		if (value.get_type() != Variant::VECTOR3I) {
			return false;
		}
		r_value = value;
		return true;
	};
	auto get_string = [](const Dictionary &p_dictionary, const char *p_key, String &r_value) {
		Variant value = p_dictionary.get(StringName(p_key), Variant());
		if (value.get_type() != Variant::STRING) {
			return false;
		}
		r_value = value;
		return true;
	};
	auto fits_i32 = [](int64_t p_value) { return p_value >= INT32_MIN && p_value <= INT32_MAX; };
	auto texture_matches = [rd](RID p_rid, RD::DataFormat p_format, int32_t p_width, int32_t p_height, uint32_t p_depth) {
		if (!rd->texture_is_valid(p_rid)) {
			return false;
		}
		const RD::TextureFormat format = rd->texture_get_format(p_rid);
		const uint32_t required_usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		return format.format == p_format && format.texture_type == RD::TEXTURE_TYPE_3D && format.width == (uint32_t)p_width && format.height == (uint32_t)p_height &&
			format.depth == p_depth && format.array_layers == 1 && (format.usage_bits & required_usage) == required_usage;
	};
	auto multiply_size = [](uint64_t p_a, uint64_t p_b, uint64_t &r_product) {
		if (p_b != 0 && p_a > UINT64_MAX / p_b) {
			return false;
		}
		r_product = p_a * p_b;
		return true;
	};

	for (uint32_t eye = 0; eye < view_count; eye++) {
		const Dictionary dictionary = p_eye_descriptors[eye];
		CloudMediumCompactDescriptor descriptor;
		int64_t format_version = -1;
		int64_t eye_index = -1;
		int64_t eye_count = -1;
		int64_t candidate_layers = -1;
		int64_t slot_capacity = -1;
		int64_t pool_capacity = -1;
		if (!get_integer(dictionary, "format_version", format_version) || !fits_i32(format_version) ||
				!get_integer(dictionary, "eye_index", eye_index) || !fits_i32(eye_index) ||
				!get_integer(dictionary, "eye_count", eye_count) || !fits_i32(eye_count) ||
				!get_integer(dictionary, "candidate_layers", candidate_layers) || !fits_i32(candidate_layers) ||
				!get_integer(dictionary, "slot_capacity", slot_capacity) || !fits_i32(slot_capacity) ||
				!get_integer(dictionary, "pool_capacity", pool_capacity) || !fits_i32(pool_capacity) ||
				!get_size(dictionary, "trace_size", descriptor.trace_size) || !get_size(dictionary, "output_size", descriptor.output_size) ||
				!get_integer(dictionary, "allocation_generation", descriptor.allocation_generation) ||
				!get_integer(dictionary, "frame_generation", descriptor.frame_generation) ||
				!get_integer(dictionary, "weather_generation", descriptor.weather_generation) ||
				!get_integer(dictionary, "lighting_generation", descriptor.lighting_generation) ||
				!get_rid(dictionary, "lt_texture", descriptor.lt_texture) ||
				!get_rid(dictionary, "depth_texture", descriptor.depth_texture) ||
				!get_rid(dictionary, "ray_map", descriptor.ray_map) ||
				!get_rid(dictionary, "exception_lt_pool", descriptor.exception_lt_pool) ||
				!get_rid(dictionary, "exception_depth_pool", descriptor.exception_depth_pool)) {
			return reject("Compact cloud-medium v2 eye descriptor is missing a field or has a mismatched Variant type.");
		}
		if (dictionary.has("direct_resources")) {
			return reject("Compact cloud-medium v2 rejects the obsolete opaque direct_resources uniform-set RID.");
		}
		descriptor.format_version = (int32_t)format_version;
		descriptor.eye_index = (int32_t)eye_index;
		descriptor.eye_count = (int32_t)eye_count;
		descriptor.candidate_layers = (int32_t)candidate_layers;
		descriptor.slot_capacity = (int32_t)slot_capacity;
		descriptor.pool_capacity = (int32_t)pool_capacity;

		if (descriptor.format_version != 2 || descriptor.eye_index != eye || descriptor.eye_count != view_count ||
				descriptor.trace_size.x <= 0 || descriptor.trace_size.y <= 0 || descriptor.output_size != internal_size ||
				descriptor.slot_capacity != 24 || descriptor.pool_capacity > 0x3fffffff ||
				descriptor.allocation_generation < 0 || descriptor.frame_generation < 0 || descriptor.weather_generation < 0 || descriptor.lighting_generation < 0) {
			return reject("Compact cloud-medium v2 eye descriptor violates version, dimensions, eye, capacity, or generation constraints.");
		}
		if (descriptor.candidate_layers != 96 || descriptor.pool_capacity < 1) {
			return reject("Compact cloud-medium v2 pinned producer requires candidate_layers=96 and pool_capacity>=1.");
		}
		Variant direct_value = dictionary.get("direct_inputs", Variant());
		if (direct_value.get_type() != Variant::DICTIONARY) {
			return reject("Compact cloud-medium v2 requires typed direct_inputs for every eye.");
		}
		const Dictionary direct_dictionary = direct_value;
		CloudDirectInputs &direct = descriptor.direct_inputs;
		int64_t direct_version = 0;
		int64_t direct_eye_index = -1;
		int64_t direct_eye_count = 0;
		if (!get_integer(direct_dictionary, "version", direct_version) || !fits_i32(direct_version) ||
				!get_string(direct_dictionary, "source_hash", direct.source_hash) ||
				!get_string(direct_dictionary, "integration_hash", direct.integration_hash) ||
				!get_integer(direct_dictionary, "eye_index", direct_eye_index) || !fits_i32(direct_eye_index) ||
				!get_integer(direct_dictionary, "eye_count", direct_eye_count) || !fits_i32(direct_eye_count) ||
				!get_integer(direct_dictionary, "allocation_generation", direct.allocation_generation) ||
				!get_integer(direct_dictionary, "frame_generation", direct.frame_generation) ||
				!get_integer(direct_dictionary, "weather_generation", direct.weather_generation) ||
				!get_integer(direct_dictionary, "lighting_generation", direct.lighting_generation) ||
				!get_rid(direct_dictionary, "settings_buffer", direct.settings_buffer) ||
				!get_rid(direct_dictionary, "weather_pages_buffer", direct.weather_pages_buffer)) {
			return reject("CloudDirectInputs is missing version, hash, eye/generation identity or typed SSBO RIDs.");
		}
		direct.version = (int32_t)direct_version;
		direct.eye_index = (int32_t)direct_eye_index;
		direct.eye_count = (int32_t)direct_eye_count;
		if (direct.version != CloudDirectInputs::ABI_VERSION || direct.source_hash.is_empty() || direct.integration_hash.is_empty() ||
				direct.eye_index != descriptor.eye_index || direct.eye_count != descriptor.eye_count ||
				direct.allocation_generation != descriptor.allocation_generation || direct.frame_generation != descriptor.frame_generation ||
				direct.weather_generation != descriptor.weather_generation || direct.lighting_generation != descriptor.lighting_generation) {
			return reject("CloudDirectInputs version, source hash, eye or generation disagrees with its v2 descriptor.");
		}
		if (!rd->storage_buffer_is_valid(direct.settings_buffer) || !rd->storage_buffer_is_valid(direct.weather_pages_buffer) ||
				rd->storage_buffer_get_size(direct.settings_buffer) != CloudDirectInputs::SETTINGS_BYTES ||
				rd->storage_buffer_get_size(direct.weather_pages_buffer) != CloudDirectInputs::WEATHER_PAGES_BYTES) {
			return reject("CloudDirectInputs requires actual 496-byte settings and 1024-byte WeatherPages storage buffers.");
		}
		// Audit-only synchronous readback: verify the actual std430 float payload,
		// not just its RID and byte count. This setter is not a production path.
		const Vector<uint8_t> settings_bytes = rd->buffer_get_data(direct.settings_buffer);
		if (settings_bytes.size() != CloudDirectInputs::SETTINGS_BYTES) {
			return reject("CloudDirectInputs settings readback did not return exactly 496 bytes.");
		}
		float settings[CloudDirectInputs::SETTINGS_BYTES / sizeof(float)];
		memcpy(settings, settings_bytes.ptr(), sizeof(settings));
		for (float value : settings) {
			if (!std::isfinite(value)) {
				return reject("CloudDirectInputs settings payload contains a non-finite std430 float.");
			}
		}
		const uint64_t eye_layer_offset = (uint64_t)descriptor.eye_index * (uint64_t)descriptor.candidate_layers;
		if (settings[52] != descriptor.trace_size.x || settings[53] != descriptor.trace_size.y ||
				settings[54] != descriptor.candidate_layers || settings[55] != (float)eye_layer_offset ||
				(uint64_t)settings[55] != eye_layer_offset ||
				settings[35] <= 0.0f || settings[56] <= 0.0f || settings[59] <= 0.0f) {
			return reject("CloudDirectInputs actual settings payload disagrees with trace dimensions, eye offset, physical radius or full-ray schedule.");
		}
		// Only finite values and a few descriptor-linked header fields are
		// checked here; physical observer/schedule semantics remain unproven.
		direct.settings_header_fields_checked = true;
		struct DirectTextureField {
			const char *name;
			RD::TextureType type;
			CloudDirectTextureInput *input;
		};
		const DirectTextureField direct_textures[] = {
			{ "climate_moisture", RD::TEXTURE_TYPE_2D, &direct.climate_moisture },
			{ "base_noise", RD::TEXTURE_TYPE_3D, &direct.base_noise },
			{ "detail_noise", RD::TEXTURE_TYPE_3D, &direct.detail_noise },
			{ "shape_noise", RD::TEXTURE_TYPE_3D, &direct.shape_noise },
			{ "local_fog", RD::TEXTURE_TYPE_3D, &direct.local_fog },
			{ "scene_depth", RD::TEXTURE_TYPE_2D, &direct.scene_depth },
			{ "weather", RD::TEXTURE_TYPE_3D, &direct.weather },
			{ "terrain_sun_global", RD::TEXTURE_TYPE_3D, &direct.terrain_sun_global },
			{ "terrain_sun_local", RD::TEXTURE_TYPE_3D, &direct.terrain_sun_local },
			{ "solar_current", RD::TEXTURE_TYPE_3D, &direct.solar_current },
			{ "solar_previous", RD::TEXTURE_TYPE_3D, &direct.solar_previous },
			{ "atmosphere_optical_depth", RD::TEXTURE_TYPE_2D, &direct.atmosphere_optical_depth },
		};
		for (const DirectTextureField &field : direct_textures) {
			const Variant field_value = direct_dictionary.get(StringName(field.name), Variant());
			if (field_value.get_type() != Variant::DICTIONARY) {
				return reject(vformat("CloudDirectInputs texture '%s' is missing typed allocation metadata.", field.name));
			}
			const Dictionary field_dictionary = field_value;
			int64_t declared_format = -1;
			int64_t declared_mipmaps = 0;
			if (!get_rid(field_dictionary, "rid", field.input->rid) || !get_integer(field_dictionary, "format", declared_format) ||
					!get_extent(field_dictionary, "extent", field.input->extent) || !get_integer(field_dictionary, "mipmaps", declared_mipmaps) ||
					declared_format < 0 || declared_format >= RD::DATA_FORMAT_MAX || declared_mipmaps < 1 || !fits_i32(declared_mipmaps) ||
					field.input->extent.x <= 0 || field.input->extent.y <= 0 || field.input->extent.z <= 0 ||
					(field.type == RD::TEXTURE_TYPE_2D && field.input->extent.z != 1) || !rd->texture_is_valid(field.input->rid)) {
				return reject(vformat("CloudDirectInputs texture '%s' has invalid RID, format, extent or mip metadata.", field.name));
			}
			const RD::TextureStorageOverlap lt_overlap = rd->texture_storage_overlap(field.input->rid, descriptor.lt_texture);
			const RD::TextureStorageOverlap depth_overlap = rd->texture_storage_overlap(field.input->rid, descriptor.depth_texture);
			if (lt_overlap != RD::TEXTURE_STORAGE_OVERLAP_DISJOINT || depth_overlap != RD::TEXTURE_STORAGE_OVERLAP_DISJOINT) {
				const bool proven_overlap = lt_overlap == RD::TEXTURE_STORAGE_OVERLAP_OVERLAP || depth_overlap == RD::TEXTURE_STORAGE_OVERLAP_OVERLAP;
				return reject(vformat("CloudDirectInputs sampled texture '%s' %s a writable current L/T or depth output storage view.", field.name,
						proven_overlap ? "aliases" : "has unknown separation from"));
			}
			field.input->declared_format = (RD::DataFormat)declared_format;
			field.input->mipmaps = (int32_t)declared_mipmaps;
			const RD::TextureFormat actual = rd->texture_get_format(field.input->rid);
			if (actual.format != field.input->declared_format || actual.texture_type != field.type ||
					actual.width != (uint32_t)field.input->extent.x || actual.height != (uint32_t)field.input->extent.y ||
					actual.depth != (uint32_t)field.input->extent.z || actual.mipmaps != (uint32_t)field.input->mipmaps ||
					actual.array_layers != 1 || actual.samples != RD::TEXTURE_SAMPLES_1 ||
					(actual.usage_bits & RD::TEXTURE_USAGE_SAMPLING_BIT) == 0) {
				return reject(vformat("CloudDirectInputs texture '%s' disagrees with the actual RD format, extent, type, mips, samples or sampling usage.", field.name));
			}
			if ((field.input == &direct.weather && actual.format != RD::DATA_FORMAT_R16G16_SFLOAT) ||
					((field.input == &direct.solar_current || field.input == &direct.solar_previous) && actual.format != RD::DATA_FORMAT_R16_SFLOAT)) {
				return reject(vformat("CloudDirectInputs texture '%s' violates the pinned producer's known volume format.", field.name));
			}
		}
		if (direct.scene_depth.extent.x != descriptor.output_size.x || direct.scene_depth.extent.y != descriptor.output_size.y) {
			return reject("CloudDirectInputs raw scene depth extent disagrees with the render output size.");
		}
		for (uint32_t previous_eye = 0; previous_eye < eye; previous_eye++) {
			const CloudDirectInputs &previous = descriptors[previous_eye].direct_inputs;
			const RD::TextureStorageOverlap depth_overlap = rd->texture_storage_overlap(direct.scene_depth.rid, previous.scene_depth.rid);
			if (depth_overlap != RD::TEXTURE_STORAGE_OVERLAP_DISJOINT) {
				return reject(depth_overlap == RD::TEXTURE_STORAGE_OVERLAP_OVERLAP ?
						"CloudDirectInputs scene-depth storage views overlap across eyes." :
						"CloudDirectInputs scene-depth storage separation across eyes is unknown.");
			}
			if (direct.settings_buffer == previous.settings_buffer || direct.source_hash != previous.source_hash ||
					direct.integration_hash != previous.integration_hash) {
				return reject("CloudDirectInputs must use a distinct settings SSBO per eye and one matching source/integration hash.");
			}
		}
		direct.allocations_validated = true;
		// RD does not expose sampler bindings or prove that metadata hashes match
		// the native integration source. Per-eye scene-depth generation identity
		// and an owner-backed GPU-use lease are also absent.
		// Keep receiver_compatible false even after every allocation check passes.
		const uint64_t view_depth = (uint64_t)descriptor.slot_capacity * descriptor.eye_count;
		uint64_t ray_count = 0;
		uint64_t ray_map_bytes = 0;
		uint64_t pool_nodes = 0;
		uint64_t pool_lt_bytes = 0;
		uint64_t pool_depth_bytes = 0;
		const bool buffer_sizes_representable = multiply_size((uint64_t)descriptor.trace_size.x, (uint64_t)descriptor.trace_size.y, ray_count) &&
			multiply_size(ray_count, descriptor.eye_count, ray_count) && multiply_size(ray_count, sizeof(uint32_t), ray_map_bytes) &&
			multiply_size((uint64_t)descriptor.pool_capacity, descriptor.candidate_layers, pool_nodes) &&
			multiply_size(pool_nodes, 2 * sizeof(uint32_t), pool_lt_bytes) && multiply_size(pool_nodes, sizeof(float), pool_depth_bytes) &&
			view_depth <= UINT32_MAX && ray_map_bytes <= UINT32_MAX && pool_lt_bytes <= UINT32_MAX && pool_depth_bytes <= UINT32_MAX;
		const bool pool_valid = descriptor.pool_capacity == 0 ?
			(!descriptor.exception_lt_pool.is_valid() && !descriptor.exception_depth_pool.is_valid()) :
			(rd->storage_buffer_is_valid(descriptor.exception_lt_pool) && rd->storage_buffer_is_valid(descriptor.exception_depth_pool));
		if (!buffer_sizes_representable ||
				!texture_matches(descriptor.lt_texture, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, descriptor.trace_size.x, descriptor.trace_size.y, (uint32_t)view_depth) ||
				!texture_matches(descriptor.depth_texture, RD::DATA_FORMAT_R32_SFLOAT, descriptor.trace_size.x, descriptor.trace_size.y, (uint32_t)view_depth) ||
				!rd->storage_buffer_is_valid(descriptor.ray_map) || rd->storage_buffer_get_size(descriptor.ray_map) < ray_map_bytes ||
				!pool_valid || (descriptor.pool_capacity > 0 &&
					(rd->storage_buffer_get_size(descriptor.exception_lt_pool) < pool_lt_bytes || rd->storage_buffer_get_size(descriptor.exception_depth_pool) < pool_depth_bytes))) {
			return reject("Compact cloud-medium v2 resource RID failed texture format/extent/usage or pool-capacity validation.");
		}

		if (eye == 0) {
			common_allocation_generation = descriptor.allocation_generation;
			common_frame_generation = descriptor.frame_generation;
			common_weather_generation = descriptor.weather_generation;
			common_lighting_generation = descriptor.lighting_generation;
			common_candidate_layers = descriptor.candidate_layers;
		} else if (descriptor.trace_size != descriptors[0].trace_size || descriptor.output_size != descriptors[0].output_size ||
				descriptor.candidate_layers != common_candidate_layers || descriptor.lt_texture != descriptors[0].lt_texture || descriptor.depth_texture != descriptors[0].depth_texture ||
				descriptor.ray_map != descriptors[0].ray_map || descriptor.exception_lt_pool != descriptors[0].exception_lt_pool ||
				descriptor.exception_depth_pool != descriptors[0].exception_depth_pool || descriptor.pool_capacity != descriptors[0].pool_capacity ||
				descriptor.allocation_generation != common_allocation_generation || descriptor.frame_generation != common_frame_generation ||
				descriptor.weather_generation != common_weather_generation || descriptor.lighting_generation != common_lighting_generation) {
			return reject("Compact cloud-medium v2 publication must contain every eye from the same generation.");
		}
		descriptors.write[eye] = descriptor;
	}

	if (cloud_medium_has_generation) {
		if (common_allocation_generation < cloud_medium_last_allocation_generation ||
				(common_allocation_generation == cloud_medium_last_allocation_generation && common_frame_generation < cloud_medium_last_frame_generation) ||
				(common_allocation_generation == cloud_medium_last_allocation_generation && common_weather_generation < cloud_medium_last_weather_generation) ||
				(common_allocation_generation == cloud_medium_last_allocation_generation && common_lighting_generation < cloud_medium_last_lighting_generation)) {
			return reject("Stale compact cloud-medium v2 generation was rejected.");
		}
	}

	// Allocation preflight is not an authorization to publish or bind DIRECT.
	// The native sampler layout, authoritative source hash and GPU-use lease
	// have no verified implementation. Keep v2 entirely neutral for now.
	return reject("CloudDirectInputs allocation preflight passed, but native layout/hash/lease verification is absent; v2 publication remains disabled.");
}

RID RenderSceneBuffersRD::get_local_fog_texture() {
	if (!has_custom_data(RB_SCOPE_FOG)) { return RID(); }
	Ref<RendererRD::Fog::VolumetricFog> fog = get_custom_data(RB_SCOPE_FOG);
	return fog->fog_map;
}

Vector2 RenderSceneBuffersRD::get_local_fog_range() {
	if (!has_custom_data(RB_SCOPE_FOG)) { return Vector2(); }
	Ref<RendererRD::Fog::VolumetricFog> fog = get_custom_data(RB_SCOPE_FOG);
	return Vector2(fog->length, fog->spread);
}

Dictionary RenderSceneBuffersRD::get_local_fog_medium_capture() {
	Dictionary result;
	result["requested"] = local_fog_medium_capture_requested;
	result["units_accepted"] = false;
	result["gpu_completion_verified"] = false;
	result["production_published"] = false;
	result["mono_only"] = true;
	if (!local_fog_medium_capture_requested || !has_custom_data(RB_SCOPE_FOG)) { return result; }
	Ref<RendererRD::Fog::VolumetricFog> fog = get_custom_data(RB_SCOPE_FOG);
	if (!fog->local_medium_capture || !fog->local_medium_recorded) { return result; }
	result["recorded"] = true;
	result["recorded_frame"] = fog->local_medium_frame;
	result["payload_bytes"] = uint64_t(fog->width) * fog->height * fog->depth * 64;
	result["coefficients"] = fog->local_medium_coefficients;
	result["emission"] = fog->local_medium_emission;
	result["uncertified_source"] = fog->local_medium_source;
	result["direct_source_pre_camera"] = fog->local_medium_direct_source;
	result["direct_camera_normalization"] = fog->local_medium_camera_normalization;
	result["phase_g"] = fog->local_medium_phase_g;
	result["physical_light_units"] = fog->local_medium_physical_light_units;
	result["direct_source_unit_version"] = 1;
	result["direct_source_includes_cloud_air_attenuation"] = false;
	result["size"] = Vector3i(fog->width, fog->height, fog->depth);
	result["camera"] = fog->local_medium_camera;
	result["projection"] = fog->local_medium_projection;
	result["range"] = Vector2(fog->length, fog->spread);
	result["format_version"] = 2;
	return result;
}

void RenderSceneBuffersRD::update_sizes(NamedTexture &p_named_texture) {
	ERR_FAIL_COND(p_named_texture.texture.is_null());

	p_named_texture.sizes.resize(p_named_texture.format.mipmaps);

	Size2i mipmap_size = Size2i(p_named_texture.format.width, p_named_texture.format.height);
	for (uint32_t mipmap = 0; mipmap < p_named_texture.format.mipmaps; mipmap++) {
		p_named_texture.sizes.ptrw()[mipmap] = mipmap_size;

		mipmap_size = Size2i(mipmap_size.width >> 1, mipmap_size.height >> 1).maxi(1);
	}
}

void RenderSceneBuffersRD::free_named_texture(NamedTexture &p_named_texture) {
	if (p_named_texture.texture.is_valid()) {
		RD::get_singleton()->free_rid(p_named_texture.texture);
	}
	p_named_texture.texture = RID();
	p_named_texture.slices.clear(); // slices should be freed automatically as dependents...
}

void RenderSceneBuffersRD::update_samplers() {
	float computed_mipmap_bias = texture_mipmap_bias;

	if (use_taa || (RSE::scaling_3d_mode_type(scaling_3d_mode) == RSE::VIEWPORT_SCALING_3D_TYPE_TEMPORAL)) {
		// Use negative mipmap LOD bias when TAA or FSR2 is enabled to compensate for loss of sharpness.
		// This restores sharpness in still images to be roughly at the same level as without TAA,
		// but moving scenes will still be blurrier.
		computed_mipmap_bias -= 0.5;
	}

	if (screen_space_aa == RSE::VIEWPORT_SCREEN_SPACE_AA_FXAA) {
		// Use negative mipmap LOD bias when FXAA is enabled to compensate for loss of sharpness.
		// If both TAA and FXAA are enabled, combine their negative LOD biases together.
		computed_mipmap_bias -= 0.25;
	}

	RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();
	material_storage->samplers_rd_free(samplers);
	samplers = material_storage->samplers_rd_allocate(computed_mipmap_bias, anisotropic_filtering_level);
}

void RenderSceneBuffersRD::cleanup() {
	// Borrowed cloud resources may not outlive this view-buffer allocation.
	set_cloud_medium(RID(), 0.0f);

	// Free our data buffers (but don't destroy them)
	for (KeyValue<StringName, Ref<RenderBufferCustomDataRD>> &E : data_buffers) {
		E.value->free_data();
	}

	// Clear our named textures
	for (KeyValue<NTKey, NamedTexture> &E : named_textures) {
		free_named_texture(E.value);
	}
	named_textures.clear();

	// Clear weight_buffer / blur textures.
	for (WeightBuffers &weight_buffer : weight_buffers) {
		if (weight_buffer.weight.is_valid()) {
			RD::get_singleton()->free_rid(weight_buffer.weight);
			weight_buffer.weight = RID();
		}
	}

#ifdef METAL_ENABLED
	if (mfx_spatial_context) {
		memdelete(mfx_spatial_context);
		mfx_spatial_context = nullptr;
	}
#endif
}

void RenderSceneBuffersRD::configure(const RenderSceneBuffersConfiguration *p_config) {
	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();

	render_target = p_config->get_render_target();
	force_hdr = texture_storage->render_target_is_using_hdr(render_target);

	target_size = p_config->get_target_size();
	internal_size = p_config->get_internal_size();
	view_count = p_config->get_view_count();

	scaling_3d_mode = p_config->get_scaling_3d_mode();
	msaa_3d = p_config->get_msaa_3d();
	screen_space_aa = p_config->get_screen_space_aa();

	fsr_sharpness = p_config->get_fsr_sharpness();
	texture_mipmap_bias = p_config->get_texture_mipmap_bias();
	anisotropic_filtering_level = p_config->get_anisotropic_filtering_level();
	use_taa = p_config->get_use_taa();
	use_debanding = p_config->get_use_debanding();

	ERR_FAIL_COND_MSG(view_count == 0, "Must have at least 1 view");

	vrs_mode = texture_storage->render_target_get_vrs_mode(render_target);

	update_samplers();

	// cleanout any old buffers we had.
	cleanup();

	// Create our color buffer.
	const bool resolve_target = msaa_3d != RSE::VIEWPORT_MSAA_DISABLED;
	create_texture(RB_SCOPE_BUFFERS, RB_TEX_COLOR, get_base_data_format(), get_color_usage_bits(resolve_target, false, can_be_storage));

	// TODO: Detect when it is safe to use RD::TEXTURE_USAGE_TRANSIENT_BIT for RB_TEX_DEPTH, RB_TEX_COLOR_MSAA and/or RB_TEX_DEPTH_MSAA.
	// (it means we cannot sample from it, we cannot copy from/to it) to save VRAM (and maybe performance too).

	// Create our depth buffer.
	create_texture(RB_SCOPE_BUFFERS, RB_TEX_DEPTH, get_depth_format(resolve_target, false, can_be_storage), get_depth_usage_bits(resolve_target, false, can_be_storage));

	// Create our MSAA buffers.
	if (msaa_3d == RSE::VIEWPORT_MSAA_DISABLED) {
		texture_samples = RD::TEXTURE_SAMPLES_1;
	} else {
		texture_samples = msaa_to_samples(msaa_3d);
		create_texture(RB_SCOPE_BUFFERS, RB_TEX_COLOR_MSAA, get_base_data_format(), get_color_usage_bits(false, true, can_be_storage), texture_samples, Size2i(), 0, 1, true, true);
		create_texture(RB_SCOPE_BUFFERS, RB_TEX_DEPTH_MSAA, get_depth_format(false, true, can_be_storage), get_depth_usage_bits(false, true, can_be_storage), texture_samples, Size2i(), 0, 1, true, true);
	}

	// VRS (note, our vrs object will only be set if VRS is supported)
	RID vrs_texture;
	if (vrs && vrs_mode != RSE::VIEWPORT_VRS_DISABLED) {
		vrs_texture = create_texture(RB_SCOPE_VRS, RB_TEXTURE, get_vrs_format(), get_vrs_usage_bits(), RD::TEXTURE_SAMPLES_1, vrs->get_vrs_texture_size(internal_size));
	}

	// (re-)configure any named buffers
	for (KeyValue<StringName, Ref<RenderBufferCustomDataRD>> &E : data_buffers) {
		E.value->configure(this);
	}
}

void RenderSceneBuffersRD::configure_for_reflections(const Size2i p_reflection_size) {
	// For now our render buffers for reflections are only used for effects/environment (Sky/Fog/Etc)
	// Possibly at some point move our entire reflection atlas buffer management into this class

	target_size = p_reflection_size;
	internal_size = p_reflection_size;
	render_target = RID();
	scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_OFF;
	fsr_sharpness = 0.0;
	msaa_3d = RSE::VIEWPORT_MSAA_DISABLED;
	screen_space_aa = RSE::VIEWPORT_SCREEN_SPACE_AA_DISABLED;
	use_taa = false;
	use_debanding = false;
	view_count = 1;

	// cleanout any old buffers we had.
	cleanup();

	// (re-)configure any named buffers
	for (KeyValue<StringName, Ref<RenderBufferCustomDataRD>> &E : data_buffers) {
		E.value->configure(this);
	}
}

void RenderSceneBuffersRD::set_fsr_sharpness(float p_fsr_sharpness) {
	fsr_sharpness = p_fsr_sharpness;
}

void RenderSceneBuffersRD::set_texture_mipmap_bias(float p_texture_mipmap_bias) {
	texture_mipmap_bias = p_texture_mipmap_bias;

	update_samplers();
}

void RenderSceneBuffersRD::set_anisotropic_filtering_level(RSE::ViewportAnisotropicFiltering p_anisotropic_filtering_level) {
	anisotropic_filtering_level = p_anisotropic_filtering_level;

	update_samplers();
}

void RenderSceneBuffersRD::set_use_debanding(bool p_use_debanding) {
	use_debanding = p_use_debanding;
}

#ifdef METAL_ENABLED
void RenderSceneBuffersRD::ensure_mfx(RendererRD::MFXSpatialEffect *p_effect) {
	if (mfx_spatial_context) {
		return;
	}

	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
	RenderingDevice *rd = RD::get_singleton();

	// Determine the output format of the render target.
	RID dest = texture_storage->render_target_get_rd_texture(render_target);
	RD::TextureFormat tf = rd->texture_get_format(dest);
	RD::DataFormat output_format = tf.format;

	RendererRD::MFXSpatialEffect::CreateParams params = {
		.input_size = internal_size,
		.output_size = target_size,
		.input_format = get_base_data_format(),
		.output_format = output_format,
	};

	mfx_spatial_context = p_effect->create_context(params);
}
#endif

// Named textures

bool RenderSceneBuffersRD::has_texture(const StringName &p_context, const StringName &p_texture_name) const {
	NTKey key(p_context, p_texture_name);

	return named_textures.has(key);
}

RID RenderSceneBuffersRD::create_texture(const StringName &p_context, const StringName &p_texture_name, const RD::DataFormat p_data_format, const uint32_t p_usage_bits, const RD::TextureSamples p_texture_samples, const Size2i p_size, const uint32_t p_layers, const uint32_t p_mipmaps, bool p_unique, bool p_discardable) {
	// Keep some useful data, we use default values when these are 0.
	Size2i size = p_size == Size2i(0, 0) ? internal_size : p_size;
	uint32_t layers = p_layers == 0 ? view_count : p_layers;
	uint32_t mipmaps = p_mipmaps == 0 ? 1 : p_mipmaps;

	// Create our texture
	RD::TextureFormat tf;
	tf.format = p_data_format;
	if (layers > 1) {
		tf.texture_type = RD::TEXTURE_TYPE_2D_ARRAY;
	}

	tf.width = size.x;
	tf.height = size.y;
	tf.depth = 1;
	tf.array_layers = layers;
	tf.mipmaps = mipmaps;
	tf.usage_bits = p_usage_bits;
	tf.samples = p_texture_samples;
	tf.is_discardable = p_discardable;

	return create_texture_from_format(p_context, p_texture_name, tf, RD::TextureView(), p_unique);
}

RID RenderSceneBuffersRD::_create_texture_from_format(const StringName &p_context, const StringName &p_texture_name, const Ref<RDTextureFormat> &p_texture_format, const Ref<RDTextureView> &p_view, bool p_unique) {
	ERR_FAIL_COND_V(p_texture_format.is_null(), RID());

	RD::TextureView texture_view;
	if (p_view.is_valid()) { // only use when supplied, else default.
		texture_view = p_view->base;
	}

	return create_texture_from_format(p_context, p_texture_name, p_texture_format->base, texture_view, p_unique);
}

RID RenderSceneBuffersRD::create_texture_from_format(const StringName &p_context, const StringName &p_texture_name, const RD::TextureFormat &p_texture_format, RD::TextureView p_view, bool p_unique) {
	// TODO p_unique, if p_unique is true, this is a texture that can be shared. This will be implemented later as an optimization.

	NTKey key(p_context, p_texture_name);

	// check if this is a known texture
	if (named_textures.has(key)) {
		return named_textures[key].texture;
	}

	// Add a new entry..
	NamedTexture &named_texture = named_textures[key];
	named_texture.format = p_texture_format;
	named_texture.is_unique = p_unique;
	named_texture.texture = RD::get_singleton()->texture_create(p_texture_format, p_view);

	Array arr = { p_context, p_texture_name };
	RD::get_singleton()->set_resource_name(named_texture.texture, String("RenderBuffer {0}/{1}").format(arr));

	update_sizes(named_texture);

	// The rest is lazy created..

	return named_texture.texture;
}

RID RenderSceneBuffersRD::_create_texture_view(const StringName &p_context, const StringName &p_texture_name, const StringName &p_view_name, const Ref<RDTextureView> p_view) {
	RD::TextureView texture_view;
	if (p_view.is_valid()) { // only use when supplied, else default.
		texture_view = p_view->base;
	}

	return create_texture_view(p_context, p_texture_name, p_view_name, texture_view);
}

RID RenderSceneBuffersRD::create_texture_view(const StringName &p_context, const StringName &p_texture_name, const StringName &p_view_name, RD::TextureView p_view) {
	NTKey view_key(p_context, p_view_name);

	// check if this is a known texture
	if (named_textures.has(view_key)) {
		return named_textures[view_key].texture;
	}

	NTKey key(p_context, p_texture_name);

	ERR_FAIL_COND_V(!named_textures.has(key), RID());

	NamedTexture &named_texture = named_textures[key];
	NamedTexture &view_texture = named_textures[view_key];

	view_texture.format = named_texture.format;
	view_texture.is_unique = named_texture.is_unique;

	view_texture.texture = RD::get_singleton()->texture_create_shared(p_view, named_texture.texture);

	Array arr = { p_context, p_view_name };
	RD::get_singleton()->set_resource_name(view_texture.texture, String("RenderBuffer View {0}/{1}").format(arr));

	update_sizes(named_texture);

	return view_texture.texture;
}

RID RenderSceneBuffersRD::get_texture(const StringName &p_context, const StringName &p_texture_name) const {
	NTKey key(p_context, p_texture_name);

	ERR_FAIL_COND_V(!named_textures.has(key), RID());

	return named_textures[key].texture;
}

Ref<RDTextureFormat> RenderSceneBuffersRD::_get_texture_format(const StringName &p_context, const StringName &p_texture_name) const {
	Ref<RDTextureFormat> tf;
	tf.instantiate();

	tf->base = get_texture_format(p_context, p_texture_name);

	return tf;
}

RID RenderSceneBuffersRD::_get_texture_slice_view(const StringName &p_context, const StringName &p_texture_name, const uint32_t p_layer, const uint32_t p_mipmap, const uint32_t p_layers, const uint32_t p_mipmaps, const Ref<RDTextureView> p_view) {
	RD::TextureView texture_view;
	if (p_view.is_valid()) {
		texture_view = p_view->base;
	}

	return get_texture_slice_view(p_context, p_texture_name, p_layer, p_mipmap, p_layers, p_mipmaps, texture_view);
}

const RD::TextureFormat RenderSceneBuffersRD::get_texture_format(const StringName &p_context, const StringName &p_texture_name) const {
	NTKey key(p_context, p_texture_name);

	ERR_FAIL_COND_V(!named_textures.has(key), RD::TextureFormat());

	return named_textures[key].format;
}

RID RenderSceneBuffersRD::get_texture_slice(const StringName &p_context, const StringName &p_texture_name, const uint32_t p_layer, const uint32_t p_mipmap, const uint32_t p_layers, const uint32_t p_mipmaps) {
	return get_texture_slice_view(p_context, p_texture_name, p_layer, p_mipmap, p_layers, p_mipmaps, RD::TextureView());
}

RID RenderSceneBuffersRD::get_texture_slice_view(const StringName &p_context, const StringName &p_texture_name, const uint32_t p_layer, const uint32_t p_mipmap, const uint32_t p_layers, const uint32_t p_mipmaps, RD::TextureView p_view) {
	NTKey key(p_context, p_texture_name);

	// check if this is a known texture
	ERR_FAIL_COND_V(!named_textures.has(key), RID());
	NamedTexture &named_texture = named_textures[key];
	ERR_FAIL_COND_V(named_texture.texture.is_null(), RID());

	// check if we're in bounds
	ERR_FAIL_UNSIGNED_INDEX_V(p_layer, named_texture.format.array_layers, RID());
	ERR_FAIL_COND_V(p_layers == 0, RID());
	ERR_FAIL_COND_V(p_layer + p_layers > named_texture.format.array_layers, RID());
	ERR_FAIL_UNSIGNED_INDEX_V(p_mipmap, named_texture.format.mipmaps, RID());
	ERR_FAIL_COND_V(p_mipmaps == 0, RID());
	ERR_FAIL_COND_V(p_mipmap + p_mipmaps > named_texture.format.mipmaps, RID());

	// asking the whole thing? just return the original
	RD::TextureView default_view = RD::TextureView();
	if (p_layer == 0 && p_mipmap == 0 && named_texture.format.array_layers == p_layers && named_texture.format.mipmaps == p_mipmaps && p_view == default_view) {
		return named_texture.texture;
	}

	// see if we have this
	NTSliceKey slice_key(p_layer, p_layers, p_mipmap, p_mipmaps, p_view);
	if (named_texture.slices.has(slice_key)) {
		return named_texture.slices[slice_key];
	}

	// create our slice
	RID &slice = named_texture.slices[slice_key];
	slice = RD::get_singleton()->texture_create_shared_from_slice(p_view, named_texture.texture, p_layer, p_mipmap, p_mipmaps, p_layers > 1 ? RD::TEXTURE_SLICE_2D_ARRAY : RD::TEXTURE_SLICE_2D, p_layers);

	Array arr = {
		p_context,
		p_texture_name,
		itos(p_layer),
		itos(p_layers),
		itos(p_mipmap),
		itos(p_mipmaps),
		itos(p_view.format_override),
		itos(p_view.swizzle_r),
		itos(p_view.swizzle_g),
		itos(p_view.swizzle_b),
		itos(p_view.swizzle_a)
	};
	RD::get_singleton()->set_resource_name(slice, String("RenderBuffer {0}/{1}, layer {2}/{3}, mipmap {4}/{5}, view {6}/{7}/{8}/{9}/{10}").format(arr));

	// and return our slice
	return slice;
}

Size2i RenderSceneBuffersRD::get_texture_slice_size(const StringName &p_context, const StringName &p_texture_name, const uint32_t p_mipmap) {
	NTKey key(p_context, p_texture_name);

	// check if this is a known texture
	ERR_FAIL_COND_V(!named_textures.has(key), Size2i());
	NamedTexture &named_texture = named_textures[key];
	ERR_FAIL_COND_V(named_texture.texture.is_null(), Size2i());

	// check if we're in bounds
	ERR_FAIL_UNSIGNED_INDEX_V(p_mipmap, named_texture.format.mipmaps, Size2i());

	// return our size
	return named_texture.sizes[p_mipmap];
}

void RenderSceneBuffersRD::clear_context(const StringName &p_context) {
	Vector<NTKey> to_free; // free these

	// Find all entries for our context, we don't want to free them yet or our loop fails.
	for (KeyValue<NTKey, NamedTexture> &E : named_textures) {
		if (E.key.context == p_context) {
			to_free.push_back(E.key);
		}
	}

	// Now free these and remove them from our textures
	for (NTKey &key : to_free) {
		free_named_texture(named_textures[key]);
		named_textures.erase(key);
	}
}

// Allocate shared buffers
void RenderSceneBuffersRD::allocate_blur_textures() {
	if (has_texture(RB_SCOPE_BUFFERS, RB_TEX_BLUR_0)) {
		// already allocated...
		return;
	}

	Size2i blur_size = internal_size;
	if (RSE::scaling_3d_mode_type(scaling_3d_mode) == RSE::VIEWPORT_SCALING_3D_TYPE_TEMPORAL) {
		// The blur texture should be as big as the target size when using an upscaler.
		blur_size = target_size;
	}

	uint32_t mipmaps_required = Image::get_image_required_mipmaps(blur_size.x, blur_size.y, Image::FORMAT_RGBAH);

	uint32_t usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	if (can_be_storage) {
		usage_bits += RD::TEXTURE_USAGE_STORAGE_BIT;
	} else {
		usage_bits += RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	create_texture(RB_SCOPE_BUFFERS, RB_TEX_BLUR_0, get_base_data_format(), usage_bits, RD::TEXTURE_SAMPLES_1, blur_size, view_count, mipmaps_required);
	create_texture(RB_SCOPE_BUFFERS, RB_TEX_BLUR_1, get_base_data_format(), usage_bits, RD::TEXTURE_SAMPLES_1, Size2i(blur_size.x >> 1, blur_size.y >> 1), view_count, mipmaps_required - 1);

	// TODO redo this:
	if (!can_be_storage) {
		// create 4 weight textures, 2 full size, 2 half size

		RD::TextureFormat tf;
		tf.format = RD::DATA_FORMAT_R16_SFLOAT; // We could probably use DATA_FORMAT_R8_SNORM if we don't pre-multiply by blur_size but that depends on whether we can remove DEPTH_GAP
		tf.width = blur_size.x;
		tf.height = blur_size.y;
		tf.texture_type = RD::TEXTURE_TYPE_2D;
		tf.array_layers = 1; // Our DOF effect handles one eye per turn
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT | RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;
		tf.mipmaps = 1;
		for (uint32_t i = 0; i < 4; i++) {
			// associated blur texture
			RID texture;
			if (i == 1) {
				texture = get_texture_slice(RB_SCOPE_BUFFERS, RB_TEX_BLUR_0, 0, 0);
			} else if (i == 2) {
				texture = get_texture_slice(RB_SCOPE_BUFFERS, RB_TEX_BLUR_1, 0, 0);
			} else if (i == 3) {
				texture = get_texture_slice(RB_SCOPE_BUFFERS, RB_TEX_BLUR_0, 0, 1);
			}

			// create weight texture
			weight_buffers[i].weight = RD::get_singleton()->texture_create(tf, RD::TextureView());

			// create frame buffer
			Vector<RID> fb;
			if (i != 0) {
				fb.push_back(texture);
			}
			fb.push_back(weight_buffers[i].weight);
			weight_buffers[i].fb = RD::get_singleton()->framebuffer_create(fb);

			if (i == 1) {
				// next 2 are half size
				tf.width = MAX(1u, tf.width >> 1);
				tf.height = MAX(1u, tf.height >> 1);
			}
		}
	}
}

// Data buffers

bool RenderSceneBuffersRD::has_custom_data(const StringName &p_name) {
	return data_buffers.has(p_name);
}

void RenderSceneBuffersRD::set_custom_data(const StringName &p_name, Ref<RenderBufferCustomDataRD> p_data) {
	if (p_data.is_valid()) {
		data_buffers[p_name] = p_data;
	} else if (has_custom_data(p_name)) {
		data_buffers.erase(p_name);
	}
}

Ref<RenderBufferCustomDataRD> RenderSceneBuffersRD::get_custom_data(const StringName &p_name) const {
	ERR_FAIL_COND_V(!data_buffers.has(p_name), Ref<RenderBufferCustomDataRD>());

	Ref<RenderBufferCustomDataRD> ret = data_buffers[p_name];

	return ret;
}

// Depth texture

bool RenderSceneBuffersRD::has_depth_texture() {
	if (render_target.is_null()) {
		// not applicable when there is no render target (likely this is for a reflection probe)
		return false;
	}

	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
	RID depth = texture_storage->render_target_get_override_depth(render_target);
	if (depth.is_valid()) {
		return true;
	} else {
		return has_texture(RB_SCOPE_BUFFERS, RB_TEX_DEPTH);
	}
}

RID RenderSceneBuffersRD::get_depth_texture() {
	if (render_target.is_null()) {
		// not applicable when there is no render target (likely this is for a reflection probe)
		return RID();
	}

	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
	RID depth = texture_storage->render_target_get_override_depth(render_target);
	if (depth.is_valid()) {
		return depth;
	} else {
		return get_texture(RB_SCOPE_BUFFERS, RB_TEX_DEPTH);
	}
}

RID RenderSceneBuffersRD::get_depth_texture(const uint32_t p_layer) {
	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
	RID depth_slice = texture_storage->render_target_get_override_depth_slice(render_target, p_layer);
	if (depth_slice.is_valid()) {
		return depth_slice;
	} else {
		return get_texture_slice(RB_SCOPE_BUFFERS, RB_TEX_DEPTH, p_layer, 0);
	}
}

// Subsampled textures.

RID RenderSceneBuffersRD::get_color_subsampled() {
	const bool use_msaa = (msaa_3d != RSE::VIEWPORT_MSAA_DISABLED);

	RD::TextureFormat tf;
	tf.format = get_base_data_format();
	if (view_count > 1) {
		tf.texture_type = RD::TEXTURE_TYPE_2D_ARRAY;
	}
	tf.width = internal_size.x;
	tf.height = internal_size.y;
	tf.array_layers = view_count;
	tf.usage_bits = get_color_usage_bits(use_msaa, false, can_be_storage);
	tf.is_subsampled = true;

	return create_texture_from_format(RB_SCOPE_BUFFERS, RB_TEX_COLOR_SUBSAMPLED, tf);
}

RID RenderSceneBuffersRD::get_color_msaa_subsampled() {
	ERR_FAIL_COND_V(msaa_3d == RSE::VIEWPORT_MSAA_DISABLED, RID());

	RD::TextureFormat tf;
	tf.format = get_base_data_format();
	if (view_count > 1) {
		tf.texture_type = RD::TEXTURE_TYPE_2D_ARRAY;
	}
	tf.width = internal_size.x;
	tf.height = internal_size.y;
	tf.array_layers = view_count;
	tf.samples = msaa_to_samples(msaa_3d);
	tf.usage_bits = get_color_usage_bits(false, true, can_be_storage);
	tf.is_discardable = true;
	tf.is_subsampled = true;

	return create_texture_from_format(RB_SCOPE_BUFFERS, RB_TEX_COLOR_MSAA_SUBSAMPLED, tf);
}

RID RenderSceneBuffersRD::get_depth_subsampled() {
	const bool use_msaa = (msaa_3d != RSE::VIEWPORT_MSAA_DISABLED);

	RD::TextureFormat tf;
	tf.format = get_depth_format(use_msaa, false, can_be_storage);
	if (view_count > 1) {
		tf.texture_type = RD::TEXTURE_TYPE_2D_ARRAY;
	}
	tf.width = internal_size.x;
	tf.height = internal_size.y;
	tf.array_layers = view_count;
	tf.usage_bits = get_depth_usage_bits(use_msaa, false, can_be_storage);
	tf.is_subsampled = true;

	return create_texture_from_format(RB_SCOPE_BUFFERS, RB_TEX_DEPTH_SUBSAMPLED, tf);
}

RID RenderSceneBuffersRD::get_depth_msaa_subsampled() {
	ERR_FAIL_COND_V(msaa_3d == RSE::VIEWPORT_MSAA_DISABLED, RID());

	RD::TextureFormat tf;
	tf.format = get_depth_format(false, true, can_be_storage);
	if (view_count > 1) {
		tf.texture_type = RD::TEXTURE_TYPE_2D_ARRAY;
	}
	tf.width = internal_size.x;
	tf.height = internal_size.y;
	tf.array_layers = view_count;
	tf.samples = msaa_to_samples(msaa_3d);
	tf.usage_bits = get_depth_usage_bits(false, true, can_be_storage);
	tf.is_discardable = true;
	tf.is_subsampled = true;

	return create_texture_from_format(RB_SCOPE_BUFFERS, RB_TEX_DEPTH_MSAA_SUBSAMPLED, tf);
}

// Upscaled texture.

void RenderSceneBuffersRD::ensure_upscaled() {
	if (!has_upscaled_texture()) {
		uint32_t usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | (can_be_storage ? RD::TEXTURE_USAGE_STORAGE_BIT : 0) | RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;
		usage_bits |= RD::TEXTURE_USAGE_INPUT_ATTACHMENT_BIT;
		create_texture(RB_SCOPE_BUFFERS, RB_TEX_COLOR_UPSCALED, get_base_data_format(), usage_bits, RD::TEXTURE_SAMPLES_1, target_size);
	}
}

// Velocity texture.

void RenderSceneBuffersRD::ensure_velocity() {
	if (!has_texture(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY)) {
		const bool msaa = msaa_3d != RSE::VIEWPORT_MSAA_DISABLED;
		create_texture(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY, get_velocity_format(), get_velocity_usage_bits(msaa, false, can_be_storage));

		if (msaa) {
			create_texture(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY_MSAA, get_velocity_format(), get_velocity_usage_bits(false, msaa, can_be_storage), texture_samples, Size2i(), 0, 1, true, true);
		}
	}
}

bool RenderSceneBuffersRD::has_velocity_buffer(bool p_has_msaa) {
	if (p_has_msaa) {
		return has_texture(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY_MSAA);
	} else {
		RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
		RID velocity = texture_storage->render_target_get_override_velocity(render_target);
		if (velocity.is_valid()) {
			return true;
		} else {
			return has_texture(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY);
		}
	}
}

RID RenderSceneBuffersRD::get_velocity_buffer(bool p_get_msaa) {
	if (p_get_msaa) {
		if (!has_texture(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY_MSAA)) {
			return RID();
		} else {
			return get_texture(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY_MSAA);
		}
	} else {
		RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
		RID velocity = texture_storage->render_target_get_override_velocity(render_target);
		if (velocity.is_valid()) {
			return velocity;
		} else if (!has_texture(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY)) {
			return RID();
		} else {
			return get_texture(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY);
		}
	}
}

RID RenderSceneBuffersRD::get_velocity_buffer(bool p_get_msaa, uint32_t p_layer) {
	if (p_get_msaa) {
		return get_texture_slice(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY_MSAA, p_layer, 0);
	} else {
		RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
		RID velocity_slice = texture_storage->render_target_get_override_velocity_slice(render_target, p_layer);
		if (velocity_slice.is_valid()) {
			return velocity_slice;
		} else {
			return get_texture_slice(RB_SCOPE_BUFFERS, RB_TEX_VELOCITY, p_layer, 0);
		}
	}
}

RID RenderSceneBuffersRD::get_velocity_depth_buffer() {
	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
	RID velocity_depth = texture_storage->render_target_get_override_velocity_depth(render_target);
	return velocity_depth;
}

void RenderSceneBuffersRD::ensure_reactive_mask() {
	if (!has_texture(RB_SCOPE_BUFFERS, RB_TEX_REACTIVE)) {
		const bool msaa = msaa_3d != RSE::VIEWPORT_MSAA_DISABLED;
		create_texture(RB_SCOPE_BUFFERS, RB_TEX_REACTIVE, RD::DATA_FORMAT_R8_UNORM, get_color_usage_bits(msaa, false, can_be_storage));

		if (msaa) {
			create_texture(RB_SCOPE_BUFFERS, RB_TEX_REACTIVE_MSAA, RD::DATA_FORMAT_R8_UNORM, get_color_usage_bits(false, msaa, can_be_storage), texture_samples);
		}
	}
}

bool RenderSceneBuffersRD::has_reactive_mask(bool p_has_msaa) {
	return has_texture(RB_SCOPE_BUFFERS, p_has_msaa ? RB_TEX_REACTIVE_MSAA : RB_TEX_REACTIVE);
}

RID RenderSceneBuffersRD::get_reactive_mask(bool p_get_msaa) {
	return get_texture(RB_SCOPE_BUFFERS, p_get_msaa ? RB_TEX_REACTIVE_MSAA : RB_TEX_REACTIVE);
}

RID RenderSceneBuffersRD::get_reactive_mask(bool p_get_msaa, uint32_t p_layer) {
	return get_texture_slice(RB_SCOPE_BUFFERS, p_get_msaa ? RB_TEX_REACTIVE_MSAA : RB_TEX_REACTIVE, p_layer, 0);
}

uint32_t RenderSceneBuffersRD::get_color_usage_bits(bool p_resolve, bool p_msaa, bool p_storage) {
	DEV_ASSERT((!p_resolve && !p_msaa) || (p_resolve != p_msaa));

	uint32_t usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RD::TEXTURE_USAGE_INPUT_ATTACHMENT_BIT;
	if (p_msaa) {
		usage_bits |= RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
	} else if (p_resolve) {
		usage_bits |= RD::TEXTURE_USAGE_CAN_COPY_TO_BIT | (p_storage ? RD::TEXTURE_USAGE_STORAGE_BIT : 0);
	} else {
		usage_bits |= (p_storage ? RD::TEXTURE_USAGE_STORAGE_BIT : 0);
	}

	return usage_bits;
}

RD::DataFormat RenderSceneBuffersRD::get_depth_format(bool p_resolve, bool p_msaa, bool p_storage) {
	if (p_resolve && (p_storage || !RenderingDevice::get_singleton()->has_feature(RD::SUPPORTS_FRAMEBUFFER_DEPTH_RESOLVE))) {
		// Use R32 for resolve on Forward+ (p_storage == true), or if we don't support depth resolve.
		return RD::DATA_FORMAT_R32_SFLOAT;
	} else {
		const RenderingDeviceCommons::DataFormat preferred_formats[2] = {
			p_storage ? RD::DATA_FORMAT_D32_SFLOAT_S8_UINT : RD::DATA_FORMAT_D24_UNORM_S8_UINT,
			p_storage ? RD::DATA_FORMAT_D24_UNORM_S8_UINT : RD::DATA_FORMAT_D32_SFLOAT_S8_UINT
		};

		return RD::get_singleton()->texture_is_format_supported_for_usage(preferred_formats[0], get_depth_usage_bits(p_resolve, p_msaa, p_storage)) ? preferred_formats[0] : preferred_formats[1];
	}
}

uint32_t RenderSceneBuffersRD::get_depth_usage_bits(bool p_resolve, bool p_msaa, bool p_storage) {
	DEV_ASSERT((!p_resolve && !p_msaa) || (p_resolve != p_msaa));

	uint32_t usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT;
	if (p_msaa) {
		usage_bits |= RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
	} else if (p_resolve) {
		usage_bits |= RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
		if (p_storage) {
			usage_bits |= RD::TEXTURE_USAGE_STORAGE_BIT;
		} else if (RenderingDevice::get_singleton()->has_feature(RD::SUPPORTS_FRAMEBUFFER_DEPTH_RESOLVE)) {
			// We're able to resolve depth in (sub)passes and we make use of this in our mobile renderer.
			usage_bits |= RD::TEXTURE_USAGE_DEPTH_RESOLVE_ATTACHMENT_BIT;
		}
	} else {
		usage_bits |= RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

	return usage_bits;
}

RD::DataFormat RenderSceneBuffersRD::get_velocity_format() {
	return RD::DATA_FORMAT_R16G16_SFLOAT;
}

uint32_t RenderSceneBuffersRD::get_velocity_usage_bits(bool p_resolve, bool p_msaa, bool p_storage) {
	return get_color_usage_bits(p_resolve, p_msaa, p_storage);
}

RD::DataFormat RenderSceneBuffersRD::get_vrs_format() {
	return RD::get_singleton()->vrs_get_format();
}

uint32_t RenderSceneBuffersRD::get_vrs_usage_bits() {
	return RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_VRS_ATTACHMENT_BIT;
}

float RenderSceneBuffersRD::get_luminance_multiplier() const {
	// On mobile renderer when not using HDR2D we need to scale HDR values by two
	// to fit 0-2 range color values into a UNORM buffer.
	return (force_hdr || can_be_storage) ? 1.0 : 2.0;
}
