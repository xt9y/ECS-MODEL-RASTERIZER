#ifndef RW_ENGINE_RENDERER_PATHTRACER_RESTIR_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_RESTIR_SHADERS_HPP

namespace Renderer::RestirShaders {

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

uniform ivec2 uResolution;
uniform uint uFrameIndex;

uint hashUint(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float randomFloat(inout uint state)
{
    state = hashUint(state);
    return float(state) * (1.0 / 4294967296.0);
}

bool validSurface(SurfaceData surface)
{
    return surface.position_depth.w > 0.0;
}

bool geometryCompatible(SurfaceData a, SurfaceData b)
{
    if (!validSurface(a) || !validSurface(b)) return false;
    float normal_similarity = dot(a.normal_material.xyz, b.normal_material.xyz);
    float depth_scale = max(max(a.position_depth.w, b.position_depth.w), 1.0);
    float position_error = length(a.position_depth.xyz - b.position_depth.xyz);
    return normal_similarity >= 0.85 && position_error <= 0.08 * depth_scale;
}

void mergeReservoir(inout ReservoirData result, ReservoirData candidate, float history_scale, inout uint rng)
{
    if (candidate.sample_position.w <= 0.0 || candidate.weights.x <= 0.0 || candidate.weights.z <= 0.0) return;
    float candidate_m = min(candidate.weights.z, 20.0);
    float source_m = max(candidate.weights.z, 1.0);
    float weight = candidate.weights.x * history_scale * (candidate_m / source_m);
    if (weight <= 0.0) return;

    float previous_sum = result.weights.x;
    float new_sum = previous_sum + weight;
    if (previous_sum <= 0.0 || randomFloat(rng) < weight / max(new_sum, 1.0e-8)) {
        result.sample_position = candidate.sample_position;
        result.sample_normal = candidate.sample_normal;
        result.radiance = candidate.radiance;
        result.weights.y = max(candidate.weights.y, 1.0e-6);
        result.weights.w = candidate.weights.w;
    }
    result.weights.x = new_sum;
    result.weights.z = min(result.weights.z + candidate_m, 20.0);
}
)GLSL";

inline constexpr const char *temporal_reuse = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) readonly buffer CurrentSurfaces { SurfaceData current_surfaces[]; };
layout(std430, binding = 1) readonly buffer PreviousSurfaces { SurfaceData previous_surfaces[]; };
layout(std430, binding = 2) readonly buffer InitialReservoirs { ReservoirData initial_reservoirs[]; };
layout(std430, binding = 3) readonly buffer PreviousReservoirs { ReservoirData previous_reservoirs[]; };
layout(std430, binding = 4) writeonly buffer TemporalReservoirs { ReservoirData temporal_reservoirs[]; };
uniform mat4 uPreviousViewProjection;
uniform vec3 uPreviousCameraPosition;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, uResolution))) return;
    uint index = uint(pixel.y * uResolution.x + pixel.x);
    SurfaceData current = current_surfaces[index];
    ReservoirData result = initial_reservoirs[index];
    if (!validSurface(current)) {
        temporal_reservoirs[index] = result;
        return;
    }

    uint rng = hashUint(index * 9781u ^ uFrameIndex * 6271u ^ 0x91e10da5u);
    vec4 clip = uPreviousViewProjection * vec4(current.position_depth.xyz, 1.0);
    if (clip.w > 1.0e-6) {
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        ivec2 previous_pixel = ivec2(floor(uv * vec2(uResolution)));
        if (
            all(greaterThanEqual(previous_pixel, ivec2(0))) &&
            all(lessThan(previous_pixel, uResolution)))
        {
            uint previous_index = uint(previous_pixel.y * uResolution.x + previous_pixel.x);
            SurfaceData previous = previous_surfaces[previous_index];
            float expected_previous_depth = length(current.position_depth.xyz - uPreviousCameraPosition);
            float relative_depth_error = abs(previous.position_depth.w - expected_previous_depth) /
                max(max(previous.position_depth.w, expected_previous_depth), 1.0e-3);
            float world_error = length(previous.position_depth.xyz - current.position_depth.xyz);
            float tolerance = max(0.03, expected_previous_depth * 0.03);
            bool valid_history =
                validSurface(previous) &&
                dot(previous.normal_material.xyz, current.normal_material.xyz) >= 0.85 &&
                relative_depth_error <= 0.05 &&
                world_error <= tolerance;
            if (valid_history) {
                ReservoirData history = previous_reservoirs[previous_index];
                history.weights.w = min(history.weights.w + 1.0, 32.0);
                mergeReservoir(result, history, 1.0, rng);
            }
        }
    }

    if (result.weights.x <= 0.0) result.weights = vec4(0.0);
    temporal_reservoirs[index] = result;
}
)GLSL";

inline constexpr const char *spatial_reuse = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) readonly buffer CurrentSurfaces { SurfaceData current_surfaces[]; };
layout(std430, binding = 1) readonly buffer TemporalReservoirs { ReservoirData temporal_reservoirs[]; };
layout(std430, binding = 2) writeonly buffer SpatialReservoirs { ReservoirData spatial_reservoirs[]; };

const ivec2 OFFSETS[8] = ivec2[8](
    ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1),
    ivec2(2, 1), ivec2(-2, -1), ivec2(1, -2), ivec2(-1, 2)
);

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, uResolution))) return;
    uint index = uint(pixel.y * uResolution.x + pixel.x);
    SurfaceData current = current_surfaces[index];
    ReservoirData result = temporal_reservoirs[index];
    if (!validSurface(current)) {
        spatial_reservoirs[index] = result;
        return;
    }

    uint rng = hashUint(index * 1597u ^ uFrameIndex * 5171u ^ 0xb5297a4du);
    int rotation = int(uFrameIndex & 7u);
    for (int sample_index = 0; sample_index < 4; ++sample_index) {
        ivec2 offset = OFFSETS[(rotation + sample_index * 2) & 7];
        ivec2 neighbor_pixel = pixel + offset;
        if (
            any(lessThan(neighbor_pixel, ivec2(0))) ||
            any(greaterThanEqual(neighbor_pixel, uResolution)))
        {
            continue;
        }
        uint neighbor_index = uint(neighbor_pixel.y * uResolution.x + neighbor_pixel.x);
        SurfaceData neighbor = current_surfaces[neighbor_index];
        if (!geometryCompatible(current, neighbor)) continue;
        ReservoirData candidate = temporal_reservoirs[neighbor_index];
        mergeReservoir(result, candidate, 0.5, rng);
    }

    spatial_reservoirs[index] = result;
}
)GLSL";

} // namespace Renderer::RestirShaders

#endif
