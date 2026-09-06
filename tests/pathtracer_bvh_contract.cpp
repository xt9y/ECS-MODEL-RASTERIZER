#include "Renderer/PathTracer/WideBvh.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

using namespace Renderer::PathTracerAccel;

Triangle makeTriangle(float x, float y, float z, std::uint32_t material)
{
    Triangle triangle{};
    triangle.p0 = {x - 0.45f, y - 0.35f, z, 0.0f};
    triangle.p1 = {x + 0.50f, y - 0.30f, z, 0.0f};
    triangle.p2 = {x, y + 0.55f, z, 0.0f};
    triangle.n0 = {0.0f, 0.0f, 1.0f, 0.0f};
    triangle.n1 = triangle.n0;
    triangle.n2 = triangle.n0;
    triangle.uv01 = {0.0f, 0.0f, 1.0f, 0.0f};
    triangle.uv2 = {0.5f, 1.0f, static_cast<float>(material), 0.0f};
    return triangle;
}

bool hitTriangle(const Ray& ray, const Triangle& triangle, float& distance)
{
    const Float3 a{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
    const Float3 b{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
    const Float3 c{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
    const Float3 e1 = subtract(b, a);
    const Float3 e2 = subtract(c, a);
    const Float3 p = cross(ray.direction, e2);
    const float det = dot(e1, p);
    if (std::abs(det) < 1.0e-7f) return false;
    const float inv_det = 1.0f / det;
    const Float3 s = subtract(ray.origin, a);
    const float u = dot(s, p) * inv_det;
    if (u < 0.0f || u > 1.0f) return false;
    const Float3 q = cross(s, e1);
    const float v = dot(ray.direction, q) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = dot(e2, q) * inv_det;
    if (t <= 1.0e-5f || t >= distance) return false;
    distance = t;
    return true;
}

float bruteForce(const std::vector<Triangle>& triangles, const Ray& ray)
{
    float distance = std::numeric_limits<float>::infinity();
    for (const Triangle& triangle : triangles) {
        hitTriangle(ray, triangle, distance);
    }
    return distance;
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
    std::vector<Triangle> source;
    for (int z = 0; z < 5; ++z) {
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 6; ++x) {
                source.push_back(makeTriangle(
                    static_cast<float>(x) * 1.4f - 3.5f,
                    static_cast<float>(y) * 1.2f - 1.8f,
                    -2.0f - static_cast<float>(z) * 1.7f,
                    static_cast<std::uint32_t>((x + y + z) % 7)
                ));
            }
        }
    }

    const BuildResult result = buildWideBvh(source);
    assert(result.valid);
    assert(!result.nodes.empty());
    assert(result.triangles.size() == source.size());

    std::vector<std::uint32_t> seen(result.triangles.size(), 0u);
    for (const WideNode& node : result.nodes) {
        const std::uint32_t child_count = childCount(node);
        assert(child_count >= 1u && child_count <= 8u);
        for (std::uint32_t child = 0u; child < child_count; ++child) {
            const Bounds decoded = decodeChildBounds(node, child);
            const std::uint32_t reference = childReference(node, child);
            if (isLeafReference(reference)) {
                const std::uint32_t first = leafFirst(reference);
                const std::uint32_t count = leafCount(reference);
                assert(count >= 1u && count <= 8u);
                assert(first + count <= result.triangles.size());
                for (std::uint32_t index = 0u; index < count; ++index) {
                    const std::uint32_t triangle_index = first + index;
                    ++seen[triangle_index];
                    assert(encloses(decoded, triangleBounds(result.triangles[triangle_index])));
                }
            } else {
                assert(reference < result.nodes.size());
                assert(encloses(decoded, nodeBounds(result.nodes[reference])));
            }
        }
    }
    assert(std::all_of(seen.begin(), seen.end(), [](std::uint32_t count) { return count == 1u; }));

    for (int y = -5; y <= 5; ++y) {
        for (int x = -8; x <= 8; ++x) {
            Ray ray;
            ray.origin = {
                static_cast<float>(x) * 0.42f,
                static_cast<float>(y) * 0.37f,
                2.0f,
            };
            ray.direction = normalize({0.03f * static_cast<float>(x), -0.02f * static_cast<float>(y), -1.0f});

            const float expected = bruteForce(source, ray);
            const CpuHit actual = traceClosest(result, ray);
            if (std::isfinite(expected)) {
                assert(actual.hit);
                assert(std::abs(actual.distance - expected) < 1.0e-3f);
            } else {
                assert(!actual.hit);
            }
        }
    }

    return 0;
}
