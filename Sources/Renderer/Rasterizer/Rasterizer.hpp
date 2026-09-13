#ifndef HORSE_RENDERER_RASTERIZER_HPP
#define HORSE_RENDERER_RASTERIZER_HPP

#include "Renderer/Components.hpp"
#include "Renderer/Renderer.hpp"

namespace Renderer {

struct RasterizerSettings {
    bool enabled = false;
    bool viewport_culling = false;
    Vec4 clear_color{};
};

class Rasterizer final : public IRenderer {
public:
    struct Impl;

    Rasterizer();
    ~Rasterizer() override;

    Rasterizer(const Rasterizer&) = delete;
    Rasterizer& operator=(const Rasterizer&) = delete;

    bool init() override;
    void resize(int width, int height) override;
    void shutdown() override;

    bool initialized() const override;
    bool enabled() const override;
    void setEnabled(bool enabled) override;

    void setViewportCulling(bool value);
    void setClearColor(Vec4 value);

    bool viewportCulling() const;
    Vec4 clearColor() const;

    RasterizerSettings& settings();
    const RasterizerSettings& settings() const;

protected:
    bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) override;
    bool compose(Internal::FrameOutput& output) override;
    void present(Internal::FrameOutput& output) override;

private:
    Impl* impl_;
};

} // namespace Renderer

#endif
