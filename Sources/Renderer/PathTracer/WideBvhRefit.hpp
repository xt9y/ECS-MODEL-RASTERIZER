#ifndef RW_ENGINE_RENDERER_PATHTRACER_WIDEBVH_REFIT_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_WIDEBVH_REFIT_HPP

#include "Renderer/PathTracer/WideBvh.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::PathTracerAccel {

// Builds the normal SAH BVH8 but also records the source-triangle permutation
// required to update the same leaf topology for deforming geometry.
BuildResult buildRefittableWideBvh(const std::vector<Triangle>& triangles);

const std::vector<std::uint32_t>& sourceTriangleOrder(const BuildResult& bvh);

// Updates leaf triangles and conservatively requantizes all internal bounds
// without changing node/leaf topology. Returns false if the source topology no
// longer matches the topology that was originally built.
bool refitWideBvh(
    BuildResult *bvh,
    const std::vector<Triangle>& source_triangles,
    const std::vector<std::uint32_t>& source_order,
    std::string *error = nullptr
);

// Copies a BLAS into a global node/triangle address space. Interior child
// references and encoded leaf-first indices are rebased, while all bounds and
// leaf counts remain unchanged.
bool appendOffsetWideBvh(
    const BuildResult& bvh,
    std::uint32_t node_offset,
    std::uint32_t triangle_offset,
    std::vector<WideNode> *nodes,
    std::vector<Triangle> *triangles,
    std::string *error = nullptr
);

} // namespace Renderer::PathTracerAccel

#endif
