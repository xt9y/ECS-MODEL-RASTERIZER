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

TransformComponent& World::addTransform(Entity entity, const TransformComponent& component)
{
    ensureCapacity(entity);
    transforms_[entity] = component;
    touch();
    return *transforms_[entity];
}

CameraComponent& World::addCamera(Entity entity, const CameraComponent& component)
{
    ensureCapacity(entity);
    cameras_[entity] = component;
    touch();
    return *cameras_[entity];
}

MeshComponent& World::addMesh(Entity entity, const MeshComponent& component)
{
    ensureCapacity(entity);
    meshes_[entity] = component;
    touch();
    return *meshes_[entity];
}

RenderableComponent& World::addRenderable(Entity entity, const RenderableComponent& component)
{
    ensureCapacity(entity);
    renderables_[entity] = component;
    touch();
    return *renderables_[entity];
}

LightComponent& World::addLight(Entity entity, const LightComponent& component)
{
    ensureCapacity(entity);
    lights_[entity] = component;
    touch();
    return *lights_[entity];
}

#define RW_ECS_GETTER(Name, Member, Type) \
Type *World::get##Name(Entity entity) { \
    if (entity >= Member.size() || !Member[entity]) return nullptr; \
    return &*Member[entity]; \
} \
const Type *World::get##Name(Entity entity) const { \
    if (entity >= Member.size() || !Member[entity]) return nullptr; \
    return &*Member[entity]; \
}

RW_ECS_GETTER(Transform, transforms_, TransformComponent)
RW_ECS_GETTER(Camera, cameras_, CameraComponent)
RW_ECS_GETTER(Mesh, meshes_, MeshComponent)
RW_ECS_GETTER(Renderable, renderables_, RenderableComponent)
RW_ECS_GETTER(Light, lights_, LightComponent)

#undef RW_ECS_GETTER

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
