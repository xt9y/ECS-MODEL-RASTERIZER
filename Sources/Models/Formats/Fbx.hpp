#ifndef RW_ENGINE_MODELS_FORMATS_FBX_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_HPP

#include "Animation/Animation.hpp"
#include "Models/Models.hpp"

#include <string>
#include <vector>

namespace Models::Fbx {

struct Part {
    MeshData mesh;
    MaterialData material;
};

struct Document {
    std::vector<Part> parts;
    Animation::Skeleton skeleton;
    std::vector<Animation::AnimationClip> animations;
    bool has_skeleton = false;
};

bool load(const std::string& path, Document *document, std::string *error = nullptr);

} // namespace Models::Fbx

#endif
