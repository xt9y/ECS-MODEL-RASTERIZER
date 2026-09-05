#ifndef RW_ENGINE_ENGINE_HPP
#define RW_ENGINE_ENGINE_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Models.hpp"

#include <memory>
#include <string>

namespace RW {

struct EngineConfig {
    int width = 1280;
    int height = 720;
    const char *title = "RW-Engine";
    bool vsync = true;
};

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    bool open(const EngineConfig& config = {}, std::string *error = nullptr);
    void close();

    int run();
    void stop();

    Models::ModelHandle loadModel(
        const std::string& path,
        const Models::SpawnOptions& options = {},
        std::string *error = nullptr
    );

    Ecs::World& world();
    const Ecs::World& world() const;
    bool isOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace RW

#endif
