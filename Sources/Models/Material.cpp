#include "Models/Material.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace RW::Models {
namespace {

std::string textureValue(const std::string& value)
{
    if (value.empty() || value[0] != '-') return value;

    std::istringstream stream(value);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) tokens.push_back(token);
    return tokens.empty() ? std::string{} : tokens.back();
}

} // namespace

bool loadMaterialLibrary(
    const std::string& path,
    MaterialMap *materials,
    std::string *error)
{
    if (error) error->clear();
    if (!materials) {
        if (error) *error = "null material destination";
        return false;
    }
    materials->clear();

    std::ifstream input(path);
    if (!input) {
        if (error) *error = "cannot open material library: " + path;
        return false;
    }

    const std::filesystem::path material_path(path);
    MaterialData *current = nullptr;
    std::string line;

    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string key;
        stream >> key;
        if (key.empty() || key[0] == '#') continue;

        if (key == "newmtl") {
            std::string name;
            std::getline(stream >> std::ws, name);
            if (name.empty()) continue;
            current = &(*materials)[name];
            current->name = name;
            continue;
        }
        if (!current) continue;

        if (key == "Kd") {
            stream >> current->color.x >> current->color.y >> current->color.z;
        } else if (key == "d") {
            stream >> current->opacity;
            current->opacity = std::clamp(current->opacity, 0.0f, 1.0f);
        } else if (key == "Tr") {
            float transparency = 0.0f;
            stream >> transparency;
            current->opacity = 1.0f - std::clamp(transparency, 0.0f, 1.0f);
        } else if (key == "map_Kd") {
            std::string value;
            std::getline(stream >> std::ws, value);
            value = textureValue(value);
            if (value.empty()) continue;

            const std::filesystem::path texture_path =
                (material_path.parent_path() / value).lexically_normal();
            current->texture_path = texture_path.string();

            std::string texture_error;
            current->diffuse_texture = loadTexture(current->texture_path, &texture_error);
        }
    }

    return true;
}

} // namespace RW::Models
