#ifndef RW_ENGINE_RENDER_HPP
#define RW_ENGINE_RENDER_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Gi/Gi.hpp"

#include <cstdint>
#include <unordered_map>

namespace Renderer {

struct LightingDefaults {
    static constexpr float scene_ambient = 0.0f;
    static constexpr float direct_diffuse = 1.0f;
};

class Rasterizer {
public:
    bool init();
    void resize(int width, int height);
    void render(const Ecs::World& world);
    void shutdown();

    bool initialized() const { return initialized_; }

    GiSettings& giSettings() { return gi_.settings(); }
    const GiSettings& giSettings() const { return gi_.settings(); }

    void setGiEnabled(bool enabled) { gi_.setEnabled(enabled); }
    bool giEnabled() const { return gi_.enabled(); }

private:
    unsigned int textureFor(std::uint32_t handle);

    bool initialized_ = false;
    int width_ = 1;
    int height_ = 1;

    GI gi_{};
    std::unordered_map<std::uint32_t, unsigned int> textures_;
};

} // namespace Renderer

#endif
