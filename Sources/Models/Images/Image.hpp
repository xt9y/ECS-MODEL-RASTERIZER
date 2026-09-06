#ifndef RW_ENGINE_MODELS_IMAGES_IMAGE_HPP
#define RW_ENGINE_MODELS_IMAGES_IMAGE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Images {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool meaningful_alpha = false;
};

bool load(const std::string& path, Image *image, std::string *error = nullptr);
bool loadMemory(
    const void *data,
    std::size_t size,
    Image *image,
    std::string *error = nullptr
);

} // namespace Models::Images

#endif
