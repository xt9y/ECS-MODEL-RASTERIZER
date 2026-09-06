#ifndef RW_ENGINE_MODELS_FORMATS_FBX_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_HPP

#include "Models/Models.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace Models::Fbx {

struct Part {
    MeshData mesh;
    MaterialData material;
};

struct Document {
    std::vector<Part> parts;
    bool has_skeleton = false;
    std::size_t animation_count = 0u;
};

bool load(const std::string& path, Document *document, std::string *error = nullptr);

} // namespace Models::Fbx

#endif
