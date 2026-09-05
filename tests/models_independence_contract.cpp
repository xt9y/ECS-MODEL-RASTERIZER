#include "Sources/Models/Models.hpp"

#include <type_traits>

static_assert(std::is_default_constructible_v<Models::Vec2>);
static_assert(std::is_default_constructible_v<Models::Vec3>);
static_assert(std::is_default_constructible_v<Models::ModelPart>);

int main()
{
    Models::ModelPart part;
    Models::Vertex vertex;
    (void)part;
    (void)vertex;
    return 0;
}
