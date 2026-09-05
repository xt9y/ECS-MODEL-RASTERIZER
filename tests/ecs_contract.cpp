#include "Sources/Ecs/Ecs.hpp"

#include <cassert>
#include <type_traits>
#include <utility>

template <typename T, typename = void>
struct HasFarPlane : std::false_type {};

template <typename T>
struct HasFarPlane<T, std::void_t<decltype(std::declval<T&>().far_plane)>> : std::true_type {};

static_assert(!HasFarPlane<Ecs::CameraComponent>::value);

int main()
{
    Ecs::World world;
    const auto entity = world.createEntity();
    world.addTransform(entity, {{1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});
    world.addCamera(entity, {60.0f, 0.1f, true});

    assert(world.entities().size() == 1u);
    assert(world.getTransform(entity));
    assert(world.getTransform(entity)->position.x == 1.0f);
    assert(world.activeCamera() == entity);
    return 0;
}
