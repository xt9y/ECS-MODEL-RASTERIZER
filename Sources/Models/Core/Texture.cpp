#include "Models/Core/Texture.hpp"

#include <filesystem>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Models {
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

TextureHandle store(
    std::string key,
    Images::Image image)
{
    const TextureHandle handle = static_cast<TextureHandle>(assets().size());
    assets().push_back({key, std::move(image)});
    cache().emplace(std::move(key), handle);
    return handle;
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

    Images::Image image;
    if (!Images::load(key, &image, error)) return INVALID_TEXTURE;
    return store(key, std::move(image));
}

TextureHandle loadTextureMemory(
    const std::string& cache_key,
    const void *data,
    std::size_t size,
    std::string *error)
{
    if (error) error->clear();
    if (cache_key.empty()) {
        if (error) *error = "empty embedded texture cache key";
        return INVALID_TEXTURE;
    }

    const auto found = cache().find(cache_key);
    if (found != cache().end()) return found->second;

    Images::Image image;
    if (!Images::loadMemory(data, size, &image, error)) return INVALID_TEXTURE;
    return store(cache_key, std::move(image));
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

} // namespace Models
