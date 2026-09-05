#include "Camera.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cmath>

namespace Camera {

void Controller::update(Ecs::World& world, float delta_seconds)
{
    const Ecs::Entity camera_entity = world.activeCamera();
    if (camera_entity == Ecs::INVALID_ENTITY) return;

    Ecs::TransformComponent *transform = world.getTransform(camera_entity);
    if (!transform) return;

    if (!mouse_initialized_) {
        Mouse.setGrabbed(LWCGL_TRUE);
        Mouse.getDX();
        Mouse.getDY();
        mouse_initialized_ = true;
    }

    constexpr float mouse_sensitivity = 0.12f;
    transform->rotation.y -= static_cast<float>(Mouse.getDX()) * mouse_sensitivity;
    transform->rotation.x += static_cast<float>(Mouse.getDY()) * mouse_sensitivity;
    transform->rotation.x = std::clamp(transform->rotation.x, -89.0f, 89.0f);

    constexpr float pi = 3.14159265358979323846f;
    const float yaw = transform->rotation.y * (pi / 180.0f);
    const float pitch = transform->rotation.x * (pi / 180.0f);
    const float cos_pitch = std::cos(pitch);

    const Ecs::Vec3 forward {
        std::sin(yaw) * cos_pitch,
        std::sin(pitch),
        -std::cos(yaw) * cos_pitch,
    };
    const Ecs::Vec3 right {std::cos(yaw), 0.0f, std::sin(yaw)};

    float speed = 4.0f * delta_seconds;
    if (Keyboard.isKeyDown(Keyboard.KEY_LSHIFT)) speed *= 3.0f;

    auto move = [&](const Ecs::Vec3& direction, float scale) {
        transform->position.x += direction.x * scale;
        transform->position.y += direction.y * scale;
        transform->position.z += direction.z * scale;
    };

    if (Keyboard.isKeyDown(Keyboard.KEY_W)) move(forward, -speed);
    if (Keyboard.isKeyDown(Keyboard.KEY_S)) move(forward, speed);
    if (Keyboard.isKeyDown(Keyboard.KEY_D)) move(right, -speed);
    if (Keyboard.isKeyDown(Keyboard.KEY_A)) move(right, speed);

    world.markChanged();
}

} // namespace Camera
