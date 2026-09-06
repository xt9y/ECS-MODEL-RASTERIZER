#ifndef RW_ENGINE_RENDERER_PATHTRACER_WAVEFRONT_RUNTIME_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_WAVEFRONT_RUNTIME_SHADERS_HPP

namespace Renderer::WavefrontRuntimeShaders {

// Inserted after WavefrontShaders::common and before stages that use uFrameIndex.
// The common block's uint uniform becomes dead; stage references are redirected
// to this int-backed alias, which works with lwcgl v2.9.3's stable GL20 table.
inline constexpr const char *frame_index_compat = R"GLSL(
uniform int uFrameIndexInt;
#define uFrameIndex uint(max(uFrameIndexInt, 0))
)GLSL";

inline constexpr const char *bounce_shade = R"GLSL(
layout(local_size_x = 64) in;
layout(std430, binding = 4) readonly buffer SecondarySurfaces { SurfaceData secondary_surfaces[]; };
layout(std430, binding = 5) readonly buffer PrimarySurfaces { SurfaceData primary_surfaces[]; };
layout(std430, binding = 6) buffer InitialReservoirs { ReservoirData initial_reservoirs[]; };
layout(std430, binding = 7) buffer RadianceCache { CacheEntry cache_entries[]; };
uniform int uPixelCount;
uniform int uCacheSizeInt;

uint cacheSize()
{
    return uint(max(uCacheSizeInt, 0));
}

uint cacheKey(vec3 position, vec3 normal)
{
    ivec3 cell = ivec3(floor(position * 0.5));
    uint normal_bits =
        (normal.x >= 0.0 ? 1u : 0u) |
        (normal.y >= 0.0 ? 2u : 0u) |
        (normal.z >= 0.0 ? 4u : 0u);
    return hashUint(
        uint(cell.x) * 73856093u ^
        uint(cell.y) * 19349663u ^
        uint(cell.z) * 83492791u ^
        normal_bits * 2654435761u
    ) | 1u;
}

vec3 cacheQuery(vec3 position, vec3 normal)
{
    uint size = cacheSize();
    if (size == 0u) return vec3(0.0);
    uint key = cacheKey(position, normal);
    uint start = key % size;
    for (uint probe = 0u; probe < 4u; ++probe) {
        uint index = (start + probe) % size;
        CacheEntry entry = cache_entries[index];
        if (entry.header.x == key && entry.header.y > 0u) {
            return vec3(entry.radiance.xyz) / (1024.0 * float(entry.header.y));
        }
        if (entry.header.x == 0u) break;
    }
    return vec3(0.0);
}

void cacheUpdate(vec3 position, vec3 normal, vec3 radiance)
{
    uint size = cacheSize();
    if (size == 0u) return;
    uint key = cacheKey(position, normal);
    uint start = key % size;
    uvec3 fixed_point = uvec3(clamp(radiance, vec3(0.0), vec3(64.0)) * 1024.0 + 0.5);
    for (uint probe = 0u; probe < 4u; ++probe) {
        uint index = (start + probe) % size;
        uint existing = atomicCompSwap(cache_entries[index].header.x, 0u, key);
        if (existing == 0u || existing == key) {
            uint count = atomicAdd(cache_entries[index].header.y, 1u);
            if (count < 65535u) {
                atomicAdd(cache_entries[index].radiance.x, fixed_point.x);
                atomicAdd(cache_entries[index].radiance.y, fixed_point.y);
                atomicAdd(cache_entries[index].radiance.z, fixed_point.z);
            }
            return;
        }
    }
}

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(max(uPixelCount, 0))) return;
    SurfaceData secondary = secondary_surfaces[index];
    if (secondary.position_depth.w <= 0.0) return;

    uint pixel = uint(max(secondary.uv_source.z, 0.0) + 0.5);
    if (pixel >= uint(max(uPixelCount, 0))) return;

    uint secondary_material = uint(max(secondary.normal_material.w, 0.0) + 0.5);
    vec3 secondary_albedo = materialAlbedo(secondary_material, secondary.uv_source.xy);
    vec3 direct = pointLight(
        secondary.position_depth.xyz,
        secondary.normal_material.xyz,
        secondary_albedo
    );
    vec3 cached = cacheQuery(secondary.position_depth.xyz, secondary.normal_material.xyz);
    vec3 outgoing = direct + cached;
    cacheUpdate(secondary.position_depth.xyz, secondary.normal_material.xyz, direct + cached * 0.5);

    SurfaceData primary = primary_surfaces[pixel];
    uint primary_material = uint(max(primary.normal_material.w, 0.0) + 0.5);
    vec3 contribution = materialAlbedo(primary_material, primary.uv_source.xy) * outgoing;
    float target = max(dot(contribution, vec3(0.2126, 0.7152, 0.0722)), 1.0e-6);

    ReservoirData reservoir;
    reservoir.sample_position_m = vec4(secondary.position_depth.xyz, 1.0);
    reservoir.radiance_weight = vec4(contribution, target);
    initial_reservoirs[pixel] = reservoir;
}
)GLSL";

} // namespace Renderer::WavefrontRuntimeShaders

#endif
