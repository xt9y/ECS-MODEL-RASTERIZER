#ifndef RW_ENGINE_RENDERER_PATHTRACER_RADIANCE_CACHE_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_RADIANCE_CACHE_SHADERS_HPP

namespace Renderer::RadianceCacheShaders {

inline constexpr const char *clear_cache = R"GLSL(
#version 430
layout(local_size_x = 64) in;

struct CacheEntry {
    uvec4 header;
    uvec4 radiance;
};

layout(std430, binding = 0) buffer RadianceCache { CacheEntry cache_entries[]; };
uniform uint uCacheSize;

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= uCacheSize) return;
    cache_entries[index].header = uvec4(0u);
    cache_entries[index].radiance = uvec4(0u);
}
)GLSL";

} // namespace Renderer::RadianceCacheShaders

#endif
