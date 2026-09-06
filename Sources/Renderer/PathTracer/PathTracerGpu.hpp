#ifndef RW_ENGINE_RENDERER_PATHTRACER_GPU_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_GPU_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace Renderer::PathTracerGpu {

using Mat4 = std::array<float, 16>;

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
    // Exact direct point-light contribution for primary surfaces.
    std::array<float, 4> direct{};
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

// BLAS instance referenced by TLAS leaves. Static world geometry is represented
// as one identity-transform instance; deforming meshes get their own local BLAS.
struct alignas(16) Instance {
    Mat4 object_to_world {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    Mat4 world_to_object = object_to_world;
    // x = first BLAS node in global node buffer
    // y = BLAS node count
    // z = first triangle in global triangle buffer
    // w = triangle count
    std::array<std::uint32_t, 4> data{};
};

struct alignas(16) DispatchCommand {
    std::uint32_t groups_x = 0u;
    std::uint32_t groups_y = 1u;
    std::uint32_t groups_z = 1u;
    std::uint32_t padding = 0u;
};

// Fixed header of the combined queue/control SSBO. A uint hit_queue[] runtime
// array starts immediately after this structure. The same buffer is also bound
// as GL_DISPATCH_INDIRECT_BUFFER, so command offsets are compile-time constants.
struct alignas(16) QueueControl {
    // x = hit count, y = bounce count, z/w reserved.
    std::array<std::uint32_t, 4> counters{};
    DispatchCommand hit_dispatch{};
    DispatchCommand bounce_dispatch{};
    std::array<std::uint32_t, 4> bucket_count0{};
    std::array<std::uint32_t, 4> bucket_count1{};
    std::array<std::uint32_t, 4> bucket_offset0{};
    std::array<std::uint32_t, 4> bucket_offset1{};
    std::array<std::uint32_t, 4> bucket_cursor0{};
    std::array<std::uint32_t, 4> bucket_cursor1{};
};

struct alignas(16) RadianceCacheEntry {
    // x key (0 = empty), y sample count, z/w reserved.
    std::array<std::uint32_t, 4> header{};
    // x/y/z fixed-point RGB sums, w reserved.
    std::array<std::uint32_t, 4> radiance{};
};

static_assert(sizeof(Ray) == 32u);
static_assert(sizeof(Surface) == 64u);
static_assert(sizeof(Reservoir) == 32u);
static_assert(sizeof(Lighting) == 16u);
static_assert(sizeof(Moments) == 16u);
static_assert(sizeof(Instance) == 144u);
static_assert(sizeof(DispatchCommand) == 16u);
static_assert(sizeof(QueueControl) == 144u);
static_assert(sizeof(RadianceCacheEntry) == 32u);

constexpr std::size_t HIT_DISPATCH_OFFSET = 16u;
constexpr std::size_t BOUNCE_DISPATCH_OFFSET = 32u;
constexpr std::size_t HIT_QUEUE_OFFSET = sizeof(QueueControl);
constexpr std::size_t RADIANCE_CACHE_ENTRIES = 65536u;
constexpr std::uint32_t GI_PHASE_COUNT = 4u;

} // namespace Renderer::PathTracerGpu

#endif
