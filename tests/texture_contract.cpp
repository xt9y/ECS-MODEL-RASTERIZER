#include "Sources/Models/Texture.hpp"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>

int main()
{
    const char *path = "/tmp/rwengine_texture_test.tga";
    const std::uint8_t tga[] = {
        0, 0, 2,
        0, 0, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 1, 0,
        24, 0x20,
        0, 0, 255,
    };

    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(tga), sizeof(tga));
    }

    std::string error;
    const auto handle = Models::loadTexture(path, &error);
    assert(handle != Models::INVALID_TEXTURE);
    assert(error.empty());

    const auto *asset = Models::texture(handle);
    assert(asset);
    assert(asset->image.width == 1);
    assert(asset->image.height == 1);
    assert(asset->image.rgba.size() == 4u);
    assert(asset->image.rgba[0] == 255u);
    assert(asset->image.rgba[1] == 0u);
    assert(asset->image.rgba[2] == 0u);
    assert(asset->image.rgba[3] == 255u);
    return 0;
}
