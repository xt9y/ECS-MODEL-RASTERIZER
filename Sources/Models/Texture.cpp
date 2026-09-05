#include "Models/Texture.hpp"

#include <filesystem>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RW::Models {
namespace {

std::vector<TextureAsset>& assets()
{
    static std::vector<TextureAsset> values;
    return values;
}

std::unordered_map<std::string, TextureHandle>& cache()
{
    static std::unordered_map<std::string, TextureHandle> values;
    return values;
}

std::string normalizedPath(const std::string& path)
{
    std::error_code error;
    const std::filesystem::path absolute =
        std::filesystem::absolute(std::filesystem::path(path), error);
    return (error ? std::filesystem::path(path) : absolute)
        .lexically_normal()
        .string();
}

} // namespace

TextureHandle loadTexture(const std::string& path, std::string *error)
{
    if (error) error->clear();
    if (path.empty()) {
        if (error) *error = "empty texture path";
        return INVALID_TEXTURE;
    }

    const std::string key = normalizedPath(path);
    const auto found = cache().find(key);
    if (found != cache().end()) return found->second;

    Tga::Image image;
    if (!Tga::load(key, &image, error)) return INVALID_TEXTURE;

    const TextureHandle handle = static_cast<TextureHandle>(assets().size());
    assets().push_back({key, std::move(image)});
    cache().emplace(key, handle);
    return handle;
}

const TextureAsset *texture(TextureHandle handle)
{
    return handle < assets().size() ? &assets()[handle] : nullptr;
}

void clearTextureCache()
{
    cache().clear();
    assets().clear();
}

} // namespace RW::Models
