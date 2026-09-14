#ifndef HORSE_RENDERER_DYNAMIC_GEOMETRY_HPP
#define HORSE_RENDERER_DYNAMIC_GEOMETRY_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Models.hpp"
#include "Models/Runtime.hpp"

#include <cstdint>

namespace Renderer {

struct ModelPoseComponent {
    Models::Runtime::Pose pose;
    std::uint64_t revision = 1u;
};

struct ModelDeformComponent {
    Models::ModelHandle model = Models::INVALID_MODEL;
    std::uint32_t part = Models::INVALID_INDEX;
    Ecs::Entity pose_entity = Ecs::INVALID_ENTITY;
};

} // namespace Renderer

#endif
