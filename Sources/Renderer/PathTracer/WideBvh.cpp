#include "Renderer/PathTracer/WideBvh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace Renderer::PathTracerAccel {
namespace {

constexpr int kSahBins = 16;
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

void expand(Bounds& bounds, const Float3& point)
{
    bounds.minimum = min(bounds.minimum, point);
    bounds.maximum = max(bounds.maximum, point);
}

void expand(Bounds& bounds, const Bounds& value)
{
    if (!validBounds(value)) return;
    expand(bounds, value.minimum);
    expand(bounds, value.maximum);
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

struct BinaryNode {
    Bounds bounds = emptyBounds();
    Bounds centroid_bounds = emptyBounds();
    std::uint32_t start = 0u;
    std::uint32_t count = 0u;
    std::uint32_t left = kInvalid;
    std::uint32_t right = kInvalid;

    bool leaf() const { return left == kInvalid; }
};

struct Bin {
    Bounds bounds = emptyBounds();
    std::uint32_t count = 0u;
};

struct Builder {
    BuildResult result;
    std::vector<BinaryNode> binary;

    Bounds rangeBounds(std::uint32_t start, std::uint32_t count) const
    {
        Bounds bounds = emptyBounds();
        for (std::uint32_t i = 0u; i < count; ++i) {
            expand(bounds, triangleBounds(result.triangles[start + i]));
        }
        return bounds;
    }

    Bounds rangeCentroidBounds(std::uint32_t start, std::uint32_t count) const
    {
        Bounds bounds = emptyBounds();
        for (std::uint32_t i = 0u; i < count; ++i) {
            expand(bounds, centroid(triangleBounds(result.triangles[start + i])));
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

        int best_axis = -1;
        int best_split = -1;
        float best_cost = std::numeric_limits<float>::infinity();

        for (int axis = 0; axis < 3; ++axis) {
            const float minimum = axisValue(node.centroid_bounds.minimum, axis);
            const float maximum = axisValue(node.centroid_bounds.maximum, axis);
            const float extent = maximum - minimum;
            if (extent <= kEpsilon) continue;

            std::array<Bin, kSahBins> bins{};
            for (std::uint32_t i = 0u; i < count; ++i) {
                const Bounds triangle_bounds = triangleBounds(result.triangles[start + i]);
                const float c = axisValue(centroid(triangle_bounds), axis);
                int bin_index = static_cast<int>(((c - minimum) / extent) * static_cast<float>(kSahBins));
                bin_index = std::clamp(bin_index, 0, kSahBins - 1);
                ++bins[bin_index].count;
                expand(bins[bin_index].bounds, triangle_bounds);
            }

            std::array<Bounds, kSahBins> prefix_bounds{};
            std::array<Bounds, kSahBins> suffix_bounds{};
            std::array<std::uint32_t, kSahBins> prefix_count{};
            std::array<std::uint32_t, kSahBins> suffix_count{};

            Bounds running = emptyBounds();
            std::uint32_t running_count = 0u;
            for (int i = 0; i < kSahBins; ++i) {
                expand(running, bins[i].bounds);
                running_count += bins[i].count;
                prefix_bounds[i] = running;
                prefix_count[i] = running_count;
            }

            running = emptyBounds();
            running_count = 0u;
            for (int i = kSahBins - 1; i >= 0; --i) {
                expand(running, bins[i].bounds);
                running_count += bins[i].count;
                suffix_bounds[i] = running;
                suffix_count[i] = running_count;
            }

            for (int split = 0; split < kSahBins - 1; ++split) {
                const std::uint32_t left_count = prefix_count[split];
                const std::uint32_t right_count = suffix_count[split + 1];
                if (left_count == 0u || right_count == 0u) continue;
                const float cost =
                    surfaceArea(prefix_bounds[split]) * static_cast<float>(left_count) +
                    surfaceArea(suffix_bounds[split + 1]) * static_cast<float>(right_count);
                if (cost < best_cost) {
                    best_cost = cost;
                    best_axis = axis;
                    best_split = split;
                }
            }
        }

        std::uint32_t middle = start;
        if (best_axis >= 0) {
            const float minimum = axisValue(node.centroid_bounds.minimum, best_axis);
            const float maximum = axisValue(node.centroid_bounds.maximum, best_axis);
            const float extent = std::max(maximum - minimum, kEpsilon);
            auto begin = result.triangles.begin() + static_cast<std::ptrdiff_t>(start);
            auto end = begin + static_cast<std::ptrdiff_t>(count);
            auto partition = std::partition(begin, end, [&](const Triangle& triangle) {
                const float c = axisValue(centroid(triangleBounds(triangle)), best_axis);
                int bin_index = static_cast<int>(((c - minimum) / extent) * static_cast<float>(kSahBins));
                bin_index = std::clamp(bin_index, 0, kSahBins - 1);
                return bin_index <= best_split;
            });
            middle = start + static_cast<std::uint32_t>(partition - begin);
        }

        if (middle <= start || middle >= start + count) {
            const Float3 extent = subtract(node.centroid_bounds.maximum, node.centroid_bounds.minimum);
            int axis = extent.y > extent.x ? 1 : 0;
            if (axisValue(extent, 2) > axisValue(extent, axis)) axis = 2;
            middle = start + count / 2u;
            std::nth_element(
                result.triangles.begin() + static_cast<std::ptrdiff_t>(start),
                result.triangles.begin() + static_cast<std::ptrdiff_t>(middle),
                result.triangles.begin() + static_cast<std::ptrdiff_t>(start + count),
                [axis](const Triangle& a, const Triangle& b) {
                    return axisValue(centroid(triangleBounds(a)), axis) <
                           axisValue(centroid(triangleBounds(b)), axis);
                }
            );
        }

        const std::uint32_t left_count = middle - start;
        const std::uint32_t right_count = count - left_count;
        node.left = buildBinary(start, left_count);
        node.right = buildBinary(middle, right_count);
        return node_index;
    }

    static std::uint8_t quantizeMinimum(float value, float base, float extent)
    {
        if (extent <= kEpsilon) return 0u;
        const float normalized = std::clamp((value - base) / extent, 0.0f, 1.0f);
        return static_cast<std::uint8_t>(std::clamp(std::floor(normalized * 255.0f), 0.0f, 255.0f));
    }

    static std::uint8_t quantizeMaximum(float value, float base, float extent)
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

    void setChildReference(WideNode& node, std::uint32_t child, std::uint32_t reference)
    {
        if (child < 4u) node.child_ref0[child] = reference;
        else node.child_ref1[child - 4u] = reference;
    }

    std::uint32_t makeLeafReference(const BinaryNode& node)
    {
        if (node.start > LEAF_FIRST_MASK || node.count == 0u || node.count > MAX_LEAF_TRIANGLES) {
            result.valid = false;
            result.error = "BVH8 leaf encoding overflow";
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
        const Float3 root_extent = subtract(root.bounds.maximum, root.bounds.minimum);
        node.extent = {root_extent.x, root_extent.y, root_extent.z, 0.0f};
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

bool hitAabb(const Ray& ray, const Bounds& bounds, float maximum_distance)
{
    float near_t = 0.0f;
    float far_t = maximum_distance;
    for (int axis = 0; axis < 3; ++axis) {
        const float origin = axisValue(ray.origin, axis);
        const float direction = axisValue(ray.direction, axis);
        const float minimum = axisValue(bounds.minimum, axis);
        const float maximum = axisValue(bounds.maximum, axis);
        if (std::abs(direction) <= kEpsilon) {
            if (origin < minimum || origin > maximum) return false;
            continue;
        }
        const float inverse = 1.0f / direction;
        float t0 = (minimum - origin) * inverse;
        float t1 = (maximum - origin) * inverse;
        if (t0 > t1) std::swap(t0, t1);
        near_t = std::max(near_t, t0);
        far_t = std::min(far_t, t1);
        if (far_t < near_t) return false;
    }
    return near_t < maximum_distance;
}

bool hitTriangleCpu(const Ray& ray, const Triangle& triangle, float& distance)
{
    const Float3 a{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
    const Float3 b{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
    const Float3 c{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
    const Float3 e1 = subtract(b, a);
    const Float3 e2 = subtract(c, a);
    const Float3 p = cross(ray.direction, e2);
    const float determinant = dot(e1, p);
    if (std::abs(determinant) < 1.0e-8f) return false;
    const float inverse_determinant = 1.0f / determinant;
    const Float3 offset = subtract(ray.origin, a);
    const float u = dot(offset, p) * inverse_determinant;
    if (u < 0.0f || u > 1.0f) return false;
    const Float3 q = cross(offset, e1);
    const float v = dot(ray.direction, q) * inverse_determinant;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = dot(e2, q) * inverse_determinant;
    if (t <= 1.0e-5f || t >= distance) return false;
    distance = t;
    return true;
}

} // namespace

Float3 add(const Float3& a, const Float3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Float3 subtract(const Float3& a, const Float3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Float3 multiply(const Float3& a, float scalar)
{
    return {a.x * scalar, a.y * scalar, a.z * scalar};
}

Float3 min(const Float3& a, const Float3& b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Float3 max(const Float3& a, const Float3& b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

Float3 cross(const Float3& a, const Float3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float dot(const Float3& a, const Float3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float lengthSquared(const Float3& value)
{
    return dot(value, value);
}

Float3 normalize(const Float3& value)
{
    const float squared = lengthSquared(value);
    if (squared <= kEpsilon) return {0.0f, 0.0f, -1.0f};
    return multiply(value, 1.0f / std::sqrt(squared));
}

Bounds triangleBounds(const Triangle& triangle)
{
    const Float3 a{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
    const Float3 b{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
    const Float3 c{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
    return {min(a, min(b, c)), max(a, max(b, c))};
}

Bounds nodeBounds(const WideNode& node)
{
    const Float3 minimum{node.base_min[0], node.base_min[1], node.base_min[2]};
    const Float3 extent{node.extent[0], node.extent[1], node.extent[2]};
    return {minimum, add(minimum, extent)};
}

std::uint32_t childCount(const WideNode& node)
{
    return static_cast<std::uint32_t>(std::clamp(std::lround(node.base_min[3]), 0l, 8l));
}

std::uint32_t childReference(const WideNode& node, std::uint32_t child)
{
    if (child >= MAX_WIDE_CHILDREN) return UINT32_MAX;
    return child < 4u ? node.child_ref0[child] : node.child_ref1[child - 4u];
}

Bounds decodeChildBounds(const WideNode& node, std::uint32_t child)
{
    if (child >= childCount(node)) return {};
    const std::uint32_t packed0 = child < 4u ? node.bounds0_0[child] : node.bounds0_1[child - 4u];
    const std::uint32_t packed1 = child < 4u ? node.bounds1_0[child] : node.bounds1_1[child - 4u];
    const float inv = 1.0f / 255.0f;
    const Float3 base{node.base_min[0], node.base_min[1], node.base_min[2]};
    const Float3 extent{node.extent[0], node.extent[1], node.extent[2]};
    const Float3 qmin{
        static_cast<float>(packed0 & 0xffu) * inv,
        static_cast<float>((packed0 >> 8u) & 0xffu) * inv,
        static_cast<float>((packed0 >> 16u) & 0xffu) * inv,
    };
    const Float3 qmax{
        static_cast<float>((packed0 >> 24u) & 0xffu) * inv,
        static_cast<float>(packed1 & 0xffu) * inv,
        static_cast<float>((packed1 >> 8u) & 0xffu) * inv,
    };
    return {
        {base.x + extent.x * qmin.x, base.y + extent.y * qmin.y, base.z + extent.z * qmin.z},
        {base.x + extent.x * qmax.x, base.y + extent.y * qmax.y, base.z + extent.z * qmax.z},
    };
}

bool isLeafReference(std::uint32_t reference)
{
    return (reference & LEAF_REFERENCE_BIT) != 0u;
}

std::uint32_t leafFirst(std::uint32_t reference)
{
    return reference & LEAF_FIRST_MASK;
}

std::uint32_t leafCount(std::uint32_t reference)
{
    return (reference >> 24u) & 0x7fu;
}

BuildResult buildWideBvh(const std::vector<Triangle>& triangles)
{
    Builder builder;
    builder.result.triangles = triangles;
    if (triangles.empty()) {
        builder.result.error = "cannot build BVH8 for an empty triangle set";
        return builder.result;
    }
    if (triangles.size() > LEAF_FIRST_MASK) {
        builder.result.error = "triangle count exceeds BVH8 leaf reference encoding";
        return builder.result;
    }

    builder.binary.reserve(triangles.size() * 2u);
    builder.result.nodes.reserve(std::max<std::size_t>(1u, triangles.size() / 8u));
    const std::uint32_t root = builder.buildBinary(0u, static_cast<std::uint32_t>(triangles.size()));
    builder.result.bounds = builder.binary[root].bounds;
    builder.result.valid = true;
    builder.emitWide(root);
    if (!builder.result.error.empty()) builder.result.valid = false;
    if (builder.result.nodes.empty()) {
        builder.result.valid = false;
        builder.result.error = "BVH8 builder emitted no nodes";
    }
    return builder.result;
}

CpuHit traceClosest(const BuildResult& bvh, const Ray& ray, float maximum_distance)
{
    CpuHit hit;
    hit.distance = maximum_distance;
    if (!bvh.valid || bvh.nodes.empty()) return hit;

    std::vector<std::uint32_t> stack;
    stack.reserve(32u);
    stack.push_back(0u);

    while (!stack.empty()) {
        const std::uint32_t node_index = stack.back();
        stack.pop_back();
        if (node_index >= bvh.nodes.size()) continue;
        const WideNode& node = bvh.nodes[node_index];
        const std::uint32_t count = childCount(node);
        for (std::uint32_t child = 0u; child < count; ++child) {
            const Bounds bounds = decodeChildBounds(node, child);
            if (!hitAabb(ray, bounds, hit.distance)) continue;
            const std::uint32_t reference = childReference(node, child);
            if (isLeafReference(reference)) {
                const std::uint32_t first = leafFirst(reference);
                const std::uint32_t leaf_count = leafCount(reference);
                for (std::uint32_t i = 0u; i < leaf_count; ++i) {
                    const std::uint32_t triangle_index = first + i;
                    if (triangle_index >= bvh.triangles.size()) continue;
                    float distance = hit.distance;
                    if (hitTriangleCpu(ray, bvh.triangles[triangle_index], distance)) {
                        hit.hit = true;
                        hit.distance = distance;
                        hit.triangle = triangle_index;
                    }
                }
            } else if (reference < bvh.nodes.size()) {
                stack.push_back(reference);
            }
        }
    }
    return hit;
}

} // namespace Renderer::PathTracerAccel
