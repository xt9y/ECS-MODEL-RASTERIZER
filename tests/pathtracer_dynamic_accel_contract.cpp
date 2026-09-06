#include "Renderer/PathTracer/WideBvh.hpp"
#include "Renderer/PathTracer/WideBvhRefit.hpp"
#include "Renderer/PathTracer/WideTlas.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace Renderer::PathTracerAccel;

Triangle triangleAt(float x, float z, std::uint32_t material)
{
    Triangle triangle{};
    triangle.p0 = {x - 0.5f, -0.5f, z, 0.0f};
    triangle.p1 = {x + 0.5f, -0.5f, z, 0.0f};
    triangle.p2 = {x, 0.5f, z, 0.0f};
    triangle.n0 = {0.0f, 0.0f, 1.0f, 0.0f};
    triangle.n1 = triangle.n0;
    triangle.n2 = triangle.n0;
    triangle.uv2[2] = static_cast<float>(material);
    return triangle;
}

bool encloses(const Bounds& outer, const Bounds& inner)
{
    constexpr float epsilon = 1.0e-4f;
    return
        outer.minimum.x <= inner.minimum.x + epsilon &&
        outer.minimum.y <= inner.minimum.y + epsilon &&
        outer.minimum.z <= inner.minimum.z + epsilon &&
        outer.maximum.x + epsilon >= inner.maximum.x &&
        outer.maximum.y + epsilon >= inner.maximum.y &&
        outer.maximum.z + epsilon >= inner.maximum.z;
}

} // namespace

int main()
{
    std::vector<Triangle> bind_pose;
    for (std::uint32_t i = 0; i < 48u; ++i) {
        bind_pose.push_back(triangleAt(static_cast<float>(i % 8u), -2.0f - static_cast<float>(i / 8u), i % 3u));
    }

    BuildResult dynamic_blas = buildWideBvh(bind_pose);
    assert(dynamic_blas.valid);
    const std::size_t original_node_count = dynamic_blas.nodes.size();
    const std::size_t original_triangle_count = dynamic_blas.triangles.size();

    const std::vector<std::uint32_t> source_order = sourceTriangleOrder(dynamic_blas);
    assert(source_order.size() == bind_pose.size());

    std::vector<Triangle> animated_pose = bind_pose;
    for (std::size_t i = 0; i < animated_pose.size(); ++i) {
        const float offset = 0.15f * std::sin(static_cast<float>(i));
        animated_pose[i].p0[1] += offset;
        animated_pose[i].p1[1] += offset;
        animated_pose[i].p2[1] += offset;
    }

    assert(refitWideBvh(&dynamic_blas, animated_pose, source_order));
    assert(dynamic_blas.nodes.size() == original_node_count);
    assert(dynamic_blas.triangles.size() == original_triangle_count);

    for (const WideNode& node : dynamic_blas.nodes) {
        const std::uint32_t count = childCount(node);
        for (std::uint32_t child = 0u; child < count; ++child) {
            const Bounds decoded = decodeChildBounds(node, child);
            const std::uint32_t reference = childReference(node, child);
            if (isLeafReference(reference)) {
                const std::uint32_t first = leafFirst(reference);
                const std::uint32_t leaf_count = leafCount(reference);
                for (std::uint32_t i = 0u; i < leaf_count; ++i) {
                    assert(encloses(decoded, triangleBounds(dynamic_blas.triangles[first + i])));
                }
            } else {
                assert(reference < dynamic_blas.nodes.size());
                assert(encloses(decoded, nodeBounds(dynamic_blas.nodes[reference])));
            }
        }
    }

    std::vector<Bounds> instances = {
        {{-12.0f, -2.0f, -12.0f}, {12.0f, 8.0f, 12.0f}},
        {{-1.0f, 0.0f, -3.0f}, {1.0f, 2.0f, -1.0f}},
        {{4.0f, 1.0f, -6.0f}, {5.0f, 3.0f, -5.0f}},
        {{-5.0f, 0.0f, -8.0f}, {-3.0f, 4.0f, -6.0f}},
    };

    const TlasBuildResult tlas = buildWideTlas(instances);
    assert(tlas.valid);
    assert(!tlas.nodes.empty());
    assert(tlas.instance_order.size() == instances.size());

    std::vector<std::uint32_t> sorted = tlas.instance_order;
    std::sort(sorted.begin(), sorted.end());
    for (std::uint32_t i = 0u; i < sorted.size(); ++i) assert(sorted[i] == i);

    for (const WideNode& node : tlas.nodes) {
        const std::uint32_t count = childCount(node);
        for (std::uint32_t child = 0u; child < count; ++child) {
            const Bounds decoded = decodeChildBounds(node, child);
            const std::uint32_t reference = childReference(node, child);
            if (isLeafReference(reference)) {
                const std::uint32_t first = leafFirst(reference);
                const std::uint32_t leaf_count = leafCount(reference);
                assert(first + leaf_count <= tlas.instance_order.size());
                for (std::uint32_t i = 0u; i < leaf_count; ++i) {
                    assert(encloses(decoded, instances[tlas.instance_order[first + i]]));
                }
            } else {
                assert(reference < tlas.nodes.size());
                assert(encloses(decoded, nodeBounds(tlas.nodes[reference])));
            }
        }
    }

    return 0;
}
