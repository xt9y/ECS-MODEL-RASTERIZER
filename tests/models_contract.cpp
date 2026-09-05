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
    const auto model = Models::load(path, &error);
    assert(model != Models::INVALID_MODEL);
    assert(error.empty());
    assert(Models::partCount(model) == 1u);

    Ecs::World world;
    const auto spawned = Models::spawn(world, model, {}, &error);
    assert(spawned.size() == 1u);
    const auto *mesh_component = world.getMesh(spawned.front());
    assert(mesh_component);
    const auto *mesh = Models::mesh(mesh_component->mesh);
    assert(mesh);
    assert(mesh->vertices.size() == 3u);
    assert(mesh->indices.size() == 3u);

    Models::clearCache();
    error.clear();
    const auto default_model = Models::load("Assets/default.obj", &error);
    assert(default_model != Models::INVALID_MODEL);
    assert(error.empty());
    assert(Models::partCount(default_model) == 1u);
    return 0;
}
