#ifndef RW_ENGINE_MODELS_TEXTURE_HPP
#define RW_ENGINE_MODELS_TEXTURE_HPP

#include "Models/Tga.hpp"

#include <cstdint>
#include <string>

namespace RW::Models {

using TextureHandle = std::uint32_t;
constexpr TextureHandle INVALID_TEXTURE = UINT32_MAX;

struct TextureAsset {
    std::string path;
    Tga::Image image;
};

TextureHandle loadTexture(const std::string& path, std::string *error = nullptr);
const TextureAsset *texture(TextureHandle handle);
void clearTextureCache();

} // namespace RW::Models

#endif
