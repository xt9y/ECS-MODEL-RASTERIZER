#ifndef RW_ENGINE_CAMERA_HPP
#define RW_ENGINE_CAMERA_HPP

#include "Ecs/Ecs.hpp"

namespace Camera {

class Controller {
public:
    void update(Ecs::World& world, float delta_seconds);

private:
    bool mouse_initialized_ = false;
};

} // namespace Camera

#endif
