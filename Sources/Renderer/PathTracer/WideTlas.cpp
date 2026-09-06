#include "Renderer/PathTracer/WideTlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::PathTracerAccel {
namespace {

constexpr float kEpsilon = 1.0e-8f;
constexpr std::uint32_t kInvalid = UINT32_MAX;

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

Float3 centroid(const Bounds& bounds)
{
    return multiply(add(bounds.minimum, bounds.maximum), 0.5f);
}

float axisValue(const Float3& value, int axis)
{
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

float surfaceArea(const Bounds& bounds)
{
    if (!validBounds(bounds)) return 0.0f;
    const Float3 extent = subtract(bounds.maximum, bounds.minimum);
    return 2.0f * (
        std::max(extent.x, 0.0f) * std::max(extent.y, 0.0f) +
        std::max(extent.y, 0.0f) * std::max(extent.z, 0.0f) +
        std::max(extent.z, 0.0f) * std::max(extent.x, 0.0f)
    );
}

struct Item {
    Bounds bounds{};
    std::uint32_t source = 0u;
};

struct BinaryNode {
    Bounds bounds = emptyBounds();
    Bounds centroid_bounds = emptyBounds();
    std::uint32_t start = 0u;
    std::uint32_t count = 0u;
    std::uint32_t left = kInvalid;
    std::uint32_t right = kInvalid;

    bool leaf() const { return left == kInvalid; }
};

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

struct Builder {
    TlasBuildResult result;
    std::vector<Item> items;
    std::vector<BinaryNode> binary;

    Bounds rangeBounds(std::uint32_t start, std::uint32_t count) const
    {
        Bounds bounds = emptyBounds();
        for (std::uint32_t i = 0u; i < count; ++i) expand(bounds, items[start + i].bounds);
        return bounds;
    }

    Bounds rangeCentroidBounds(std::uint32_t start, std::uint32_t count) const
    {
        Bounds bounds = emptyBounds();
        for (std::uint32_t i = 0u; i < count; ++i) {
            const Float3 point = centroid(items[start + i].bounds);
            const Bounds point_bounds{point, point};
            expand(bounds, point_bounds);
        }
        return bounds;
    }

    std::uint32_t buildBinary(std::uint32_t start, std::uint32_t count)
    {
        const std::uint32_t node_index = static_cast<std::uint32_t>(binary.size());
        binary.push_back({});
        BinaryNode& node = binary[node_index];
        node.start = start;
        node.count = count;
        node.bounds = rangeBounds(start, count);
        node.centroid_bounds = rangeCentroidBounds(start, count);

        if (count <= MAX_LEAF_TRIANGLES) return node_index;

        const Float3 extent = subtract(node.centroid_bounds.maximum, node.centroid_bounds.minimum);
        int axis = extent.y > extent.x ? 1 : 0;
        if (axisValue(extent, 2) > axisValue(extent, axis)) axis = 2;

        const std::uint32_t middle = start + count / 2u;
        std::nth_element(
            items.begin() + static_cast<std::ptrdiff_t>(start),
            items.begin() + static_cast<std::ptrdiff_t>(middle),
            items.begin() + static_cast<std::ptrdiff_t>(start + count),
            [axis](const Item& a, const Item& b) {
                return axisValue(centroid(a.bounds), axis) < axisValue(centroid(b.bounds), axis);
            }
        );

        node.left = buildBinary(start, middle - start);
        node.right = buildBinary(middle, start + count - middle);
        return node_index;
    }

    std::uint32_t makeLeafReference(const BinaryNode& node)
    {
        if (
            node.start > LEAF_FIRST_MASK ||
            node.count == 0u ||
            node.count > MAX_LEAF_TRIANGLES)
        {
            result.error = "TLAS leaf encoding overflow";
            return 0u;
        }
        return LEAF_REFERENCE_BIT | (node.count << 24u) | node.start;
    }

    std::uint32_t emitWide(std::uint32_t binary_index)
    {
        const BinaryNode root = binary[binary_index];
        const std::uint32_t wide_index = static_cast<std::uint32_t>(result.nodes.size());
        result.nodes.push_back({});

        std::vector<std::uint32_t> frontier;
        if (root.leaf()) {
            frontier.push_back(binary_index);
        } else {
            frontier.push_back(root.left);
            frontier.push_back(root.right);
        }

        while (frontier.size() < MAX_WIDE_CHILDREN) {
            std::size_t expand_index = frontier.size();
            float best_score = -1.0f;
            for (std::size_t i = 0u; i < frontier.size(); ++i) {
                const BinaryNode& candidate = binary[frontier[i]];
                if (candidate.leaf()) continue;
                const float score = surfaceArea(candidate.bounds) * static_cast<float>(candidate.count);
                if (score > best_score) {
                    best_score = score;
                    expand_index = i;
                }
            }
            if (expand_index >= frontier.size()) break;
            const BinaryNode candidate = binary[frontier[expand_index]];
            frontier.erase(frontier.begin() + static_cast<std::ptrdiff_t>(expand_index));
            frontier.insert(frontier.begin() + static_cast<std::ptrdiff_t>(expand_index), candidate.right);
            frontier.insert(frontier.begin() + static_cast<std::ptrdiff_t>(expand_index), candidate.left);
        }

        WideNode node{};
        node.base_min = {
            root.bounds.minimum.x,
            root.bounds.minimum.y,
            root.bounds.minimum.z,
            static_cast<float>(frontier.size()),
        };
        const Float3 extent = subtract(root.bounds.maximum, root.bounds.minimum);
        node.extent = {extent.x, extent.y, extent.z, 0.0f};
        node.child_ref0.fill(UINT32_MAX);
        node.child_ref1.fill(UINT32_MAX);

        for (std::uint32_t child = 0u; child < frontier.size(); ++child) {
            const BinaryNode& binary_child = binary[frontier[child]];
            const std::uint32_t reference = binary_child.leaf()
                ? makeLeafReference(binary_child)
                : emitWide(frontier[child]);
            if (!result.error.empty()) return wide_index;
            setChildReference(node, child, reference);
            setChildBounds(node, child, binary_child.bounds);
        }

        result.nodes[wide_index] = node;
        return wide_index;
    }
};

} // namespace

TlasBuildResult buildWideTlas(const std::vector<Bounds>& instance_bounds)
{
    Builder builder;
    if (instance_bounds.empty()) {
        builder.result.error = "cannot build TLAS for zero instances";
        return builder.result;
    }
    if (instance_bounds.size() > LEAF_FIRST_MASK) {
        builder.result.error = "instance count exceeds TLAS leaf encoding";
        return builder.result;
    }

    builder.items.reserve(instance_bounds.size());
    for (std::uint32_t index = 0u; index < instance_bounds.size(); ++index) {
        if (!validBounds(instance_bounds[index])) {
            builder.result.error = "TLAS instance has invalid bounds";
            return builder.result;
        }
        builder.items.push_back({instance_bounds[index], index});
    }

    builder.binary.reserve(instance_bounds.size() * 2u);
    builder.result.nodes.reserve(std::max<std::size_t>(1u, instance_bounds.size() / 4u));
    const std::uint32_t root = builder.buildBinary(0u, static_cast<std::uint32_t>(builder.items.size()));
    builder.result.bounds = builder.binary[root].bounds;
    builder.emitWide(root);
    if (!builder.result.error.empty() || builder.result.nodes.empty()) {
        if (builder.result.error.empty()) builder.result.error = "TLAS builder emitted no nodes";
        return builder.result;
    }

    builder.result.instance_order.reserve(builder.items.size());
    for (const Item& item : builder.items) builder.result.instance_order.push_back(item.source);
    builder.result.valid = true;
    return builder.result;
}

} // namespace Renderer::PathTracerAccel
