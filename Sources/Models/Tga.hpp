#ifndef RW_ENGINE_MODELS_TGA_HPP
#define RW_ENGINE_MODELS_TGA_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace RW::Models::Tga {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool meaningful_alpha = false;
};

bool load(const std::string& path, Image *image, std::string *error = nullptr);

} // namespace RW::Models::Tga

#endif
