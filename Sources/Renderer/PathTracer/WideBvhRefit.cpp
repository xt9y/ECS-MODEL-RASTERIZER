#include "Renderer/PathTracer/WideBvhRefit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Renderer::PathTracerAccel {
namespace {

constexpr float kEpsilon = 1.0e-8f;

Bounds emptyBounds()
{
    const float infinity = std::numeric_limits<float>::infinity();
    return {{infinity, infinity, infinity}, {-infinity, -infinity, -infinity}};
}

bool validBounds(const Bounds& bounds)
{
    return
        bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

void expand(Bounds& target, const Bounds& value)
{
    if (!validBounds(value)) return;
    if (!validBounds(target)) {
        target = value;
        return;
    }
    target.minimum = min(target.minimum, value.minimum);
    target.maximum = max(target.maximum, value.maximum);
}

std::uint8_t quantizeMinimum(float value, float base, float extent)
{
    if (extent <= kEpsilon) return 0u;
    const float normalized = std::clamp((value - base) / extent, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::clamp(std::floor(normalized * 255.0f), 0.0f, 255.0f));
}

std::uint8_t quantizeMaximum(float value, float base, float extent)
{
    if (extent <= kEpsilon) return 0u;
    const float normalized = std::clamp((value - base) / extent, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::clamp(std::ceil(normalized * 255.0f), 0.0f, 255.0f));
}

void setChildReference(WideNode& node, std::uint32_t child, std::uint32_t reference)
{
    if (child < 4u) node.child_ref0[child] = reference;
    else node.child_ref1[child - 4u] = reference;
}

void setChildBounds(WideNode& node, std::uint32_t child, const Bounds& bounds)
{
    const Float3 base{node.base_min[0], node.base_min[1], node.base_min[2]};
    const Float3 extent{node.extent[0], node.extent[1], node.extent[2]};

    const std::uint32_t min_x = quantizeMinimum(bounds.minimum.x, base.x, extent.x);
    const std::uint32_t min_y = quantizeMinimum(bounds.minimum.y, base.y, extent.y);
    const std::uint32_t min_z = quantizeMinimum(bounds.minimum.z, base.z, extent.z);
    const std::uint32_t max_x = quantizeMaximum(bounds.maximum.x, base.x, extent.x);
    const std::uint32_t max_y = quantizeMaximum(bounds.maximum.y, base.y, extent.y);
    const std::uint32_t max_z = quantizeMaximum(bounds.maximum.z, base.z, extent.z);

    const std::uint32_t packed0 = min_x | (min_y << 8u) | (min_z << 16u) | (max_x << 24u);
    const std::uint32_t packed1 = max_y | (max_z << 8u);
    if (child < 4u) {
        node.bounds0_0[child] = packed0;
        node.bounds1_0[child] = packed1;
    } else {
        node.bounds0_1[child - 4u] = packed0;
        node.bounds1_1[child - 4u] = packed1;
    }
}

Bounds refitNode(BuildResult& bvh, std::uint32_t node_index, bool& valid)
{
    if (node_index >= bvh.nodes.size()) {
        valid = false;
        return emptyBounds();
    }

    WideNode& node = bvh.nodes[node_index];
    const std::uint32_t count = childCount(node);
    if (count == 0u || count > MAX_WIDE_CHILDREN) {
        valid = false;
        return emptyBounds();
    }

    std::vector<Bounds> child_bounds(count);
    Bounds node_bounds = emptyBounds();

    for (std::uint32_t child = 0u; child < count; ++child) {
        const std::uint32_t reference = childReference(node, child);
        Bounds bounds = emptyBounds();
        if (isLeafReference(reference)) {
            const std::uint32_t first = leafFirst(reference);
            const std::uint32_t leaf_count = leafCount(reference);
            if (
                leaf_count == 0u ||
                first > bvh.triangles.size() ||
                leaf_count > bvh.triangles.size() - first)
            {
                valid = false;
                return emptyBounds();
            }
            for (std::uint32_t i = 0u; i < leaf_count; ++i) {
                expand(bounds, triangleBounds(bvh.triangles[first + i]));
            }
        } else {
            bounds = refitNode(bvh, reference, valid);
            if (!valid) return emptyBounds();
        }
        child_bounds[child] = bounds;
        expand(node_bounds, bounds);
    }

    if (!validBounds(node_bounds)) {
        valid = false;
        return emptyBounds();
    }

    const Float3 extent = subtract(node_bounds.maximum, node_bounds.minimum);
    node.base_min = {
        node_bounds.minimum.x,
        node_bounds.minimum.y,
        node_bounds.minimum.z,
        static_cast<float>(count),
    };
    node.extent = {extent.x, extent.y, extent.z, 0.0f};
    node.bounds0_0.fill(0u);
    node.bounds0_1.fill(0u);
    node.bounds1_0.fill(0u);
    node.bounds1_1.fill(0u);

    for (std::uint32_t child = 0u; child < count; ++child) {
        setChildBounds(node, child, child_bounds[child]);
    }
    return node_bounds;
}

} // namespace

BuildResult buildRefittableWideBvh(const std::vector<Triangle>& triangles)
{
    if (triangles.size() > LEAF_FIRST_MASK) {
        BuildResult failed;
        failed.error = "triangle count exceeds refittable BVH8 source-index encoding";
        return failed;
    }

    std::vector<Triangle> tagged = triangles;
    for (std::size_t index = 0u; index < tagged.size(); ++index) {
        // IEEE-754 float represents all integers exactly through 16,777,216;
        // LEAF_FIRST_MASK is 16,777,215, so this temporary tag is lossless.
        tagged[index].uv2[3] = static_cast<float>(index);
    }

    BuildResult result = buildWideBvh(tagged);
    if (!result.valid) return result;

    result.source_order.resize(result.triangles.size());
    for (std::size_t index = 0u; index < result.triangles.size(); ++index) {
        const float encoded = result.triangles[index].uv2[3];
        const std::uint32_t source = static_cast<std::uint32_t>(std::lround(encoded));
        if (source >= triangles.size()) {
            result.valid = false;
            result.error = "refittable BVH8 lost source-triangle permutation";
            result.source_order.clear();
            return result;
        }
        result.source_order[index] = source;
        result.triangles[index].uv2[3] = triangles[source].uv2[3];
    }
    return result;
}

const std::vector<std::uint32_t>& sourceTriangleOrder(const BuildResult& bvh)
{
    return bvh.source_order;
}

bool refitWideBvh(
    BuildResult *bvh,
    const std::vector<Triangle>& source_triangles,
    const std::vector<std::uint32_t>& source_order,
    std::string *error)
{
    if (!bvh || !bvh->valid || bvh->nodes.empty()) {
        if (error) *error = "cannot refit an invalid BVH8";
        return false;
    }
    if (source_order.size() != bvh->triangles.size()) {
        if (error) *error = "BVH8 source-order size changed";
        return false;
    }

    for (std::size_t index = 0u; index < source_order.size(); ++index) {
        const std::uint32_t source = source_order[index];
        if (source >= source_triangles.size()) {
            if (error) *error = "BVH8 source topology changed during refit";
            return false;
        }
        bvh->triangles[index] = source_triangles[source];
    }

    bool valid = true;
    const Bounds bounds = refitNode(*bvh, 0u, valid);
    if (!valid || !validBounds(bounds)) {
        if (error) *error = "BVH8 bound refit failed";
        return false;
    }
    bvh->bounds = bounds;
    bvh->source_order = source_order;
    bvh->error.clear();
    bvh->valid = true;
    return true;
}

bool appendOffsetWideBvh(
    const BuildResult& bvh,
    std::uint32_t node_offset,
    std::uint32_t triangle_offset,
    std::vector<WideNode> *nodes,
    std::vector<Triangle> *triangles,
    std::string *error)
{
    if (!nodes || !triangles || !bvh.valid || bvh.nodes.empty()) {
        if (error) *error = "cannot pack invalid BVH8";
        return false;
    }
    if (node_offset != nodes->size() || triangle_offset != triangles->size()) {
        if (error) *error = "BVH8 packing offsets do not match destination buffers";
        return false;
    }
    if (triangle_offset > LEAF_FIRST_MASK) {
        if (error) *error = "BVH8 packed triangle offset exceeds leaf encoding";
        return false;
    }

    triangles->insert(triangles->end(), bvh.triangles.begin(), bvh.triangles.end());
    nodes->reserve(nodes->size() + bvh.nodes.size());

    for (const WideNode& source_node : bvh.nodes) {
        WideNode node = source_node;
        const std::uint32_t count = childCount(node);
        for (std::uint32_t child = 0u; child < count; ++child) {
            const std::uint32_t reference = childReference(node, child);
            std::uint32_t rebased = reference;
            if (isLeafReference(reference)) {
                const std::uint32_t first = leafFirst(reference);
                const std::uint32_t leaf_count = leafCount(reference);
                const std::uint64_t global_first =
                    static_cast<std::uint64_t>(triangle_offset) + static_cast<std::uint64_t>(first);
                if (global_first > LEAF_FIRST_MASK) {
                    if (error) *error = "BVH8 packed leaf exceeds triangle reference encoding";
                    return false;
                }
                rebased =
                    LEAF_REFERENCE_BIT |
                    (leaf_count << 24u) |
                    static_cast<std::uint32_t>(global_first);
            } else {
                const std::uint64_t global_node =
                    static_cast<std::uint64_t>(node_offset) + static_cast<std::uint64_t>(reference);
                if (global_node >= LEAF_REFERENCE_BIT) {
                    if (error) *error = "BVH8 packed node offset exceeds interior reference encoding";
                    return false;
                }
                rebased = static_cast<std::uint32_t>(global_node);
            }
            setChildReference(node, child, rebased);
        }
        nodes->push_back(node);
    }
    return true;
}

} // namespace Renderer::PathTracerAccel
