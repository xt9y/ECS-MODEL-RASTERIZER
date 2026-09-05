#ifndef RW_ENGINE_MODELS_OBJ_HPP
#define RW_ENGINE_MODELS_OBJ_HPP

#include "Models/Models.hpp"

#include <string>
#include <vector>

namespace Models::Obj {

struct Part {
    MeshData mesh;
    MaterialData material;
};

struct Document {
    std::vector<Part> parts;
};

bool load(const std::string& path, Document *document, std::string *error = nullptr);

} // namespace Models::Obj

#endif
