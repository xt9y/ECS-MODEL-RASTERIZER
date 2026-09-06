#ifndef RW_ENGINE_CAMERA_HPP
#define RW_ENGINE_CAMERA_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"

namespace Camera {

struct CameraComponent {
    float fov_degrees = 60.0f;
    float near_plane = 0.1f;
    bool active = true;
};

Ecs::Entity activeCamera(const Ecs::World& world);
Renderer::Vec3 flightDirection(float yaw_degrees, float pitch_degrees);
Renderer::Vec3 strafeDirection(float yaw_degrees);

class Controller {
public:
    void update(Ecs::World& world, float delta_seconds);

private:
    bool mouse_initialized_ = false;
};

} // namespace Camera

#endif
