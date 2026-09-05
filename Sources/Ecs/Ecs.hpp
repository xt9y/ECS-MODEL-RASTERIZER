#ifndef RW_ENGINE_ECS_HPP
#define RW_ENGINE_ECS_HPP

#include <cstdint>
#include <optional>
#include <vector>

namespace Ecs {

using Entity = std::uint32_t;
constexpr Entity INVALID_ENTITY = UINT32_MAX;
constexpr std::uint32_t INVALID_ASSET_HANDLE = UINT32_MAX;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct TransformComponent {
    Vec3 position {0.0f, 0.0f, 0.0f};
    Vec3 rotation {0.0f, 0.0f, 0.0f};
    Vec3 scale {1.0f, 1.0f, 1.0f};
};

struct CameraComponent {
    float fov_degrees = 60.0f;
    float near_plane = 0.1f;
    bool active = true;
};

struct MeshComponent {
    std::uint32_t mesh = INVALID_ASSET_HANDLE;
    std::uint32_t material = INVALID_ASSET_HANDLE;
};

struct RenderableComponent {
    bool visible = true;
};

enum class LightType {
    Directional,
    Point,
    Spot,
};

struct LightComponent {
    LightType type = LightType::Directional;
    Vec3 color {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

class World {
public:
    Entity createEntity();

    TransformComponent& addTransform(Entity entity, const TransformComponent& component);
    CameraComponent& addCamera(Entity entity, const CameraComponent& component);
    MeshComponent& addMesh(Entity entity, const MeshComponent& component);
    RenderableComponent& addRenderable(Entity entity, const RenderableComponent& component);
    LightComponent& addLight(Entity entity, const LightComponent& component);

    TransformComponent *getTransform(Entity entity);
    const TransformComponent *getTransform(Entity entity) const;
    CameraComponent *getCamera(Entity entity);
    const CameraComponent *getCamera(Entity entity) const;
    MeshComponent *getMesh(Entity entity);
    const MeshComponent *getMesh(Entity entity) const;
    RenderableComponent *getRenderable(Entity entity);
    const RenderableComponent *getRenderable(Entity entity) const;
    LightComponent *getLight(Entity entity);
    const LightComponent *getLight(Entity entity) const;

    Entity activeCamera() const;
    const std::vector<Entity>& entities() const;

    std::uint64_t changeRevision() const { return change_revision_; }
    void markChanged() { touch(); }

private:
    void ensureCapacity(Entity entity);
    void touch() { ++change_revision_; }

    std::vector<Entity> entities_;
    std::vector<std::optional<TransformComponent>> transforms_;
    std::vector<std::optional<CameraComponent>> cameras_;
    std::vector<std::optional<MeshComponent>> meshes_;
    std::vector<std::optional<RenderableComponent>> renderables_;
    std::vector<std::optional<LightComponent>> lights_;

    std::uint64_t change_revision_ = 1u;
    mutable std::uint64_t active_camera_revision_ = 0u;
    mutable Entity active_camera_cache_ = INVALID_ENTITY;
};

} // namespace Ecs

#endif
