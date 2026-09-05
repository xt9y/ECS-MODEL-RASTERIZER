#ifndef RW_ENGINE_MODELS_HPP
#define RW_ENGINE_MODELS_HPP

#include "Ecs/Ecs.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RW::Models {

using ModelHandle = std::uint32_t;
using MeshHandle = std::uint32_t;
using MaterialHandle = std::uint32_t;

constexpr ModelHandle INVALID_MODEL = UINT32_MAX;
constexpr MeshHandle INVALID_MESH = UINT32_MAX;
constexpr MaterialHandle INVALID_MATERIAL = UINT32_MAX;

struct Vertex {
    Ecs::Vec3 position;
    Ecs::Vec3 normal {0.0f, 0.0f, 1.0f};
    Ecs::Vec2 uv;
};

struct Bounds {
    Ecs::Vec3 minimum;
    Ecs::Vec3 maximum;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Bounds bounds;
};

struct MaterialData {
    std::string name;
    Ecs::Vec3 color {1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    std::string texture_path;
};

struct SpawnOptions {
    Ecs::TransformComponent transform {};
    bool visible = true;
};

ModelHandle load(const std::string& path, std::string *error = nullptr);
std::vector<Ecs::Entity> spawn(
    Ecs::World& world,
    ModelHandle model,
    const SpawnOptions& options = {},
    std::string *error = nullptr
);
std::vector<Ecs::Entity> loadInto(
    Ecs::World& world,
    const std::string& path,
    const SpawnOptions& options = {},
    std::string *error = nullptr
);

const MeshData *mesh(MeshHandle handle);
const MaterialData *material(MaterialHandle handle);
std::size_t partCount(ModelHandle model);
void clearCache();

} // namespace RW::Models

#endif
