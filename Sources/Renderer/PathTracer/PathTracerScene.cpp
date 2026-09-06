#include "Renderer/PathTracer/PathTracerScene.hpp"

#include "Animation/Animation.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/PathTracer/WideBvhRefit.hpp"
#include "Renderer/PathTracer/WideTlas.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Renderer::PathTracerScene {
namespace {

using Mat4 = PathTracerGpu::Mat4;
using Bounds = PathTracerAccel::Bounds;
using Float3 = PathTracerAccel::Float3;
using Triangle = PathTracerAccel::Triangle;
using WideNode = PathTracerAccel::WideNode;

constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kMaximumTriangles = 1000000u;

Mat4 identityMatrix()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
            }
        }
    }
    return result;
}

Mat4 translation(float x, float y, float z)
{
    Mat4 result = identityMatrix();
    result[12] = x;
    result[13] = y;
    result[14] = z;
    return result;
}

Mat4 scaling(float x, float y, float z)
{
    Mat4 result{};
    result[0] = x;
    result[5] = y;
    result[10] = z;
    result[15] = 1.0f;
    return result;
}

Mat4 rotationX(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[5] = c;
    result[6] = s;
    result[9] = -s;
    result[10] = c;
    return result;
}

Mat4 rotationY(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = c;
    result[2] = -s;
    result[8] = s;
    result[10] = c;
    return result;
}

Mat4 rotationZ(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = c;
    result[1] = s;
    result[4] = -s;
    result[5] = c;
    return result;
}

Mat4 modelMatrix(const Transform& transform)
{
    return multiply(
        multiply(
            multiply(
                multiply(
                    translation(transform.position.x, transform.position.y, transform.position.z),
                    rotationX(transform.rotation.x)
                ),
                rotationY(transform.rotation.y)
            ),
            rotationZ(transform.rotation.z)
        ),
        scaling(transform.scale.x, transform.scale.y, transform.scale.z)
    );
}

Mat4 inverseModelMatrix(const Transform& transform)
{
    const float sx = std::abs(transform.scale.x) > 1.0e-8f ? 1.0f / transform.scale.x : 0.0f;
    const float sy = std::abs(transform.scale.y) > 1.0e-8f ? 1.0f / transform.scale.y : 0.0f;
    const float sz = std::abs(transform.scale.z) > 1.0e-8f ? 1.0f / transform.scale.z : 0.0f;
    return multiply(
        multiply(
            multiply(
                multiply(scaling(sx, sy, sz), rotationZ(-transform.rotation.z)),
                rotationY(-transform.rotation.y)
            ),
            rotationX(-transform.rotation.x)
        ),
        translation(-transform.position.x, -transform.position.y, -transform.position.z)
    );
}

Float3 transformPoint(const Mat4& matrix, const Float3& point)
{
    return {
        matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
        matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14],
    };
}

Float3 transformNormal(const Mat4& world_to_object, const Float3& normal)
{
    return PathTracerAccel::normalize({
        world_to_object[0] * normal.x + world_to_object[1] * normal.y + world_to_object[2] * normal.z,
        world_to_object[4] * normal.x + world_to_object[5] * normal.y + world_to_object[6] * normal.z,
        world_to_object[8] * normal.x + world_to_object[9] * normal.y + world_to_object[10] * normal.z,
    });
}

Bounds transformedBounds(const Bounds& bounds, const Mat4& matrix)
{
    const float infinity = std::numeric_limits<float>::infinity();
    Bounds result{{infinity, infinity, infinity}, {-infinity, -infinity, -infinity}};
    for (int x = 0; x < 2; ++x) {
        for (int y = 0; y < 2; ++y) {
            for (int z = 0; z < 2; ++z) {
                const Float3 corner{
                    x == 0 ? bounds.minimum.x : bounds.maximum.x,
                    y == 0 ? bounds.minimum.y : bounds.maximum.y,
                    z == 0 ? bounds.minimum.z : bounds.maximum.z,
                };
                const Float3 world = transformPoint(matrix, corner);
                result.minimum = PathTracerAccel::min(result.minimum, world);
                result.maximum = PathTracerAccel::max(result.maximum, world);
            }
        }
    }
    return result;
}

