#include "Sources/Ecs/Ecs.hpp"
#include <cassert>

int main()
{
    RW::Ecs::World world;
    const auto entity = world.createEntity();
    world.addTransform(entity, {{1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});
    world.addCamera(entity, {60.0f, 0.1f, 1000.0f, true});

    assert(world.entities().size() == 1u);
    assert(world.getTransform(entity));
    assert(world.getTransform(entity)->position.x == 1.0f);
    assert(world.activeCamera() == entity);
    return 0;
}
