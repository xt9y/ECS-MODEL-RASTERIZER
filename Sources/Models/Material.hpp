#ifndef RW_ENGINE_MODELS_MATERIAL_HPP
#define RW_ENGINE_MODELS_MATERIAL_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Texture.hpp"

#include <string>
#include <unordered_map>

namespace Models {

struct MaterialData {
    std::string name;
    Ecs::Vec3 color {1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    std::string texture_path;
    TextureHandle diffuse_texture = INVALID_TEXTURE;
};

using MaterialMap = std::unordered_map<std::string, MaterialData>;

bool loadMaterialLibrary(
    const std::string& path,
    MaterialMap *materials,
    std::string *error = nullptr
);

} // namespace Models

#endif
