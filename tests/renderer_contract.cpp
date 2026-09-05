#include "Sources/Renderer/Render.hpp"
#include <type_traits>

static_assert(std::is_default_constructible_v<Renderer::Rasterizer>);

int main()
{
    Renderer::Rasterizer rasterizer;
    return rasterizer.initialized() ? 1 : 0;
}
