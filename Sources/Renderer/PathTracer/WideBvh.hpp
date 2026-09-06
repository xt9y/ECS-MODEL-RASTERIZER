#ifndef RW_ENGINE_RENDERER_PATHTRACER_WIDEBVH_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_WIDEBVH_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::PathTracerAccel {

constexpr std::uint32_t LEAF_REFERENCE_BIT = 0x80000000u;
constexpr std::uint32_t LEAF_FIRST_MASK = 0x00ffffffu;
constexpr std::uint32_t MAX_WIDE_CHILDREN = 8u;
constexpr std::uint32_t MAX_LEAF_TRIANGLES = 8u;

struct Float3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Bounds {
    Float3 minimum{};
    Float3 maximum{};
};

struct Ray {
    Float3 origin{};
    Float3 direction{0.0f, 0.0f, -1.0f};
};

struct CpuHit {
    bool hit = false;
    float distance = 0.0f;
    std::uint32_t triangle = UINT32_MAX;
};

// std430-compatible: eight vec4 values = 128 bytes.
struct alignas(16) Triangle {
    std::array<float, 4> p0{};
    std::array<float, 4> p1{};
    std::array<float, 4> p2{};
    std::array<float, 4> n0{};
    std::array<float, 4> n1{};
    std::array<float, 4> n2{};
    std::array<float, 4> uv01{};
    // xy = uv2, z = material index, w reserved.
    std::array<float, 4> uv2{};
};

// Compressed BVH8 node. Child bounds are 8-bit quantized relative to
// base_min.xyz + extent.xyz. Each child uses two packed uints:
// packed0 = minX|minY|minZ|maxX, packed1 = maxY|maxZ.
struct alignas(16) WideNode {
    // w stores child count as an exactly representable float in [1, 8].
    std::array<float, 4> base_min{};
    std::array<float, 4> extent{};

    std::array<std::uint32_t, 4> child_ref0{};
    std::array<std::uint32_t, 4> child_ref1{};
    std::array<std::uint32_t, 4> bounds0_0{};
    std::array<std::uint32_t, 4> bounds0_1{};
    std::array<std::uint32_t, 4> bounds1_0{};
    std::array<std::uint32_t, 4> bounds1_1{};
};

static_assert(sizeof(Triangle) == 128u, "Triangle GPU layout changed");
static_assert(sizeof(WideNode) == 128u, "WideNode GPU layout changed");

struct BuildResult {
    bool valid = false;
    std::string error;
    std::vector<WideNode> nodes;
    std::vector<Triangle> triangles;
    Bounds bounds{};
};

Float3 add(const Float3& a, const Float3& b);
Float3 subtract(const Float3& a, const Float3& b);
Float3 multiply(const Float3& a, float scalar);
Float3 min(const Float3& a, const Float3& b);
Float3 max(const Float3& a, const Float3& b);
Float3 cross(const Float3& a, const Float3& b);
float dot(const Float3& a, const Float3& b);
float lengthSquared(const Float3& value);
Float3 normalize(const Float3& value);

Bounds triangleBounds(const Triangle& triangle);
Bounds nodeBounds(const WideNode& node);
std::uint32_t childCount(const WideNode& node);
std::uint32_t childReference(const WideNode& node, std::uint32_t child);
Bounds decodeChildBounds(const WideNode& node, std::uint32_t child);

bool isLeafReference(std::uint32_t reference);
std::uint32_t leafFirst(std::uint32_t reference);
std::uint32_t leafCount(std::uint32_t reference);

BuildResult buildWideBvh(const std::vector<Triangle>& triangles);
CpuHit traceClosest(const BuildResult& bvh, const Ray& ray, float maximum_distance = 1.0e30f);

} // namespace Renderer::PathTracerAccel

#endif
