#ifndef RW_ENGINE_CAMERA_HPP
#define RW_ENGINE_CAMERA_HPP

#include "Ecs/Ecs.hpp"

namespace Camera {

Ecs::Vec3 flightDirection(float yaw_degrees, float pitch_degrees);
Ecs::Vec3 strafeDirection(float yaw_degrees);

class Controller {
public:
    void update(Ecs::World& world, float delta_seconds);

private:
    bool mouse_initialized_ = false;
};

} // namespace Camera

#endif
