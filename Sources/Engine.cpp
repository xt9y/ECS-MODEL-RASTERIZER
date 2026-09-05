#include "Engine.hpp"

#include "Camera.hpp"
#include "Renderer/Render.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace RW {

struct Engine::Impl {
    Ecs::World world;
    Renderer::Rasterizer rasterizer;
    CameraController camera;
    EngineConfig config;
    bool opened = false;
    bool running = false;
    int framebuffer_width = 1;
    int framebuffer_height = 1;
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() { close(); }
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

bool Engine::open(const EngineConfig& config, std::string *error)
{
    if (error) error->clear();
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->opened) return true;

    impl_->config = config;
    impl_->config.width = std::max(config.width, 1);
    impl_->config.height = std::max(config.height, 1);

    lwcglInstallFastRuntime();

    DisplayMode mode = DisplayMode(impl_->config.width, impl_->config.height);
    if (Display.setDisplayMode(&mode) != 0 || Display.create() != 0) {
        if (error) {
            const char *message = lwcglGetLastError();
            *error = message ? message : "failed to create display";
        }
        if (Display.isCreated()) Display.destroy();
        return false;
    }

    // From this point close() owns cleanup, including partial initialization.
    impl_->opened = true;

    Display.setTitle(impl_->config.title ? impl_->config.title : "RW-Engine");
    Display.setResizable(LWCGL_TRUE);
    Display.setVSyncEnabled(impl_->config.vsync ? LWCGL_TRUE : LWCGL_FALSE);

    if (Keyboard.create() != 0 || Mouse.create() != 0) {
        if (error) {
            const char *message = lwcglGetLastError();
            *error = message ? message : "failed to initialize input";
        }
        close();
        return false;
    }

    if (!impl_->rasterizer.init()) {
        if (error) *error = "failed to initialize rasterizer";
        close();
        return false;
    }

    impl_->framebuffer_width = std::max(Display.getWidth(), 1);
    impl_->framebuffer_height = std::max(Display.getHeight(), 1);
    impl_->rasterizer.resize(impl_->framebuffer_width, impl_->framebuffer_height);

    if (impl_->world.activeCamera() == Ecs::INVALID_ENTITY) {
        const Ecs::Entity camera = impl_->world.createEntity();
        impl_->world.addTransform(camera, {
            {0.0f, 1.5f, 5.0f},
            {0.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 1.0f},
        });
        impl_->world.addCamera(camera, {60.0f, 0.1f, 2000.0f, true});
    }

    return true;
}

void Engine::close()
{
    if (!impl_ || !impl_->opened) return;

    impl_->running = false;
    impl_->rasterizer.shutdown();
    if (Mouse.isCreated()) Mouse.destroy();
    if (Keyboard.isCreated()) Keyboard.destroy();
    if (Display.isCreated()) Display.destroy();
    impl_->opened = false;
}

int Engine::run()
{
    if (!impl_ || !impl_->opened) return 2;

    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();
    impl_->running = true;

    while (impl_->running && !Display.isCloseRequested()) {
        Display.processMessages();
        if (Keyboard.isKeyDown(Keyboard.KEY_ESCAPE)) break;

        const auto now = Clock::now();
        const float delta_seconds = std::chrono::duration<float>(now - previous).count();
        previous = now;

        impl_->camera.update(impl_->world, std::min(delta_seconds, 0.1f));

        const int width = std::max(Display.getWidth(), 1);
        const int height = std::max(Display.getHeight(), 1);
        if (width != impl_->framebuffer_width || height != impl_->framebuffer_height) {
            impl_->framebuffer_width = width;
            impl_->framebuffer_height = height;
            impl_->rasterizer.resize(width, height);
        }

        impl_->rasterizer.render(impl_->world);
        Display.updateNoMessages();
    }

    impl_->running = false;
    return 0;
}

void Engine::stop()
{
    if (impl_) impl_->running = false;
}

Models::ModelHandle Engine::loadModel(
    const std::string& path,
    const Models::SpawnOptions& options,
    std::string *error)
{
    if (!impl_) {
        if (error) *error = "engine is not initialized";
        return Models::INVALID_MODEL;
    }

    const Models::ModelHandle model = Models::load(path, error);
    if (model == Models::INVALID_MODEL) return model;
    if (Models::spawn(impl_->world, model, options, error).empty()) return Models::INVALID_MODEL;
    return model;
}

Ecs::World& Engine::world() { return impl_->world; }
const Ecs::World& Engine::world() const { return impl_->world; }
bool Engine::isOpen() const { return impl_ && impl_->opened; }

} // namespace RW
