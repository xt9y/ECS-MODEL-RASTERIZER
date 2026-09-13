#include "Renderer/Lighting/LightingSDLGPU.hpp"

#include "Renderer/SDLGPU/Context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace Renderer::Internal {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr std::size_t HeaderVec4Count = 1u;
constexpr std::size_t LightVec4Count = 5u;

SDL_GPUBuffer *buffer = nullptr;
std::size_t capacity = 0u;
std::uint64_t uploaded_revision = std::numeric_limits<std::uint64_t>::max();

bool upload(const Lighting::State& lighting)
{
    if (!Renderer::SDLGPU::device()) return false;
    if (buffer && uploaded_revision == lighting.revision) return true;

    const std::size_t vec4_count = HeaderVec4Count + lighting.lights.size() * LightVec4Count;
    std::vector<std::array<float, 4>> data(vec4_count);
    data[0][0] = static_cast<float>(lighting.lights.size());

    for (std::size_t index = 0u; index < lighting.lights.size(); ++index) {
        const Scenes::LightState& light = lighting.lights[index];
        const std::size_t offset = HeaderVec4Count + index * LightVec4Count;
        const float inner = std::cos(light.inner_cone_degrees * (Pi / 180.0f));
        const float outer = std::cos(light.outer_cone_degrees * (Pi / 180.0f));
        const float type = light.type == LightType::Point
            ? 1.0f
            : (light.type == LightType::Directional ? 2.0f : 3.0f);

        data[offset + 0u] = {
            light.position.x, light.position.y, light.position.z,
            std::max(light.intensity, 0.0f)};
        data[offset + 1u] = {
            light.direction.x, light.direction.y, light.direction.z, type};
        data[offset + 2u] = {
            light.color.x, light.color.y, light.color.z,
            std::max(light.range, 0.0f)};
        data[offset + 3u] = {
            inner, outer,
            light.shadows ? 1.0f : 0.0f,
            std::max(light.shadow_bias, 0.0f)};
        data[offset + 4u] = {
            std::max(light.volumetric_intensity, 0.0f),
            light.volumetric ? 1.0f : 0.0f,
            0.0f, 0.0f};
    }

    const std::size_t bytes = data.size() * sizeof(data.front());
    if (!buffer || capacity < bytes) {
        SDL_GPUBuffer *replacement = Renderer::SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
                SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
            bytes,
            data.data(),
            "Horse Lighting");
        if (!replacement) return false;
        if (buffer) SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), buffer);
        buffer = replacement;
        capacity = bytes;
    } else {
        SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(Renderer::SDLGPU::device());
        if (!command || !Renderer::SDLGPU::uploadBuffer(
                command, buffer, data.data(), bytes, true) ||
            !SDL_SubmitGPUCommandBuffer(command))
        {
            if (command) SDL_CancelGPUCommandBuffer(command);
            return false;
        }
    }

    uploaded_revision = lighting.revision;
    return true;
}

} // namespace

bool bindLightingSDLGPU(
    SDL_GPURenderPass *pass,
    const Lighting::State& lighting,
    std::uint32_t slot)
{
    if (!pass || !upload(lighting)) return false;
    SDL_GPUBuffer *buffers[] = {buffer};
    SDL_BindGPUFragmentStorageBuffers(pass, slot, buffers, 1u);
    return true;
}

bool bindLightingSDLGPU(
    SDL_GPUComputePass *pass,
    const Lighting::State& lighting,
    std::uint32_t slot)
{
    if (!pass || !upload(lighting)) return false;
    SDL_GPUBuffer *buffers[] = {buffer};
    SDL_BindGPUComputeStorageBuffers(pass, slot, buffers, 1u);
    return true;
}

void shutdownLightingSDLGPU()
{
    if (buffer && Renderer::SDLGPU::device())
        SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), buffer);
    buffer = nullptr;
    capacity = 0u;
    uploaded_revision = std::numeric_limits<std::uint64_t>::max();
}

} // namespace Renderer::Internal
