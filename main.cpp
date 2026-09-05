
#include "Sources/Camera.hpp"
#include "Sources/Ecs/Ecs.hpp"
#include "Sources/Models/Models.hpp"
#include "Sources/Renderer/Render.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

class Example
{
public:
    static inline int run(int argc, char **argv)
    {
        constexpr int initial_width = 1280;
        constexpr int initial_height = 720;

        lwcglInstallFastRuntime();

        Display.setDisplayMode(new DisplayMode(initial_width, initial_height));
        Display.create();

        Display.setTitle("Test");

        Keyboard.create();
        Mouse.create();

        Renderer::Rasterizer renderer;
        renderer.init();

        int framebuffer_width  = std::max(Display.getWidth(),  1),
            framebuffer_height = std::max(Display.getHeight(), 1);

        renderer.resize(framebuffer_width, framebuffer_height);

        Ecs::World world;
        Camera::Controller camera_controller;

        const Ecs::Entity camera = world.createEntity();
        world.addTransform(camera, {
               .position = {.x = 0.0f, .y = 1.5f, .z = 5.0f},
               .rotation = {.x = 0.0f, .y = 0.0f, .z = 0.0f},
               .scale    = {.x = 1.0f, .y = 1.0f, .z = 1.0f},
        });

        world.addCamera(camera, {60.0f, 0.1f, true});

        std::string error;

        const Models::ModelHandle model = Models::load((argc > 1 ? argv[1] : "Assets/Sponza/sponza.obj"), &error);

        if (model == Models::INVALID_MODEL)
        {
            std::fprintf(stderr, "[LOG]: %s\n", error.c_str());

            renderer.shutdown();
            Models::clearCache();
            Mouse.destroy();
            Keyboard.destroy();
            Display.destroy();
            return 3;
        }

        if (Models::partCount(model) == 0u)
        {
            std::fprintf(stderr, "[LOG]: model has no renderable parts\n");

            renderer.shutdown();
            Models::clearCache();
            Mouse.destroy();
            Keyboard.destroy();
            Display.destroy();
            return 3;
        }

        for (std::size_t i = 0; i < Models::partCount(model); ++i)
        {
            if (!Models::part(model, i)) continue;

            const Ecs::Entity entity = world.createEntity();
            world.addTransform(entity, {});
            world.addMesh(entity, {Models::part(model, i)->mesh, Models::part(model, i)->material});
            world.addRenderable(entity, {true});
        }

        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();

        while (!Display.isCloseRequested())
        {
            Display.processMessages();
            if (Keyboard.isKeyDown(Keyboard.KEY_ESCAPE)) break;

            const auto now = Clock::now();
            const float delta_seconds = std::chrono::duration<float>(now - previous).count();
            previous = now;

            camera_controller.update(world, std::min(delta_seconds, 0.1f));

            const int width  = std::max(Display.getWidth(), 1);
            const int height = std::max(Display.getHeight(), 1);

            if ((width != framebuffer_width) || height != framebuffer_height)
            {
                framebuffer_height = height;
                framebuffer_width  = width;
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
};

int main(int argc, char **argv) {
    return Example::run(argc, argv);
}