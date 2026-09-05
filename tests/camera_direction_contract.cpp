#include "Sources/Camera.hpp"

#include <cassert>
#include <cmath>

int main()
{
    const Ecs::Vec3 forward = Camera::flightDirection(0.0f, 0.0f);
    assert(std::fabs(forward.x) < 0.001f);
    assert(std::fabs(forward.y) < 0.001f);
    assert(forward.z < -0.999f);

    const Ecs::Vec3 right_turn = Camera::flightDirection(90.0f, 0.0f);
    assert(right_turn.x > 0.999f);
    assert(std::fabs(right_turn.z) < 0.001f);

    const Ecs::Vec3 up = Camera::flightDirection(0.0f, 30.0f);
    assert(up.y > 0.49f);
    assert(up.z < -0.86f);
    return 0;
}
