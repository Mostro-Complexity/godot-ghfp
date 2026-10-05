#ifndef FOG_SOURCE_UNITS_INC_GLSL
#define FOG_SOURCE_UNITS_INC_GLSL
// Independently authored unit adapter. LightStorage multiplies direct-light
// uploads by this render's camera normalization. Do not use this operation on
// the combined fog source: authored emission/ambient and baked GI differ.
vec4 fog_direct_source_pre_camera(vec3 normalized_phase_weighted_light,
        vec3 scattering_per_metre, float actual_camera_normalization) {
    bool valid = !isnan(actual_camera_normalization) && !isinf(actual_camera_normalization) &&
            actual_camera_normalization > 0.0 &&
            !any(isnan(normalized_phase_weighted_light)) && !any(isinf(normalized_phase_weighted_light)) &&
            !any(isnan(scattering_per_metre)) && !any(isinf(scattering_per_metre)) &&
            all(greaterThanEqual(normalized_phase_weighted_light, vec3(0.0))) &&
            all(greaterThanEqual(scattering_per_metre, vec3(0.0)));
    if (!valid) { return vec4(0.0); }
    vec3 source = (normalized_phase_weighted_light / actual_camera_normalization) * scattering_per_metre;
    valid = !any(isnan(source)) && !any(isinf(source)) && all(greaterThanEqual(source, vec3(0.0)));
    return valid ? vec4(source, 1.0) : vec4(0.0);
}
#endif
