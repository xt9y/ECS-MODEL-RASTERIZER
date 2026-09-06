#ifndef RW_ENGINE_RENDERER_PATHTRACER_SCENE_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_SCENE_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Core/Texture.hpp"
#include "Renderer/PathTracer/PathTracerGpu.hpp"
#include "Renderer/PathTracer/WideBvh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::PathTracerScene {

struct DirtyRange {
    std::size_t first = 0u;
    std::size_t count = 0u;
};

struct SyncResult {
    bool ok = true;
    bool full_upload = false;
    bool instances_dirty = false;
    bool invalidate_history = false;
    bool clear_radiance_cache = false;
    std::vector<DirtyRange> node_ranges;
    std::vector<DirtyRange> triangle_ranges;
    std::string error;
};

class SceneCache {
public:
    SceneCache();
    ~SceneCache();

    SceneCache(const SceneCache&) = delete;
    SceneCache& operator=(const SceneCache&) = delete;

    SyncResult sync(const Ecs::World& world);
    void clear();

    const std::vector<PathTracerAccel::WideNode>& nodes() const;
    const std::vector<PathTracerAccel::Triangle>& triangles() const;
    const std::vector<PathTracerGpu::Instance>& instances() const;
    const std::vector<PathTracerGpu::Material>& materials() const;
    const std::array<Models::TextureHandle, 16>& textureHandles() const;

    std::size_t tlasNodeCount() const;
    std::uint64_t topologyRevision() const;

private:
    struct Impl;
    Impl *impl_;
};

} // namespace Renderer::PathTracerScene

#endif
