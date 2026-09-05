#include "Sources/Camera.hpp"

#include <cassert>
#include <cmath>

namespace {

constexpr float epsilon = 0.001f;

bool near(float value, float expected)
{
    return std::fabs(value - expected) < epsilon;
}

void expectDirection(const Ecs::Vec3& value, float x, float y, float z)
{
    assert(near(value.x, x));
    assert(near(value.y, y));
    assert(near(value.z, z));
}

} // namespace

int main()
{
    // Forward must follow the renderer's camera yaw convention through all
    // four cardinal directions. S is the exact negative of this vector.
    expectDirection(Camera::flightDirection(0.0f, 0.0f), 0.0f, 0.0f, -1.0f);
    expectDirection(Camera::flightDirection(90.0f, 0.0f), -1.0f, 0.0f, 0.0f);
    expectDirection(Camera::flightDirection(180.0f, 0.0f), 0.0f, 0.0f, 1.0f);
    expectDirection(Camera::flightDirection(270.0f, 0.0f), 1.0f, 0.0f, 0.0f);

    // Pitch is part of flight movement: looking up while moving forward
    // must gain height regardless of yaw.
    const Ecs::Vec3 up_forward = Camera::flightDirection(0.0f, 30.0f);
    assert(up_forward.y > 0.49f);
    assert(up_forward.z < -0.86f);

    const Ecs::Vec3 up_left = Camera::flightDirection(90.0f, 30.0f);
    assert(up_left.x < -0.86f);
    assert(up_left.y > 0.49f);

    // Right is camera-local right in every quadrant. A is its exact negative.
    expectDirection(Camera::strafeDirection(0.0f), 1.0f, 0.0f, 0.0f);
    expectDirection(Camera::strafeDirection(90.0f), 0.0f, 0.0f, -1.0f);
    expectDirection(Camera::strafeDirection(180.0f), -1.0f, 0.0f, 0.0f);
    expectDirection(Camera::strafeDirection(270.0f), 0.0f, 0.0f, 1.0f);
    return 0;
}
