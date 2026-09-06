#include "Models/Formats/Fbx.hpp"

#include "Models/Core/Texture.hpp"
#include "Models/ThirdParty/Ufbx.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Models::Fbx {
namespace {

std::string toString(ufbx_string value)
{
    return value.data && value.length > 0u
        ? std::string(value.data, value.length)
        : std::string{};
}

Vec3 toVec3(ufbx_vec3 value)
{
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z),
    };
}

Vec2 toVec2(ufbx_vec2 value)
{
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
    };
}

Vec3 normalize(Vec3 value)
{
    const float length_squared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (length_squared <= 1.0e-20f) return {0.0f, 0.0f, 1.0f};
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
    };
}

Bounds calculateBounds(const std::vector<Vertex>& vertices)
{
    if (vertices.empty()) return {};

    const float maximum = std::numeric_limits<float>::max();
    Bounds bounds {{maximum, maximum, maximum}, {-maximum, -maximum, -maximum}};
    for (const Vertex& vertex : vertices) {
        bounds.minimum.x = std::min(bounds.minimum.x, vertex.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.position.z);
    }
    return bounds;
}

const ufbx_material *materialFor(
    const ufbx_node *node,
    const ufbx_mesh *mesh,
    std::uint32_t index)
{
    if (node && index < node->materials.count) return node->materials.data[index];
    if (index < mesh->materials.count) return mesh->materials.data[index];
    return nullptr;
}

const ufbx_texture *baseColorTexture(const ufbx_material *material)
{
    if (!material) return nullptr;
    if (material->pbr.base_color.texture) return material->pbr.base_color.texture;
    return material->fbx.diffuse_color.texture;
}

MaterialData convertMaterial(
    const ufbx_material *material,
    const std::filesystem::path& directory)
{
    MaterialData result;
    if (!material) return result;

    result.name = toString(material->name);

    if (material->pbr.base_color.has_value) {
        const ufbx_vec4 value = material->pbr.base_color.value_vec4;
        result.color = {
            static_cast<float>(value.x),
            static_cast<float>(value.y),
            static_cast<float>(value.z),
        };
    } else if (material->fbx.diffuse_color.has_value) {
        result.color = toVec3(material->fbx.diffuse_color.value_vec3);
    }

    if (material->pbr.opacity.has_value) {
        result.opacity = std::clamp(
            static_cast<float>(material->pbr.opacity.value_vec3.x),
            0.0f,
            1.0f
        );
    } else if (material->fbx.transparency_factor.has_value) {
        result.opacity = 1.0f - std::clamp(
            static_cast<float>(material->fbx.transparency_factor.value_vec3.x),
            0.0f,
            1.0f
        );
    }

    const ufbx_texture *texture = baseColorTexture(material);
    if (texture) {
        std::string filename = toString(texture->relative_filename);
        if (filename.empty()) filename = toString(texture->filename);
        if (!filename.empty()) {
            std::filesystem::path texture_path(filename);
            if (texture_path.is_relative()) texture_path = directory / texture_path;
            texture_path = texture_path.lexically_normal();
            result.texture_path = texture_path.string();

            std::string extension = texture_path.extension().string();
            std::transform(
                extension.begin(),
                extension.end(),
                extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
            );
            if (extension == ".tga") {
                std::string ignored_error;
                result.diffuse_texture = loadTexture(result.texture_path, &ignored_error);
            }
        }
    }

    return result;
}

struct Builder {
    const ufbx_material *source_material = nullptr;
    MeshData mesh;
};

} // namespace

