#ifndef HORSE_RENDERER_LIGHTING_LIGHTING_HPP
#define HORSE_RENDERER_LIGHTING_LIGHTING_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <cstdint>
#include <vector>

namespace Renderer::Lighting {

struct State {
    std::vector<Scenes::LightState> lights;
    std::uint64_t revision = 0u;
};

State state(const Ecs::World& world);
std::uint64_t signature(const State& state);

} // namespace Renderer::Lighting

#endif
