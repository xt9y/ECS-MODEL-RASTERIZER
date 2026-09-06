#ifndef RW_ENGINE_RENDERER_PATHTRACER_SVGF_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_SVGF_SHADERS_HPP

namespace Renderer::SvgfShaders {

inline constexpr const char *common = R"GLSL(
#version 430

struct SurfaceData {
    vec4 position_depth;
    vec4 normal_material;
    vec4 uv;
    vec4 direct;
};

struct ReservoirData {
    vec4 sample_position;
    vec4 sample_normal;
    vec4 radiance;
    vec4 weights;
};

struct LightingData { vec4 color; };
struct MomentsData { vec4 value; };

uniform ivec2 uResolution;

uint pixelIndex(ivec2 pixel)
{
    return uint(pixel.y * uResolution.x + pixel.x);
}

bool inBounds(ivec2 pixel)
{
    return all(greaterThanEqual(pixel, ivec2(0))) && all(lessThan(pixel, uResolution));
}

bool validSurface(SurfaceData surface)
{
    return surface.position_depth.w > 0.0;
}

float luminance(vec3 color)
{
    return dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

bool historyCompatible(SurfaceData current, SurfaceData previous, vec3 previous_camera_position)
{
    if (!validSurface(current) || !validSurface(previous)) return false;
    float expected_depth = length(current.position_depth.xyz - previous_camera_position);
    float depth_error = abs(previous.position_depth.w - expected_depth) /
        max(max(previous.position_depth.w, expected_depth), 1.0e-3);
    float normal_similarity = dot(current.normal_material.xyz, previous.normal_material.xyz);
    float world_error = length(current.position_depth.xyz - previous.position_depth.xyz);
    float world_tolerance = max(0.025, expected_depth * 0.025);
    return normal_similarity >= 0.88 && depth_error <= 0.05 && world_error <= world_tolerance;
}
)GLSL";

inline constexpr const char *compose = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) readonly buffer PrimarySurfaces { SurfaceData primary_surfaces[]; };
layout(std430, binding = 1) readonly buffer Reservoirs { ReservoirData reservoirs[]; };
layout(std430, binding = 2) writeonly buffer CurrentLighting { LightingData current_lighting[]; };

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (!inBounds(pixel)) return;
    uint index = pixelIndex(pixel);
    SurfaceData surface = primary_surfaces[index];
    vec3 color = vec3(0.0);
    if (validSurface(surface)) {
        color = max(surface.direct.rgb, vec3(0.0));
        ReservoirData reservoir = reservoirs[index];
        if (
            reservoir.sample_position.w > 0.0 &&
            reservoir.weights.x > 0.0 &&
            reservoir.weights.y > 1.0e-8 &&
            reservoir.weights.z > 0.0)
        {
            float normalization = reservoir.weights.x /
                max(reservoir.weights.y * reservoir.weights.z, 1.0e-8);
            normalization = clamp(normalization, 0.0, 4.0);
            color += max(reservoir.radiance.rgb, vec3(0.0)) * normalization;
        }
    }
    current_lighting[index].color = vec4(color, 1.0);
}
)GLSL";

inline constexpr const char *temporal_filter = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) readonly buffer CurrentSurfaces { SurfaceData current_surfaces[]; };
layout(std430, binding = 1) readonly buffer PreviousSurfaces { SurfaceData previous_surfaces[]; };
layout(std430, binding = 2) readonly buffer CurrentLighting { LightingData current_lighting[]; };
layout(std430, binding = 3) readonly buffer PreviousLighting { LightingData previous_lighting[]; };
layout(std430, binding = 4) readonly buffer PreviousMoments { MomentsData previous_moments[]; };
layout(std430, binding = 5) writeonly buffer TemporalLighting { LightingData temporal_lighting[]; };
layout(std430, binding = 6) writeonly buffer CurrentMoments { MomentsData current_moments[]; };
uniform mat4 uPreviousViewProjection;
uniform vec3 uPreviousCameraPosition;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (!inBounds(pixel)) return;
    uint index = pixelIndex(pixel);
    SurfaceData current_surface = current_surfaces[index];
    vec3 current_color = max(current_lighting[index].color.rgb, vec3(0.0));
    float current_luminance = luminance(current_color);

    vec3 neighborhood_min = current_color;
    vec3 neighborhood_max = current_color;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 sample_pixel = pixel + ivec2(x, y);
            if (!inBounds(sample_pixel)) continue;
            vec3 sample_color = max(current_lighting[pixelIndex(sample_pixel)].color.rgb, vec3(0.0));
            neighborhood_min = min(neighborhood_min, sample_color);
            neighborhood_max = max(neighborhood_max, sample_color);
        }
    }

    vec3 output_color = current_color;
    float first_moment = current_luminance;
    float second_moment = current_luminance * current_luminance;
    float history_length = 1.0;

    if (validSurface(current_surface)) {
        vec4 clip = uPreviousViewProjection * vec4(current_surface.position_depth.xyz, 1.0);
        if (clip.w > 1.0e-6) {
            vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
            ivec2 previous_pixel = ivec2(floor(uv * vec2(uResolution)));
            if (inBounds(previous_pixel)) {
                uint previous_index = pixelIndex(previous_pixel);
                SurfaceData previous_surface = previous_surfaces[previous_index];
                if (historyCompatible(current_surface, previous_surface, uPreviousCameraPosition)) {
                    vec3 history_color = previous_lighting[previous_index].color.rgb;
                    history_color = clamp(history_color, neighborhood_min, neighborhood_max);
                    MomentsData history_moments = previous_moments[previous_index];
                    history_length = min(history_moments.value.w + 1.0, 32.0);
                    float alpha = max(1.0 / history_length, 0.06);
                    output_color = mix(history_color, current_color, alpha);
                    first_moment = mix(history_moments.value.x, current_luminance, alpha);
                    second_moment = mix(history_moments.value.y, current_luminance * current_luminance, alpha);
                }
            }
        }
    }

    float variance = max(second_moment - first_moment * first_moment, 0.0);
    temporal_lighting[index].color = vec4(max(output_color, vec3(0.0)), 1.0);
    current_moments[index].value = vec4(first_moment, second_moment, variance, history_length);
}
)GLSL";

