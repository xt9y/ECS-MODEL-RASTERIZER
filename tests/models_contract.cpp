#include "Sources/Models/Models.hpp"
#include <cassert>
#include <fstream>

int main()
{
    const char *path = "/tmp/rwengine_test.obj";
    {
        std::ofstream out(path);
        out << "v -1 0 0\n"
               "v 1 0 0\n"
               "v 0 1 0\n"
               "f 1 2 3\n";
    }

    std::string error;
    const auto model = RW::Models::load(path, &error);
    assert(model != RW::Models::INVALID_MODEL);
    assert(error.empty());
    assert(RW::Models::partCount(model) == 1u);

    RW::Ecs::World world;
    const auto spawned = RW::Models::spawn(world, model, {}, &error);
    assert(spawned.size() == 1u);
    const auto *mesh_component = world.getMesh(spawned.front());
    assert(mesh_component);
    const auto *mesh = RW::Models::mesh(mesh_component->mesh);
    assert(mesh);
    assert(mesh->vertices.size() == 3u);
    assert(mesh->indices.size() == 3u);
    return 0;
}
