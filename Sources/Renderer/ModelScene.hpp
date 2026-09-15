#ifndef HORSE_RENDERER_MODEL_SCENE_HPP
#define HORSE_RENDERER_MODEL_SCENE_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Models.hpp"
#include "Models/Runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Renderer::ModelScene {

struct PartBinding {
    std::uint32_t part = Models::INVALID_INDEX;
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Models::MeshHandle mesh = Models::INVALID_MESH;
    Models::MaterialHandle animated_material = Models::INVALID_MATERIAL;
    bool dynamic_mesh = false;
};

struct NodeBinding {
    std::uint32_t node = Models::INVALID_INDEX;
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    std::vector<PartBinding> parts;
};

struct Instance {
    Models::ModelHandle model = Models::INVALID_MODEL;
    std::uint32_t scene = Models::INVALID_INDEX;
    std::uint32_t variant = Models::INVALID_INDEX;
    Ecs::Entity pose_entity = Ecs::INVALID_ENTITY;
    std::vector<NodeBinding> nodes;
    std::vector<PartBinding> loose_parts;
};

struct Options {
    std::uint32_t scene = Models::INVALID_INDEX;
    std::uint32_t variant = Models::INVALID_INDEX;
    Ecs::Entity parent = Ecs::INVALID_ENTITY;
    bool instantiate_cameras = true;
    bool instantiate_lights = true;
    bool activate_first_camera = false;
};

struct AnimationTarget {
    Instance *instance = nullptr;
    Models::Runtime::RetargetOptions options;
    Models::Runtime::Retarget binding;
    Models::Runtime::Pose pose;
};

struct AnimationAttachment {
    std::size_t target = Models::INVALID_INDEX;
    std::string source_node;
    std::string target_node;
    std::uint32_t source = Models::INVALID_INDEX;
    std::uint32_t target_basis = Models::INVALID_INDEX;
};

struct Animation {
    Models::ModelHandle model = Models::INVALID_MODEL;
    std::uint32_t clip = Models::INVALID_INDEX;
    float time = 0.0f;
    bool loop = true;
    bool active = false;
    std::vector<AnimationTarget> targets;
    std::vector<AnimationAttachment> attachments;
};

bool instantiate(
    Ecs::World& world,
    Models::ModelHandle model,
    Instance *output,
    const Options& options = {},
    std::string *error = nullptr
);

bool applyPose(
    Ecs::World& world,
    Instance& instance,
    const Models::Runtime::Pose& pose,
    std::string *error = nullptr
);

bool setVariant(
    Ecs::World& world,
    Instance& instance,
    std::uint32_t variant,
    std::string *error = nullptr
);

std::size_t bind(
    Animation& animation,
    Instance& instance,
    const Models::Runtime::RetargetOptions& options = {}
);

bool attach(
    Animation& animation,
    std::size_t target,
    std::string_view source_node,
    std::string_view target_node
);

bool play(
    Animation& animation,
    Models::ModelHandle model,
    std::size_t clip,
    bool loop = true,
    std::string *error = nullptr
);

bool update(
    Ecs::World& world,
    Animation& animation,
    float delta_seconds,
    std::string *error = nullptr
);

bool playing(const Animation& animation);

void destroy(Ecs::World& world, Instance& instance);

} // namespace Renderer::ModelScene

#endif
