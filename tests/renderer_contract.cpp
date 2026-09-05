#include "Sources/Renderer/Render.hpp"
#include <type_traits>

static_assert(std::is_default_constructible_v<RW::Renderer::Rasterizer>);

int main()
{
    RW::Renderer::Rasterizer rasterizer;
    return rasterizer.initialized() ? 1 : 0;
}
