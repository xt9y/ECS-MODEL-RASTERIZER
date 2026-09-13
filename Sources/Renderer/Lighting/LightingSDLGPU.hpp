#ifndef HORSE_RENDERER_LIGHTING_LIGHTING_SDLGPU_HPP
#define HORSE_RENDERER_LIGHTING_LIGHTING_SDLGPU_HPP

#include "Renderer/Lighting/Lighting.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstdint>

namespace Renderer::Internal {

bool bindLightingSDLGPU(
    SDL_GPURenderPass *pass,
    const Lighting::State& lighting,
    std::uint32_t slot
);
bool bindLightingSDLGPU(
    SDL_GPUComputePass *pass,
    const Lighting::State& lighting,
    std::uint32_t slot
);
void shutdownLightingSDLGPU();

} // namespace Renderer::Internal

#endif
