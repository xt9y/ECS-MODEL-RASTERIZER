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

    if (argc > 1 && engine.loadModel(argv[1], {}, &error) == RW::Models::INVALID_MODEL) {
        std::fprintf(stderr, "RW-Engine: %s\n", error.c_str());
        return 3;
    }

    return engine.run();
}
