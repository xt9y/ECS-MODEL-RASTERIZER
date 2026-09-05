#include "Sources/Models/Material.hpp"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>

int main()
{
    const char *texture_path = "/tmp/rwengine_material_test.tga";
    const char *material_path = "/tmp/rwengine_material_test.mtl";

    const std::uint8_t tga[] = {
        0, 0, 2,
        0, 0, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 1, 0,
        24, 0x20,
        255, 255, 255,
    };

    {
        std::ofstream out(texture_path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(tga), sizeof(tga));
    }
    {
        std::ofstream out(material_path);
        out << "newmtl base\n"
               "Kd 0.25 0.5 0.75\n"
               "d 0.8\n"
               "map_Kd rwengine_material_test.tga\n";
    }

    Models::MaterialMap materials;
    std::string error;
    assert(Models::loadMaterialLibrary(material_path, &materials, &error));
    assert(error.empty());
    assert(materials.size() == 1u);

    const auto found = materials.find("base");
    assert(found != materials.end());
    assert(found->second.color.x == 0.25f);
    assert(found->second.color.y == 0.5f);
    assert(found->second.color.z == 0.75f);
    assert(found->second.opacity == 0.8f);
    assert(found->second.diffuse_texture != Models::INVALID_TEXTURE);
    return 0;
}
