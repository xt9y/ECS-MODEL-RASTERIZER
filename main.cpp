#include "Sources/Camera.hpp"
#include "Sources/Ecs/Ecs.hpp"
#include "Sources/Models/Models.hpp"
#include "Sources/Renderer/Render.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    constexpr int initial_width = 1280;
    constexpr int initial_height = 720;

    lwcglInstallFastRuntime();

    DisplayMode mode(initial_width, initial_height);
    if (Display.setDisplayMode(&mode) != 0 || Display.create() != 0) {
        const char *message = lwcglGetLastError();
        std::fprintf(stderr, "RW-Engine: %s\n", message ? message : "failed to create display");
        if (Display.isCreated()) Display.destroy();
        return 2;
    }

    Display.setTitle("RW-Engine");
    Display.setResizable(LWCGL_TRUE);
    Display.setVSyncEnabled(LWCGL_TRUE);

    if (Keyboard.create() != 0 || Mouse.create() != 0) {
        const char *message = lwcglGetLastError();
        std::fprintf(stderr, "RW-Engine: %s\n", message ? message : "failed to initialize input");
        if (Mouse.isCreated()) Mouse.destroy();
        if (Keyboard.isCreated()) Keyboard.destroy();
        Display.destroy();
        return 2;
    }

    Renderer::Rasterizer renderer;
    if (!renderer.init()) {
        std::fprintf(stderr, "RW-Engine: failed to initialize rasterizer\n");
        Mouse.destroy();
        Keyboard.destroy();
        Display.destroy();
        return 2;
    }

    int framebuffer_width = std::max(Display.getWidth(), 1);
    int framebuffer_height = std::max(Display.getHeight(), 1);
    renderer.resize(framebuffer_width, framebuffer_height);

    Ecs::World world;
    Camera::Controller camera_controller;

    const Ecs::Entity camera = world.createEntity();
    world.addTransform(camera, {
        {0.0f, 1.5f, 5.0f},
        {0.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f},
    });
    world.addCamera(camera, {60.0f, 0.1f, 2000.0f, true});

    const char *model_path = argc > 1 ? argv[1] : "Assets/default.obj";
    std::string error;
    const Models::ModelHandle model = Models::load(model_path, &error);
    if (model == Models::INVALID_MODEL || Models::spawn(world, model, {}, &error).empty()) {
        std::fprintf(stderr, "RW-Engine: %s\n", error.empty() ? "failed to spawn model" : error.c_str());
        renderer.shutdown();
        Models::clearCache();
        Mouse.destroy();
        Keyboard.destroy();
        Display.destroy();
        return 3;
    }

    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();

    while (!Display.isCloseRequested()) {
        Display.processMessages();
        if (Keyboard.isKeyDown(Keyboard.KEY_ESCAPE)) break;

        const auto now = Clock::now();
        const float delta_seconds = std::chrono::duration<float>(now - previous).count();
        previous = now;

        camera_controller.update(world, std::min(delta_seconds, 0.1f));

        const int width = std::max(Display.getWidth(), 1);
        const int height = std::max(Display.getHeight(), 1);
        if (width != framebuffer_width || height != framebuffer_height) {
            framebuffer_width = width;
            framebuffer_height = height;
            renderer.resize(width, height);
        }

        renderer.render(world);
        Display.updateNoMessages();
    }

    renderer.shutdown();
    Models::clearCache();
    Mouse.destroy();
    Keyboard.destroy();
    Display.destroy();
    return 0;
}