bool load(const std::string& path, Document *document, std::string *error)
{
    if (error) error->clear();
    if (!document) {
        if (error) *error = "null FBX destination";
        return false;
    }
    *document = {};

    ufbx_load_opts options{};
    options.generate_missing_normals = true;
    options.normalize_normals = true;
    options.ignore_missing_external_files = true;

    ufbx_error load_error{};
    ufbx_scene *scene = ufbx_load_file(path.c_str(), &options, &load_error);
    if (!scene) {
        if (error) {
            char buffer[1024]{};
            ufbx_format_error(buffer, sizeof(buffer), &load_error);
            *error = "cannot load FBX: " + path + ": " + buffer;
        }
        return false;
    }

    document->has_skeleton = scene->bones.count > 0u || scene->skin_deformers.count > 0u;
    document->animation_count = scene->anim_stacks.count;
    const std::filesystem::path directory = std::filesystem::path(path).parent_path();

    for (std::size_t mesh_index = 0u; mesh_index < scene->meshes.count; ++mesh_index) {
        const ufbx_mesh *mesh = scene->meshes.data[mesh_index];
        if (!mesh || mesh->num_faces == 0u) continue;

        const std::size_t triangle_index_capacity =
            std::max<std::size_t>(static_cast<std::size_t>(mesh->max_face_triangles) * 3u, 3u);
        std::vector<std::uint32_t> triangle_indices(triangle_index_capacity);

        for (std::size_t instance_index = 0u; instance_index < mesh->instances.count; ++instance_index) {
            const ufbx_node *node = mesh->instances.data[instance_index];
            if (!node || !node->visible) continue;

            const std::size_t material_count = std::max<std::size_t>(
                std::max(node->materials.count, mesh->materials.count),
                1u
            );
            std::vector<Builder> builders(material_count);
            for (std::size_t i = 0u; i < material_count; ++i) {
                builders[i].source_material = materialFor(
                    node,
                    mesh,
                    static_cast<std::uint32_t>(i)
                );
            }

            const ufbx_matrix normal_to_world = ufbx_matrix_for_normals(&node->geometry_to_world);

            for (std::size_t face_index = 0u; face_index < mesh->faces.count; ++face_index) {
                if (mesh->face_hole.count > face_index && mesh->face_hole.data[face_index]) continue;

                const ufbx_face face = mesh->faces.data[face_index];
                std::uint32_t material_index = 0u;
                if (mesh->face_material.count > face_index) {
                    material_index = mesh->face_material.data[face_index];
                }
                if (material_index >= builders.size()) material_index = 0u;
                Builder& builder = builders[material_index];

                const std::uint32_t triangle_index_count = ufbx_triangulate_face(
                    triangle_indices.data(),
                    triangle_indices.size(),
                    mesh,
                    face
                );

                for (std::uint32_t i = 0u; i < triangle_index_count; ++i) {
                    const std::uint32_t index = triangle_indices[i];
                    const ufbx_vec3 local_position = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                    const ufbx_vec3 local_normal = mesh->vertex_normal.exists
                        ? ufbx_get_vertex_vec3(&mesh->vertex_normal, index)
                        : ufbx_zero_vec3;
                    const ufbx_vec2 uv = mesh->vertex_uv.exists
                        ? ufbx_get_vertex_vec2(&mesh->vertex_uv, index)
                        : ufbx_zero_vec2;

                    const ufbx_vec3 world_position =
                        ufbx_transform_position(&node->geometry_to_world, local_position);
                    const ufbx_vec3 world_normal =
                        ufbx_transform_direction(&normal_to_world, local_normal);

                    Vertex vertex;
                    vertex.position = toVec3(world_position);
                    vertex.normal = normalize(toVec3(world_normal));
                    vertex.uv = toVec2(uv);
                    builder.mesh.indices.push_back(
                        static_cast<std::uint32_t>(builder.mesh.vertices.size())
                    );
                    builder.mesh.vertices.push_back(vertex);
                }
            }

            for (Builder& builder : builders) {
                if (builder.mesh.indices.empty()) continue;
                builder.mesh.bounds = calculateBounds(builder.mesh.vertices);
                document->parts.push_back({
                    std::move(builder.mesh),
                    convertMaterial(builder.source_material, directory),
                });
            }
        }
    }

    ufbx_free_scene(scene);

    if (document->parts.empty()) {
        if (error) *error = "FBX contains no renderable mesh instances: " + path;
        return false;
    }

    return true;
}

} // namespace Models::Fbx
