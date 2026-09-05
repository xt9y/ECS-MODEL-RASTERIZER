#include "Core.h"

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    RW::Engine engine;
    std::string error;

    if (!engine.open({}, &error)) {
        std::fprintf(stderr, "RW-Engine: %s\n", error.c_str());
        return 2;
    }

    const char *model_path = argc > 1 ? argv[1] : "Assets/default.obj";
    if (engine.loadModel(model_path, {}, &error) == RW::Models::INVALID_MODEL) {
        std::fprintf(stderr, "RW-Engine: %s\n", error.c_str());
        return 3;
    }

    return engine.run();
}
