#ifndef RW_ENGINE_RENDERER_PATHTRACER_WIDE_TLAS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_WIDE_TLAS_HPP

#include "Renderer/PathTracer/WideBvh.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::PathTracerAccel {

struct TlasBuildResult {
    bool valid = false;
    std::string error;
    std::vector<WideNode> nodes;
    // TLAS leaves index this array. Entries are indices into the caller's
    // original instance-bounds array.
    std::vector<std::uint32_t> instance_order;
    Bounds bounds{};
};

TlasBuildResult buildWideTlas(const std::vector<Bounds>& instance_bounds);

// Preserves TLAS topology and leaf ordering while updating all quantized child
// bounds from a new set of instance AABBs. This is the fast path for animated
// BLAS bounds and moving instance transforms.
bool refitWideTlas(
    TlasBuildResult *tlas,
    const std::vector<Bounds>& instance_bounds,
    std::string *error = nullptr
);

} // namespace Renderer::PathTracerAccel

#endif
