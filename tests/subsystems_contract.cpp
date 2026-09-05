#include "Sources/Camera.hpp"
#include "Sources/Ecs/Ecs.hpp"
#include "Sources/Models/Models.hpp"
#include "Sources/Renderer/Render.hpp"

#include <type_traits>

static_assert(std::is_default_constructible_v<Ecs::World>);
static_assert(std::is_default_constructible_v<Models::ModelPart>);
static_assert(std::is_default_constructible_v<Renderer::Rasterizer>);
static_assert(std::is_default_constructible_v<Camera::Controller>);

int main()
{
    Ecs::World world;
    Models::ModelPart part;
    Renderer::Rasterizer renderer;
    Camera::Controller camera;
    (void)world;
    (void)part;
    (void)renderer;
    (void)camera;
    return 0;
}
