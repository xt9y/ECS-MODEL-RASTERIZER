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

    const auto *part = Models::part(model, 0u);
    assert(part);
    const auto *mesh = Models::mesh(part->mesh);
    assert(mesh);
    assert(mesh->vertices.size() == 3u);
    assert(mesh->indices.size() == 3u);

    Models::clearCache();
    error.clear();
    const auto default_model = Models::load("Assets/Sponza/sponza.obj", &error);
    assert(default_model != Models::INVALID_MODEL);
    assert(error.empty());
    assert(Models::partCount(default_model) > 0u);
    return 0;
}
