#include "Core.h"
#include <type_traits>

static_assert(std::is_default_constructible_v<RW::Engine>);
static_assert(std::is_default_constructible_v<RW::Ecs::World>);

int main()
{
    return 0;
}