void hashValue(std::uint64_t& hash, std::uint32_t value)
{
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ull;
}

void hashValue(std::uint64_t& hash, std::uint64_t value)
{
    hashValue(hash, static_cast<std::uint32_t>(value));
    hashValue(hash, static_cast<std::uint32_t>(value >> 32u));
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

void hashTransform(std::uint64_t& hash, const Transform& transform)
{
    hashFloat(hash, transform.position.x);
    hashFloat(hash, transform.position.y);
    hashFloat(hash, transform.position.z);
    hashFloat(hash, transform.rotation.x);
    hashFloat(hash, transform.rotation.y);
    hashFloat(hash, transform.rotation.z);
    hashFloat(hash, transform.scale.x);
    hashFloat(hash, transform.scale.y);
    hashFloat(hash, transform.scale.z);
}

const Animation::Pose *animatedPose(
    const Ecs::World& world,
    Ecs::Entity entity,
    Ecs::Entity *animator_entity = nullptr)
{
    const Animation::SkinBindingComponent *binding = world.get<Animation::SkinBindingComponent>(entity);
    if (!binding || binding->animator == Ecs::INVALID_ENTITY) return nullptr;
    const Animation::AnimatorComponent *animator = world.get<Animation::AnimatorComponent>(binding->animator);
    if (!animator || animator->pose.skin.empty()) return nullptr;
    if (animator_entity) *animator_entity = binding->animator;
    return &animator->pose;
}

void setChildReference(WideNode& node, std::uint32_t child, std::uint32_t reference)
{
    if (child < 4u) node.child_ref0[child] = reference;
    else node.child_ref1[child - 4u] = reference;
}

bool writePackedBlas(
    const PathTracerAccel::BuildResult& bvh,
    std::size_t node_first,
    std::size_t triangle_first,
    std::vector<WideNode>& nodes,
    std::vector<Triangle>& triangles,
    std::string *error)
{
    if (
        node_first > nodes.size() ||
        bvh.nodes.size() > nodes.size() - node_first ||
        triangle_first > triangles.size() ||
        bvh.triangles.size() > triangles.size() - triangle_first)
    {
        if (error) *error = "packed BVH8 range no longer fits acceleration buffers";
        return false;
    }
    if (triangle_first > PathTracerAccel::LEAF_FIRST_MASK) {
        if (error) *error = "packed BVH8 triangle range exceeds reference encoding";
        return false;
    }

    std::copy(bvh.triangles.begin(), bvh.triangles.end(), triangles.begin() + static_cast<std::ptrdiff_t>(triangle_first));
    for (std::size_t local = 0u; local < bvh.nodes.size(); ++local) {
        WideNode node = bvh.nodes[local];
        const std::uint32_t child_count = PathTracerAccel::childCount(node);
        for (std::uint32_t child = 0u; child < child_count; ++child) {
            const std::uint32_t reference = PathTracerAccel::childReference(node, child);
            std::uint32_t rebased = reference;
            if (PathTracerAccel::isLeafReference(reference)) {
                const std::uint64_t first =
                    static_cast<std::uint64_t>(triangle_first) +
                    static_cast<std::uint64_t>(PathTracerAccel::leafFirst(reference));
                if (first > PathTracerAccel::LEAF_FIRST_MASK) {
                    if (error) *error = "packed BVH8 leaf exceeds reference encoding";
                    return false;
                }
                rebased =
                    PathTracerAccel::LEAF_REFERENCE_BIT |
                    (PathTracerAccel::leafCount(reference) << 24u) |
                    static_cast<std::uint32_t>(first);
            } else {
                const std::uint64_t child_node =
                    static_cast<std::uint64_t>(node_first) + static_cast<std::uint64_t>(reference);
                if (child_node >= PathTracerAccel::LEAF_REFERENCE_BIT) {
                    if (error) *error = "packed BVH8 node exceeds reference encoding";
                    return false;
                }
                rebased = static_cast<std::uint32_t>(child_node);
            }
            setChildReference(node, child, rebased);
        }
        nodes[node_first + local] = node;
    }
    return true;
}

} // namespace

