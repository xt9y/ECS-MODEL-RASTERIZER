#ifndef HORSE_MODELS_RUNTIME_HPP
#define HORSE_MODELS_RUNTIME_HPP

#include "Models/Models.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Models::Runtime {

struct NodeState {
    Vec3 translation {};
    Quat rotation {};
    Vec3 scale {1.0f, 1.0f, 1.0f};
    Mat4 local = identityMatrix();
    Mat4 world = identityMatrix();
    std::vector<float> weights;
    bool visible = true;
    bool selectable = true;
    bool hoverable = true;
};

struct Pose {
    ModelHandle model = INVALID_MODEL;
    std::uint32_t animation = INVALID_INDEX;
    float time_seconds = 0.0f;
    std::vector<NodeState> nodes;
    std::unordered_map<std::string, std::vector<float>> pointer_values;
};

enum class RetargetMode : std::uint8_t {
    LocalDelta,
    World,
};

struct RetargetOptions {
    RetargetMode mode = RetargetMode::LocalDelta;
    std::string source_root;
    std::string target_root;
};

struct Retarget {
    ModelHandle source = INVALID_MODEL;
    ModelHandle target = INVALID_MODEL;
    RetargetOptions options;
    Pose source_bind;
    std::vector<std::uint32_t> source_nodes;
    std::uint32_t source_root = INVALID_INDEX;
    std::uint32_t target_root = INVALID_INDEX;
};

struct DeformedPart {
    PrimitiveMode primitive_mode = PrimitiveMode::Triangles;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Bounds bounds {};
    Mat4 node_world = identityMatrix();
    MaterialHandle material = INVALID_MATERIAL;
};

bool reset(ModelHandle model, Pose *pose, std::string *error = nullptr);

bool sample(
    ModelHandle model,
    std::size_t animation_index,
    float time_seconds,
    bool loop,
    Pose *pose,
    std::string *error = nullptr
);

bool bindRetarget(
    ModelHandle source,
    ModelHandle target,
    Retarget *binding,
    const RetargetOptions& options = {},
    std::string *error = nullptr
);

bool retarget(
    const Retarget& binding,
    const Pose& source,
    Pose *target,
    std::string *error = nullptr
);

bool deformPart(
    ModelHandle model,
    std::size_t part_index,
    const Pose& pose,
    DeformedPart *output,
    std::string *error = nullptr
);

bool instanceMatrices(
    ModelHandle model,
    std::size_t instance_index,
    const Pose& pose,
    std::vector<Mat4> *matrices,
    std::string *error = nullptr
);

MaterialHandle materialForVariant(
    ModelHandle model,
    std::size_t part_index,
    std::uint32_t variant = INVALID_INDEX
);

} // namespace Models::Runtime

#endif
