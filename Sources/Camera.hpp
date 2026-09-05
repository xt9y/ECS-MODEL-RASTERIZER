#ifndef RW_ENGINE_CAMERA_HPP
#define RW_ENGINE_CAMERA_HPP

#include "Ecs/Ecs.hpp"

namespace RW {

class CameraController {
public:
    void update(Ecs::World& world, float delta_seconds);

private:
    bool mouse_initialized_ = false;
};

} // namespace RW

#endif