struct SceneCache::Impl {
    struct Region {
        std::size_t node_first = 0u;
        std::size_t node_count = 0u;
        std::size_t triangle_first = 0u;
        std::size_t triangle_count = 0u;
    };

    struct DynamicEntry {
        Ecs::Entity entity = Ecs::INVALID_ENTITY;
        Ecs::Entity animator = Ecs::INVALID_ENTITY;
        std::uint32_t mesh = Models::INVALID_MESH;
        std::uint32_t material = Models::INVALID_MATERIAL;
        std::uint32_t gpu_material = 0u;
        std::uint64_t pose_revision = 0u;
        PathTracerAccel::BuildResult bvh;
        Region region;
        Mat4 model = identityMatrix();
        Mat4 inverse_model = identityMatrix();
        Bounds world_bounds{};
        std::size_t source_instance = 0u;
    };

    PathTracerAccel::BuildResult static_blas;
    Region static_region;
    bool has_static = false;
    std::vector<DynamicEntry> dynamic;
    PathTracerAccel::TlasBuildResult tlas;
    std::vector<Bounds> instance_bounds;

    std::vector<WideNode> nodes;
    std::vector<Triangle> triangles;
    std::vector<PathTracerGpu::Instance> instances;
    std::vector<PathTracerGpu::Material> materials;
    std::array<Models::TextureHandle, 16> texture_handles{};

    std::unordered_map<std::uint32_t, std::uint32_t> material_indices;
    std::unordered_map<std::uint32_t, int> texture_indices;

    std::uint64_t topology_signature = 0u;
    std::uint64_t pose_signature = 0u;
    std::uint64_t transform_signature = 0u;
    std::uint64_t topology_revision = 0u;
    bool built = false;

    Impl()
    {
        texture_handles.fill(Models::INVALID_TEXTURE);
    }

    void reset()
    {
        static_blas = {};
        static_region = {};
        has_static = false;
        dynamic.clear();
        tlas = {};
        instance_bounds.clear();
        nodes.clear();
        triangles.clear();
        instances.clear();
        materials.clear();
        texture_handles.fill(Models::INVALID_TEXTURE);
        material_indices.clear();
        texture_indices.clear();
        topology_signature = 0u;
        pose_signature = 0u;
        transform_signature = 0u;
        built = false;
    }

