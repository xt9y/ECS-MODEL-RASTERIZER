#ifndef RW_ENGINE_ANIMATION_HPP
#define RW_ENGINE_ANIMATION_HPP

#include "Ecs/Ecs.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Animation {

using SkeletonHandle = std::uint32_t;
using ClipHandle = std::uint32_t;

constexpr SkeletonHandle INVALID_SKELETON = UINT32_MAX;
constexpr ClipHandle INVALID_CLIP = UINT32_MAX;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Mat4 {
    std::array<float, 16> value {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
};

struct Transform {
    Vec3 translation {};
    Quat rotation {};
    Vec3 scale {1.0f, 1.0f, 1.0f};
};

struct Bone {
    std::string name;
    std::int32_t parent = -1;
    Transform bind_local {};
    Mat4 inverse_bind {};
};

struct Skeleton {
    std::string name;
    std::vector<Bone> bones;
};

struct Track {
    std::vector<Transform> samples;
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    float sample_rate = 30.0f;
    std::vector<Track> tracks;
};

struct Pose {
    std::vector<Transform> local;
    std::vector<Mat4> global;
    std::vector<Mat4> skin;
    std::uint64_t revision = 0u;
};

struct SkinWeights {
    std::array<std::uint16_t, 4> joints {};
    std::array<float, 4> weights {};
};

struct AnimatorComponent {
    SkeletonHandle skeleton = INVALID_SKELETON;
    ClipHandle clip = INVALID_CLIP;
    ClipHandle next_clip = INVALID_CLIP;

    float time = 0.0f;
    float next_time = 0.0f;
    float speed = 1.0f;
    float next_speed = 1.0f;
    float blend_time = 0.0f;
    float blend_duration = 0.0f;

    bool loop = true;
    bool next_loop = true;
    bool playing = true;

    Pose pose;
};

struct SkinBindingComponent {
    Ecs::Entity animator = Ecs::INVALID_ENTITY;
};

struct State {
    std::string name;
    ClipHandle clip = INVALID_CLIP;
    bool loop = true;
    float speed = 1.0f;
};

class StateMachine {
public:
    void add(State state);
    const State *find(std::string_view name) const;

private:
    std::vector<State> states_;
    std::unordered_map<std::string, std::size_t> lookup_;
};

class System {
public:
    void update(Ecs::World& world, float delta_seconds) const;
};

SkeletonHandle registerSkeleton(Skeleton skeleton);
ClipHandle registerClip(AnimationClip clip);
const Skeleton *skeleton(SkeletonHandle handle);
const AnimationClip *clip(ClipHandle handle);
void clearAssets();

void play(
    AnimatorComponent& animator,
    ClipHandle clip,
    float blend_seconds = 0.0f,
    bool loop = true,
    float speed = 1.0f
);

bool playState(
    AnimatorComponent& animator,
    const StateMachine& machine,
    std::string_view state,
    float blend_seconds = 0.0f
);

void stop(AnimatorComponent& animator);

Transform blend(const Transform& a, const Transform& b, float factor);
Mat4 matrix(const Transform& transform);
Mat4 multiply(const Mat4& a, const Mat4& b);
Vec3 transformPoint(const Mat4& matrix, Vec3 point);
Vec3 transformVector(const Mat4& matrix, Vec3 vector);

bool skinVertex(
    const Pose& pose,
    const SkinWeights& skin,
    Vec3 position,
    Vec3 normal,
    Vec3 *out_position,
    Vec3 *out_normal
);

} // namespace Animation

#endif