inline constexpr const char *atrous = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) readonly buffer Surfaces { SurfaceData surfaces[]; };
layout(std430, binding = 1) readonly buffer InputLighting { LightingData input_lighting[]; };
layout(std430, binding = 2) readonly buffer Moments { MomentsData moments[]; };
layout(std430, binding = 3) writeonly buffer OutputLighting { LightingData output_lighting[]; };
uniform int uStep;

const float KERNEL[3] = float[3](1.0, 2.0, 1.0);

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (!inBounds(pixel)) return;
    uint index = pixelIndex(pixel);
    SurfaceData center_surface = surfaces[index];
    vec3 center_color = input_lighting[index].color.rgb;
    if (!validSurface(center_surface)) {
        output_lighting[index].color = vec4(center_color, 1.0);
        return;
    }

    float center_luminance = luminance(center_color);
    float variance = max(moments[index].value.z, 1.0e-6);
    float sigma_luminance = sqrt(variance) + 0.02;
    vec3 sum = vec3(0.0);
    float weight_sum = 0.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 sample_pixel = pixel + ivec2(x, y) * max(uStep, 1);
            if (!inBounds(sample_pixel)) continue;
            uint sample_index = pixelIndex(sample_pixel);
            SurfaceData sample_surface = surfaces[sample_index];
            if (!validSurface(sample_surface)) continue;

            vec3 sample_color = input_lighting[sample_index].color.rgb;
            float normal_weight = pow(max(dot(
                center_surface.normal_material.xyz,
                sample_surface.normal_material.xyz), 0.0), 32.0);
            float depth_scale = max(center_surface.position_depth.w, 1.0);
            float depth_weight = exp(-abs(
                sample_surface.position_depth.w - center_surface.position_depth.w) /
                (0.02 * depth_scale + 1.0e-4));
            float luminance_weight = exp(-abs(luminance(sample_color) - center_luminance) /
                (4.0 * sigma_luminance + 0.02));
            float kernel_weight = KERNEL[abs(x)] * KERNEL[abs(y)];
            float weight = kernel_weight * normal_weight * depth_weight * luminance_weight;
            sum += sample_color * weight;
            weight_sum += weight;
        }
    }

    vec3 filtered = weight_sum > 1.0e-6 ? sum / weight_sum : center_color;
    output_lighting[index].color = vec4(max(filtered, vec3(0.0)), 1.0);
}
)GLSL";

inline constexpr const char *copy_to_image = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) readonly buffer Lighting { LightingData lighting[]; };
layout(rgba16f, binding = 0) uniform writeonly image2D uOutput;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (!inBounds(pixel)) return;
    imageStore(uOutput, pixel, vec4(max(lighting[pixelIndex(pixel)].color.rgb, vec3(0.0)), 1.0));
}
)GLSL";

inline constexpr const char *present_vertex = R"GLSL(
#version 430 compatibility
out vec2 vUv;
void main()
{
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
    vUv = gl_Vertex.xy * 0.5 + 0.5;
}
)GLSL";

inline constexpr const char *present_fragment = R"GLSL(
#version 430 compatibility
uniform sampler2D uOutput;
uniform float uExposure;
in vec2 vUv;
layout(location = 0) out vec4 outColor;

vec3 acesApprox(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 linear_color = max(texture(uOutput, vUv).rgb, vec3(0.0)) * max(uExposure, 0.0);
    vec3 mapped = acesApprox(linear_color);
    outColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

} // namespace Renderer::SvgfShaders

#endif
