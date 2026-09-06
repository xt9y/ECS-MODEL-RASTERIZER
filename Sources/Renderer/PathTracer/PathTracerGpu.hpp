#ifndef RW_ENGINE_RENDERER_PATHTRACER_GPU_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_GPU_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace Renderer::PathTracerGpu {

struct alignas(16) Ray {
    // xyz origin, w = source/output pixel encoded with uintBitsToFloat.
    std::array<float, 4> origin_pixel{};
    // xyz direction, w = RNG state encoded with uintBitsToFloat.
    std::array<float, 4> direction_rng{};
};

struct alignas(16) Surface {
    // xyz position, w linear distance/depth; depth < 0 means miss.
    std::array<float, 4> position_depth{};
    // xyz normal, w material index encoded as float.
    std::array<float, 4> normal_material{};
    // xy UV, z source pixel for secondary surfaces, w reserved.
    std::array<float, 4> uv_source{};
};

struct alignas(16) Reservoir {
    // xyz secondary sample position, w effective M; M <= 0 means invalid.
    std::array<float, 4> sample_position_m{};
    // RGB selected indirect contribution, w reservoir weight sum.
    // Selected target is recomputed as luminance(RGB).
    std::array<float, 4> radiance_weight{};
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
    // x key (0 = empty), y sample count, z/w reserved.
    std::array<std::uint32_t, 4> header{};
    // x/y/z fixed-point RGB sums, w reserved.
    std::array<std::uint32_t, 4> radiance{};
};

static_assert(sizeof(Ray) == 32u);
static_assert(sizeof(Surface) == 48u);
static_assert(sizeof(Reservoir) == 32u);
static_assert(sizeof(Lighting) == 16u);
static_assert(sizeof(Moments) == 16u);
static_assert(sizeof(QueueCounters) == 16u);
static_assert(sizeof(DispatchCommand) == 16u);
static_assert(sizeof(DispatchCommands) == 32u);
static_assert(sizeof(RadianceCacheEntry) == 32u);

constexpr std::size_t RADIANCE_CACHE_ENTRIES = 65536u;

} // namespace Renderer::PathTracerGpu

#endif
