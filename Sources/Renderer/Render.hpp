#ifndef RW_ENGINE_RENDER_HPP
#define RW_ENGINE_RENDER_HPP

#include "Ecs/Ecs.hpp"

#include <cstdint>
#include <unordered_map>

namespace Renderer {

struct LightingDefaults {
    static constexpr float scene_ambient = 0.65f;
    static constexpr float direct_diffuse = 0.75f;
};

class Rasterizer {
public:
    bool init();
    void resize(int width, int height);
    void render(const Ecs::World& world);
    void shutdown();

    bool initialized() const { return initialized_; }

private:
    unsigned int textureFor(std::uint32_t handle);

    bool initialized_ = false;
    int width_ = 1;
    int height_ = 1;
    std::unordered_map<std::uint32_t, unsigned int> textures_;
};

} // namespace Renderer

#endif
