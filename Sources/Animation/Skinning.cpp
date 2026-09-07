#include "Animation/Animation.hpp"

#include <cmath>

namespace Animation {

void buildSkinMatrices(
    const Pose& pose,
    const std::vector<Mat4>& inverse_bind,
    std::vector<Mat4> *out_matrices)
{
    if (!out_matrices) return;
    const std::size_t count = pose.global.size();
    out_matrices->resize(count);

    if (inverse_bind.size() == count) {
        for (std::size_t joint = 0u; joint < count; ++joint) {
            (*out_matrices)[joint] = multiply(pose.global[joint], inverse_bind[joint]);
        }
        return;
    }

    if (pose.skin.size() == count) {
        *out_matrices = pose.skin;
        return;
    }

    for (std::size_t joint = 0u; joint < count; ++joint) {
        (*out_matrices)[joint] = pose.global[joint];
    }
}

bool skinVertex(
    const std::vector<Mat4>& skin_matrices,
    const SkinWeights& skin,
    Vec3 position,
    Vec3 normal,
    Vec3 *out_position,
    Vec3 *out_normal)
{
    if (!out_position || !out_normal) return false;

    Vec3 skinned_position {};
    Vec3 skinned_normal {};
    float total_weight = 0.0f;

    for (std::size_t influence = 0u; influence < skin.weights.size(); ++influence) {
        const float weight = skin.weights[influence];
        const std::size_t joint = skin.joints[influence];
        if (weight <= 0.0f || joint >= skin_matrices.size()) continue;

        const Vec3 transformed_position = transformPoint(skin_matrices[joint], position);
        const Vec3 transformed_normal = transformVector(skin_matrices[joint], normal);
        skinned_position.x += transformed_position.x * weight;
        skinned_position.y += transformed_position.y * weight;
        skinned_position.z += transformed_position.z * weight;
        skinned_normal.x += transformed_normal.x * weight;
        skinned_normal.y += transformed_normal.y * weight;
        skinned_normal.z += transformed_normal.z * weight;
        total_weight += weight;
    }

    if (total_weight <= 1.0e-8f) {
        *out_position = position;
        *out_normal = normal;
        return false;
    }

    const float inverse_weight = 1.0f / total_weight;
    skinned_position.x *= inverse_weight;
    skinned_position.y *= inverse_weight;
    skinned_position.z *= inverse_weight;

    const float normal_length_squared =
        skinned_normal.x * skinned_normal.x +
        skinned_normal.y * skinned_normal.y +
        skinned_normal.z * skinned_normal.z;
    if (normal_length_squared > 1.0e-20f) {
        const float inverse_length = 1.0f / std::sqrt(normal_length_squared);
        skinned_normal.x *= inverse_length;
        skinned_normal.y *= inverse_length;
        skinned_normal.z *= inverse_length;
    } else {
        skinned_normal = normal;
    }

    *out_position = skinned_position;
    *out_normal = skinned_normal;
    return true;
}

} // namespace Animation
