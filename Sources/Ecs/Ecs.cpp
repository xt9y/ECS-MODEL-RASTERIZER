#include "Ecs/Ecs.hpp"

namespace Ecs {

void World::ensureCapacity(Entity entity)
{
    const std::size_t size = static_cast<std::size_t>(entity) + 1u;
    if (transforms_.size() < size) transforms_.resize(size);
    if (cameras_.size() < size) cameras_.resize(size);
    if (meshes_.size() < size) meshes_.resize(size);
    if (renderables_.size() < size) renderables_.resize(size);
    if (lights_.size() < size) lights_.resize(size);
}

Entity World::createEntity()
{
    const Entity entity = static_cast<Entity>(entities_.size());
    entities_.push_back(entity);
    ensureCapacity(entity);
    touch();
    return entity;
}

#define ECS_ADDER(Name, Member, Type) \
    Type& World::add##Name(Entity entity, const Type& component) { \
        ensureCapacity(entity); \
        Member[entity] = component; \
        touch(); \
        return *Member[entity]; \
    }

#define ECS_GETTER(Name, Member, Type) \
    Type *World::get##Name(Entity entity) { \
        if (entity >= Member.size() || !Member[entity]) return nullptr; \
        return &*Member[entity]; \
    } \
    const Type *World::get##Name(Entity entity) const { \
        if (entity >= Member.size() || !Member[entity]) return nullptr; \
        return &*Member[entity]; \
    }

ECS_ADDER(Transform, transforms_, TransformComponent)
ECS_ADDER(Camera, cameras_, CameraComponent)
ECS_ADDER(Mesh, meshes_, MeshComponent)
ECS_ADDER(Renderable, renderables_, RenderableComponent)
ECS_ADDER(Light, lights_, LightComponent)

ECS_GETTER(Transform, transforms_, TransformComponent)
ECS_GETTER(Camera, cameras_, CameraComponent)
ECS_GETTER(Mesh, meshes_, MeshComponent)
ECS_GETTER(Renderable, renderables_, RenderableComponent)
ECS_GETTER(Light, lights_, LightComponent)

#undef ECS_ADDER
#undef ECS_GETTER

Entity World::activeCamera() const
{
    if (active_camera_revision_ == change_revision_) return active_camera_cache_;

    active_camera_cache_ = INVALID_ENTITY;
    for (const Entity entity : entities_) {
        const CameraComponent *camera = getCamera(entity);
        if (camera && camera->active && getTransform(entity)) {
            active_camera_cache_ = entity;
            break;
        }
    }
    active_camera_revision_ = change_revision_;
    return active_camera_cache_;
}

const std::vector<Entity>& World::entities() const
{
    return entities_;
}

} // namespace Ecs
