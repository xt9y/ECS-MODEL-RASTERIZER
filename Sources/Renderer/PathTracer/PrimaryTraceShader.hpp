#ifndef RW_ENGINE_RENDERER_PATHTRACER_PRIMARY_TRACE_SHADER_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_PRIMARY_TRACE_SHADER_HPP

namespace Renderer::PrimaryTraceShader {

inline constexpr const char *source = R"GLSL(
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 4) writeonly buffer PrimarySurfaces { SurfaceData primary_surfaces[]; };
layout(std430, binding = 5) buffer QueueControlBlock {
    uvec4 counters;
    uvec4 hit_dispatch;
    uvec4 bounce_dispatch;
    uvec4 bucket_count0;
    uvec4 bucket_count1;
    uvec4 bucket_offset0;
    uvec4 bucket_offset1;
    uvec4 bucket_cursor0;
    uvec4 bucket_cursor1;
    uint hit_queue[];
};
layout(std430, binding = 6) writeonly buffer InitialReservoirs { ReservoirData initial_reservoirs[]; };
uniform int uResolutionX;
uniform int uResolutionY;
uniform vec3 uCameraPosition;
uniform vec3 uCameraForward;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uTanHalfFov;
uniform float uAspect;

void main()
{
    ivec2 resolution = ivec2(uResolutionX, uResolutionY);
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, resolution))) return;
    uint index = uint(pixel.y * resolution.x + pixel.x);

    uint state = hashUint(index * 9781u ^ uFrameIndex * 6271u ^ 0x68bc21ebu);
    vec2 jitter = vec2(randomFloat(state), randomFloat(state)) - 0.5;
    vec2 uv = (vec2(pixel) + 0.5 + jitter) / vec2(resolution);
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 direction = normalize(
        uCameraForward +
        uCameraRight * (ndc.x * uAspect * uTanHalfFov) +
        uCameraUp * (ndc.y * uTanHalfFov)
    );

    Hit hit = traceClosest(uCameraPosition, direction, INF);
    SurfaceData surface;
    surface.position_depth = vec4(0.0, 0.0, 0.0, -1.0);
    surface.normal_material = vec4(0.0, 1.0, 0.0, 0.0);
    surface.uv_source = vec4(0.0);
    surface.direct = vec4(0.0);

    ReservoirData empty_reservoir;
    empty_reservoir.sample_position_m = vec4(0.0);
    empty_reservoir.radiance_weight = vec4(0.0);
    initial_reservoirs[index] = empty_reservoir;

    if (hit.found) {
        surface.position_depth = vec4(hit.position, hit.distance);
        surface.normal_material = vec4(hit.normal, float(hit.material));
        surface.uv_source = vec4(hit.uv, 0.0, 0.0);
        uint queue_index = atomicAdd(counters.x, 1u);
        uint pixel_count = uint(max(uResolutionX, 0) * max(uResolutionY, 0));
        if (queue_index < pixel_count) hit_queue[queue_index] = index;
    }
    primary_surfaces[index] = surface;
}
)GLSL";

} // namespace Renderer::PrimaryTraceShader

#endif