    std::uint64_t topologySignature(const Ecs::World& world) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (const Ecs::Entity entity : world.entities()) {
            const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
            const MeshComponent *mesh = world.get<MeshComponent>(entity);
            const Transform *transform = world.get<Transform>(entity);
            if (!renderable || !mesh || !transform) continue;
            hashValue(hash, entity);
            hashValue(hash, renderable->visible ? 1u : 0u);
            hashValue(hash, mesh->mesh);
            hashValue(hash, mesh->material);
            if (!renderable->visible) continue;
            Ecs::Entity animator = Ecs::INVALID_ENTITY;
            const bool dynamic_entity = animatedPose(world, entity, &animator) != nullptr;
            hashValue(hash, dynamic_entity ? 1u : 0u);
            if (dynamic_entity) {
                hashValue(hash, animator);
            } else {
                hashTransform(hash, *transform);
            }
        }
        return hash;
    }

    std::uint64_t poseSignature(const Ecs::World& world) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (const DynamicEntry& entry : dynamic) {
            hashValue(hash, entry.entity);
            const Animation::Pose *pose = animatedPose(world, entry.entity);
            hashValue(hash, pose ? pose->revision : 0u);
        }
        return hash;
    }

    std::uint64_t transformSignature(const Ecs::World& world) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (const DynamicEntry& entry : dynamic) {
            hashValue(hash, entry.entity);
            const Transform *transform = world.get<Transform>(entry.entity);
            if (transform) hashTransform(hash, *transform);
        }
        return hash;
    }

    std::uint32_t materialIndex(std::uint32_t handle)
    {
        const auto found = material_indices.find(handle);
        if (found != material_indices.end()) return found->second;

        PathTracerGpu::Material gpu;
        const Models::MaterialData *material = Models::material(handle);
        if (material) {
            gpu.base_color = {
                material->color.x,
                material->color.y,
                material->color.z,
                std::clamp(material->opacity, 0.0f, 1.0f),
            };
            if (material->diffuse_texture != Models::INVALID_TEXTURE) {
                int slot = -1;
                const auto texture_found = texture_indices.find(material->diffuse_texture);
                if (texture_found != texture_indices.end()) {
                    slot = texture_found->second;
                } else {
                    for (std::size_t index = 0u; index < texture_handles.size(); ++index) {
                        if (texture_handles[index] != Models::INVALID_TEXTURE) continue;
                        texture_handles[index] = material->diffuse_texture;
                        slot = static_cast<int>(index);
                        texture_indices.emplace(material->diffuse_texture, slot);
                        break;
                    }
                }
                gpu.data[0] = slot;
            }
        }

        const std::uint32_t index = static_cast<std::uint32_t>(materials.size());
        materials.push_back(gpu);
        material_indices.emplace(handle, index);
        return index;
    }

    bool buildMeshTriangles(
        const Models::MeshData& mesh,
        const Animation::Pose *pose,
        std::uint32_t gpu_material,
        const Transform *bake_transform,
        std::vector<Triangle> *out,
        std::string *error) const
    {
        if (!out) return false;
        std::vector<Models::Vec3> positions;
        std::vector<Models::Vec3> normals;
        if (pose) {
            positions.resize(mesh.vertices.size());
            normals.resize(mesh.vertices.size());
            for (std::size_t vertex_index = 0u; vertex_index < mesh.vertices.size(); ++vertex_index) {
                const Models::Vertex& vertex = mesh.vertices[vertex_index];
                Animation::Vec3 out_position{};
                Animation::Vec3 out_normal{};
                const bool skinned = Animation::skinVertex(
                    *pose,
                    vertex.skin,
                    {vertex.position.x, vertex.position.y, vertex.position.z},
                    {vertex.normal.x, vertex.normal.y, vertex.normal.z},
                    &out_position,
                    &out_normal
                );
                positions[vertex_index] = skinned
                    ? Models::Vec3{out_position.x, out_position.y, out_position.z}
                    : vertex.position;
                normals[vertex_index] = skinned
                    ? Models::Vec3{out_normal.x, out_normal.y, out_normal.z}
                    : vertex.normal;
            }
        }

        const Mat4 model = bake_transform ? modelMatrix(*bake_transform) : identityMatrix();
        const Mat4 inverse_model = bake_transform ? inverseModelMatrix(*bake_transform) : identityMatrix();
        const std::size_t triangle_count = mesh.indices.size() / 3u;
        if (out->size() + triangle_count > kMaximumTriangles) {
            if (error) *error = "path tracer triangle budget exceeded";
            return false;
        }
        out->reserve(out->size() + triangle_count);

        for (std::size_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
            const std::size_t offset = triangle_index * 3u;
            const std::uint32_t i0 = mesh.indices[offset + 0u];
            const std::uint32_t i1 = mesh.indices[offset + 1u];
            const std::uint32_t i2 = mesh.indices[offset + 2u];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) continue;

            const Models::Vertex& v0 = mesh.vertices[i0];
            const Models::Vertex& v1 = mesh.vertices[i1];
            const Models::Vertex& v2 = mesh.vertices[i2];
            const Models::Vec3 p0_local = pose ? positions[i0] : v0.position;
            const Models::Vec3 p1_local = pose ? positions[i1] : v1.position;
            const Models::Vec3 p2_local = pose ? positions[i2] : v2.position;
            const Models::Vec3 n0_local = pose ? normals[i0] : v0.normal;
            const Models::Vec3 n1_local = pose ? normals[i1] : v1.normal;
            const Models::Vec3 n2_local = pose ? normals[i2] : v2.normal;

            const Float3 p0 = transformPoint(model, {p0_local.x, p0_local.y, p0_local.z});
            const Float3 p1 = transformPoint(model, {p1_local.x, p1_local.y, p1_local.z});
            const Float3 p2 = transformPoint(model, {p2_local.x, p2_local.y, p2_local.z});
            const Float3 n0 = transformNormal(inverse_model, {n0_local.x, n0_local.y, n0_local.z});
            const Float3 n1 = transformNormal(inverse_model, {n1_local.x, n1_local.y, n1_local.z});
            const Float3 n2 = transformNormal(inverse_model, {n2_local.x, n2_local.y, n2_local.z});

            Triangle triangle{};
            triangle.p0 = {p0.x, p0.y, p0.z, 0.0f};
            triangle.p1 = {p1.x, p1.y, p1.z, 0.0f};
            triangle.p2 = {p2.x, p2.y, p2.z, 0.0f};
            triangle.n0 = {n0.x, n0.y, n0.z, 0.0f};
            triangle.n1 = {n1.x, n1.y, n1.z, 0.0f};
            triangle.n2 = {n2.x, n2.y, n2.z, 0.0f};
            triangle.uv01 = {v0.uv.x, v0.uv.y, v1.uv.x, v1.uv.y};
            triangle.uv2 = {v2.uv.x, v2.uv.y, static_cast<float>(gpu_material), 0.0f};
            out->push_back(triangle);
        }
        return true;
    }

    std::vector<PathTracerGpu::Instance> sourceInstances() const
    {
        const std::size_t source_count = (has_static ? 1u : 0u) + dynamic.size();
        std::vector<PathTracerGpu::Instance> source(source_count);
        if (has_static) {
            PathTracerGpu::Instance instance;
            instance.data = {
                static_cast<std::uint32_t>(static_region.node_first),
                static_cast<std::uint32_t>(static_region.node_count),
                static_cast<std::uint32_t>(static_region.triangle_first),
                static_cast<std::uint32_t>(static_region.triangle_count),
            };
            source[0] = instance;
        }
        for (const DynamicEntry& entry : dynamic) {
            PathTracerGpu::Instance instance;
            instance.object_to_world = entry.model;
            instance.world_to_object = entry.inverse_model;
            instance.data = {
                static_cast<std::uint32_t>(entry.region.node_first),
                static_cast<std::uint32_t>(entry.region.node_count),
                static_cast<std::uint32_t>(entry.region.triangle_first),
                static_cast<std::uint32_t>(entry.region.triangle_count),
            };
            source[entry.source_instance] = instance;
        }
        return source;
    }

    bool rebuildGpuInstances(std::string *error)
    {
        const std::vector<PathTracerGpu::Instance> source = sourceInstances();
        if (tlas.instance_order.size() != source.size()) {
            if (error) *error = "TLAS instance order does not match acceleration instances";
            return false;
        }
        instances.resize(source.size());
        for (std::size_t output = 0u; output < tlas.instance_order.size(); ++output) {
            const std::uint32_t source_index = tlas.instance_order[output];
            if (source_index >= source.size()) {
                if (error) *error = "TLAS references an invalid source instance";
                return false;
            }
            instances[output] = source[source_index];
        }
        return true;
    }

    bool fullBuild(const Ecs::World& world, std::uint64_t next_topology, SyncResult& result)
    {
        reset();
        const auto fail = [&](const std::string& message) {
            result.ok = false;
            result.error = message;
            return false;
        };

        std::vector<Triangle> static_triangles;
        static_triangles.reserve(200000u);

        for (const Ecs::Entity entity : world.entities()) {
            const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
            const MeshComponent *mesh_component = world.get<MeshComponent>(entity);
            const Transform *transform = world.get<Transform>(entity);
            if (!renderable || !renderable->visible || !mesh_component || !transform) continue;

            const Models::MeshData *mesh = Models::mesh(mesh_component->mesh);
            if (!mesh || mesh->indices.size() < 3u) continue;
            const Models::MaterialData *material = Models::material(mesh_component->material);
            if (material && material->opacity < 0.5f) continue;

            const std::uint32_t gpu_material = materialIndex(mesh_component->material);
            Ecs::Entity animator_entity = Ecs::INVALID_ENTITY;
            const Animation::Pose *pose = animatedPose(world, entity, &animator_entity);
            if (!pose) {
                if (!buildMeshTriangles(*mesh, nullptr, gpu_material, transform, &static_triangles, &result.error)) {
                    result.ok = false;
                    return false;
                }
                continue;
            }

            std::vector<Triangle> local_triangles;
            if (!buildMeshTriangles(*mesh, pose, gpu_material, nullptr, &local_triangles, &result.error)) {
                result.ok = false;
                return false;
            }
            PathTracerAccel::BuildResult bvh = PathTracerAccel::buildRefittableWideBvh(local_triangles);
            if (!bvh.valid) return fail("dynamic BVH8 build failed: " + bvh.error);

            DynamicEntry entry;
            entry.entity = entity;
            entry.animator = animator_entity;
            entry.mesh = mesh_component->mesh;
            entry.material = mesh_component->material;
            entry.gpu_material = gpu_material;
            entry.pose_revision = pose->revision;
            entry.bvh = std::move(bvh);
            entry.model = modelMatrix(*transform);
            entry.inverse_model = inverseModelMatrix(*transform);
            entry.world_bounds = transformedBounds(entry.bvh.bounds, entry.model);
            dynamic.push_back(std::move(entry));
        }

        if (!static_triangles.empty()) {
            static_blas = PathTracerAccel::buildWideBvh(static_triangles);
            if (!static_blas.valid) return fail("static BVH8 build failed: " + static_blas.error);
            has_static = true;
        }
        if (!has_static && dynamic.empty()) return fail("scene contains no traceable triangles");
        if (materials.empty()) materials.push_back(PathTracerGpu::Material{});

        instance_bounds.clear();
        if (has_static) instance_bounds.push_back(static_blas.bounds);
        for (std::size_t index = 0u; index < dynamic.size(); ++index) {
            dynamic[index].source_instance = instance_bounds.size();
            instance_bounds.push_back(dynamic[index].world_bounds);
        }

        tlas = PathTracerAccel::buildWideTlas(instance_bounds);
        if (!tlas.valid) return fail("TLAS build failed: " + tlas.error);

        nodes = tlas.nodes;
        triangles.clear();
        if (has_static) {
            static_region.node_first = nodes.size();
            static_region.node_count = static_blas.nodes.size();
            static_region.triangle_first = triangles.size();
            static_region.triangle_count = static_blas.triangles.size();
            if (!PathTracerAccel::appendOffsetWideBvh(
                    static_blas,
                    static_cast<std::uint32_t>(static_region.node_first),
                    static_cast<std::uint32_t>(static_region.triangle_first),
                    &nodes,
                    &triangles,
                    &result.error))
            {
                result.ok = false;
                return false;
            }
        }

        for (DynamicEntry& entry : dynamic) {
            entry.region.node_first = nodes.size();
            entry.region.node_count = entry.bvh.nodes.size();
            entry.region.triangle_first = triangles.size();
            entry.region.triangle_count = entry.bvh.triangles.size();
            if (!PathTracerAccel::appendOffsetWideBvh(
                    entry.bvh,
                    static_cast<std::uint32_t>(entry.region.node_first),
                    static_cast<std::uint32_t>(entry.region.triangle_first),
                    &nodes,
                    &triangles,
                    &result.error))
            {
                result.ok = false;
                return false;
            }
        }

        if (triangles.size() > kMaximumTriangles) return fail("path tracer triangle budget exceeded");
        if (!rebuildGpuInstances(&result.error)) {
            result.ok = false;
            return false;
        }

        topology_signature = next_topology;
        pose_signature = poseSignature(world);
        transform_signature = transformSignature(world);
        built = true;
        ++topology_revision;

        result.full_upload = true;
        result.instances_dirty = true;
        result.invalidate_history = true;
        result.clear_radiance_cache = true;
        return true;
    }

    bool incrementalUpdate(const Ecs::World& world, SyncResult& result)
    {
        const std::uint64_t next_pose = poseSignature(world);
        const std::uint64_t next_transform = transformSignature(world);
        const bool pose_changed = next_pose != pose_signature;
        const bool transform_changed = next_transform != transform_signature;
        if (!pose_changed && !transform_changed) return true;

        if (pose_changed) {
            for (DynamicEntry& entry : dynamic) {
                const Animation::Pose *pose = animatedPose(world, entry.entity);
                const Models::MeshData *mesh = Models::mesh(entry.mesh);
                if (!pose || !mesh) {
                    result.ok = false;
                    result.error = "dynamic path tracer entity lost its animation pose or mesh";
                    return false;
                }
                if (pose->revision == entry.pose_revision) continue;

                std::vector<Triangle> source;
                if (!buildMeshTriangles(*mesh, pose, entry.gpu_material, nullptr, &source, &result.error)) {
                    result.ok = false;
                    return false;
                }
                if (!PathTracerAccel::refitWideBvh(
                        &entry.bvh,
                        source,
                        PathTracerAccel::sourceTriangleOrder(entry.bvh),
                        &result.error))
                {
                    result.ok = false;
                    return false;
                }
                if (!writePackedBlas(
                        entry.bvh,
                        entry.region.node_first,
                        entry.region.triangle_first,
                        nodes,
                        triangles,
                        &result.error))
                {
                    result.ok = false;
                    return false;
                }

                entry.pose_revision = pose->revision;
                result.node_ranges.push_back({entry.region.node_first, entry.region.node_count});
                result.triangle_ranges.push_back({entry.region.triangle_first, entry.region.triangle_count});
            }
        }

        if (transform_changed) {
            for (DynamicEntry& entry : dynamic) {
                const Transform *transform = world.get<Transform>(entry.entity);
                if (!transform) {
                    result.ok = false;
                    result.error = "dynamic path tracer entity lost its transform";
                    return false;
                }
                entry.model = modelMatrix(*transform);
                entry.inverse_model = inverseModelMatrix(*transform);
            }
            result.instances_dirty = true;
        }

        for (DynamicEntry& entry : dynamic) {
            entry.world_bounds = transformedBounds(entry.bvh.bounds, entry.model);
            if (entry.source_instance >= instance_bounds.size()) {
                result.ok = false;
                result.error = "dynamic instance index escaped TLAS source array";
                return false;
            }
            instance_bounds[entry.source_instance] = entry.world_bounds;
        }

        if (!PathTracerAccel::refitWideTlas(&tlas, instance_bounds, &result.error)) {
            result.ok = false;
            return false;
        }
        if (tlas.nodes.size() > nodes.size()) {
            result.ok = false;
            result.error = "refitted TLAS no longer fits packed node prefix";
            return false;
        }
        std::copy(tlas.nodes.begin(), tlas.nodes.end(), nodes.begin());
        result.node_ranges.push_back({0u, tlas.nodes.size()});

        if (transform_changed && !rebuildGpuInstances(&result.error)) {
            result.ok = false;
            return false;
        }

        pose_signature = next_pose;
        transform_signature = next_transform;
        result.invalidate_history = true;
        return true;
    }
};

