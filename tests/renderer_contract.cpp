#include "Sources/Renderer/Render.hpp"
#include <type_traits>

static_assert(std::is_default_constructible_v<Renderer::Rasterizer>);
static_assert(Renderer::LightingDefaults::scene_ambient >= 0.5f);
static_assert(Renderer::LightingDefaults::scene_ambient < 1.0f);
static_assert(Renderer::LightingDefaults::direct_diffuse > 0.0f);

int main()
{
    Renderer::Rasterizer rasterizer;
    return rasterizer.initialized() ? 1 : 0;
}
