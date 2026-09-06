#include "Renderer/PathTracer/WideTlas.hpp"

#include <algorithm>
#include <cmath>
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

Bounds refitNode(
    TlasBuildResult& tlas,
    std::uint32_t node_index,
    const std::vector<Bounds>& instance_bounds,
    bool& valid)
{
    if (node_index >= tlas.nodes.size()) {
        valid = false;
        return emptyBounds();
    }

    WideNode& node = tlas.nodes[node_index];
    const std::uint32_t count = childCount(node);
    if (count == 0u || count > MAX_WIDE_CHILDREN) {
        valid = false;
        return emptyBounds();
    }

    std::vector<Bounds> child_bounds(count);
    Bounds aggregate = emptyBounds();
    for (std::uint32_t child = 0u; child < count; ++child) {
        const std::uint32_t reference = childReference(node, child);
        Bounds bounds = emptyBounds();
        if (isLeafReference(reference)) {
            const std::uint32_t first = leafFirst(reference);
            const std::uint32_t leaf_count = leafCount(reference);
            if (
                leaf_count == 0u ||
                first > tlas.instance_order.size() ||
                leaf_count > tlas.instance_order.size() - first)
            {
                valid = false;
                return emptyBounds();
            }
            for (std::uint32_t i = 0u; i < leaf_count; ++i) {
                const std::uint32_t source = tlas.instance_order[first + i];
                if (source >= instance_bounds.size() || !validBounds(instance_bounds[source])) {
                    valid = false;
                    return emptyBounds();
                }
                expand(bounds, instance_bounds[source]);
            }
        } else {
            bounds = refitNode(tlas, reference, instance_bounds, valid);
            if (!valid) return emptyBounds();
        }
        child_bounds[child] = bounds;
        expand(aggregate, bounds);
    }

    if (!validBounds(aggregate)) {
        valid = false;
        return emptyBounds();
    }

    const Float3 extent = subtract(aggregate.maximum, aggregate.minimum);
    node.base_min = {
        aggregate.minimum.x,
        aggregate.minimum.y,
        aggregate.minimum.z,
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
    return aggregate;
}

} // namespace

bool refitWideTlas(
    TlasBuildResult *tlas,
    const std::vector<Bounds>& instance_bounds,
    std::string *error)
{
    if (!tlas || !tlas->valid || tlas->nodes.empty()) {
        if (error) *error = "cannot refit an invalid TLAS";
        return false;
    }
    if (tlas->instance_order.size() != instance_bounds.size()) {
        if (error) *error = "TLAS instance count changed during refit";
        return false;
    }

    bool valid = true;
    const Bounds bounds = refitNode(*tlas, 0u, instance_bounds, valid);
    if (!valid || !validBounds(bounds)) {
        if (error) *error = "TLAS bound refit failed";
        return false;
    }
    tlas->bounds = bounds;
    tlas->error.clear();
    tlas->valid = true;
    return true;
}

} // namespace Renderer::PathTracerAccel