SceneCache::SceneCache() : impl_(new Impl) {}

SceneCache::~SceneCache()
{
    delete impl_;
    impl_ = nullptr;
}

SyncResult SceneCache::sync(const Ecs::World& world)
{
    SyncResult result;
    const std::uint64_t next_topology = impl_->topologySignature(world);
    if (!impl_->built || next_topology != impl_->topology_signature) {
        impl_->fullBuild(world, next_topology, result);
        return result;
    }
    impl_->incrementalUpdate(world, result);
    return result;
}

void SceneCache::clear()
{
    impl_->reset();
}

const std::vector<WideNode>& SceneCache::nodes() const { return impl_->nodes; }
const std::vector<Triangle>& SceneCache::triangles() const { return impl_->triangles; }
const std::vector<PathTracerGpu::Instance>& SceneCache::instances() const { return impl_->instances; }
const std::vector<PathTracerGpu::Material>& SceneCache::materials() const { return impl_->materials; }
const std::array<Models::TextureHandle, 16>& SceneCache::textureHandles() const { return impl_->texture_handles; }
std::size_t SceneCache::tlasNodeCount() const { return impl_->tlas.nodes.size(); }
std::uint64_t SceneCache::topologyRevision() const { return impl_->topology_revision; }

} // namespace Renderer::PathTracerScene
