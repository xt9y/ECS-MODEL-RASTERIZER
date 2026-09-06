#ifndef RW_ENGINE_RENDERER_PATHTRACER_GPU_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_GPU_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace Renderer::PathTracerGpu {

struct alignas(16) Ray {
    std::array<float, 4> origin{};
    std::array<float, 4> direction{};
    // x = source/output pixel, y = RNG state, z/w reserved.
    std::array<std::uint32_t, 4> data{};
};

struct alignas(16) Surface {
    // xyz position, w linear distance/depth; depth < 0 means miss.
    std::array<float, 4> position_depth{};
    // xyz normal, w material index encoded as float.
    std::array<float, 4> normal_material{};
    // xy UV, z/w reserved.
    std::array<float, 4> uv{};
    // RGB exact direct-light contribution, w visibility/history marker.
    std::array<float, 4> direct{};
};

struct alignas(16) Reservoir {
    // xyz secondary sample position, w valid flag.
    std::array<float, 4> sample_position{};
    // xyz secondary normal, w source PDF/metadata.
    std::array<float, 4> sample_normal{};
    // RGB indirect contribution carried by selected sample, w target luminance.
    std::array<float, 4> radiance{};
    // x weight sum, y selected target, z effective M, w age.
    std::array<float, 4> weights{};
};

struct alignas(16) Lighting {
    std::array<float, 4> color{};
};

struct alignas(16) Moments {
    // x first luminance moment, y second moment, z variance, w history length.
    std::array<float, 4> value{};
};

struct alignas(16) QueueCounters {
    std::uint32_t hit_count = 0u;
    std::uint32_t bounce_count = 0u;
    std::uint32_t reserved0 = 0u;
    std::uint32_t reserved1 = 0u;
};

struct alignas(16) DispatchCommand {
    std::uint32_t groups_x = 0u;
    std::uint32_t groups_y = 1u;
    std::uint32_t groups_z = 1u;
    std::uint32_t padding = 0u;
};

struct alignas(16) DispatchCommands {
    DispatchCommand hits{};
    DispatchCommand bounces{};
};

struct alignas(16) RadianceCacheEntry {
    // x key (0 = empty), y sample count, z age/reserved, w reserved.
    std::array<std::uint32_t, 4> header{};
    // x/y/z fixed-point RGB sums, w reserved.
    std::array<std::uint32_t, 4> radiance{};
};

static_assert(sizeof(Ray) == 48u);
static_assert(sizeof(Surface) == 64u);
static_assert(sizeof(Reservoir) == 64u);
static_assert(sizeof(Lighting) == 16u);
static_assert(sizeof(Moments) == 16u);
static_assert(sizeof(QueueCounters) == 16u);
static_assert(sizeof(DispatchCommand) == 16u);
static_assert(sizeof(DispatchCommands) == 32u);
static_assert(sizeof(RadianceCacheEntry) == 32u);

constexpr std::size_t RADIANCE_CACHE_ENTRIES = 65536u;

} // namespace Renderer::PathTracerGpu

#endif
