#ifndef RW_ENGINE_RENDER_HPP
#define RW_ENGINE_RENDER_HPP

#include "Ecs/Ecs.hpp"

namespace RW::Renderer {

class Rasterizer {
public:
    bool init();
    void resize(int width, int height);
    void render(const Ecs::World& world);
    void shutdown();

    bool initialized() const { return initialized_; }

private:
    bool initialized_ = false;
    int width_ = 1;
    int height_ = 1;
};

} // namespace RW::Renderer

#endif
