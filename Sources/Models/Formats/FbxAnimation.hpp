#ifndef HORSE_MODELS_FORMATS_FBX_ANIMATION_HPP
#define HORSE_MODELS_FORMATS_FBX_ANIMATION_HPP

#include "Models/Formats/FbxSkin.hpp"

#include <string>
#include <vector>

namespace Models::FbxInternal {

bool makeAnimations(
    const Scene& scene,
    const SkeletonBuild& skeleton,
    std::vector<Animation::AnimationClip> *out,
    std::string *error);

} // namespace Models::FbxInternal

#endif
