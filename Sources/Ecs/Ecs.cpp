#include "Ecs/Ecs.hpp"

namespace Ecs {

Entity World::createEntity()
{
    const Entity entity = static_cast<Entity>(entities_.size());
    entities_.push_back(entity);
    touch();
    return entity;
}

} // namespace Ecs
