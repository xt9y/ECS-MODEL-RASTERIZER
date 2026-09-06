# Real-Time Path Tracer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the brute-force renderer with a native-resolution OpenGL 4.3 wavefront path tracer using BVH8, ReSTIR GI, temporal reprojection, SVGF and a persistent radiance cache.

**Architecture:** CPU builds a quantized BVH8 over world-space triangles. GPU work is split into compact wavefront queues. One fresh indirect sample is reused through ReSTIR GI, deeper diffuse transport comes from a hash radiance cache, and SVGF reconstructs stable full-resolution lighting across camera motion.

**Tech Stack:** C++20, OpenGL 4.3 compute through lwcgl v2.9.3, GLSL 4.30, generic ECS.

**Spec:** `docs/superpowers/specs/2026-09-07-realtime-path-tracer-design.md`

## Global Constraints

- OpenGL 4.3 minimum; do not add Vulkan/DXR in this plan.
- Preserve `Renderer::PathTracer` public API.
- One ECS point light.
- Native output-resolution primary visibility.
- No shadow maps, PCF/PCSS, fake ambient, or raster G-buffer.
- No GitHub workflow file changes.

---

### Task 1: Quantized BVH8

**Files:**
- Create: `Sources/Renderer/PathTracer/WideBvh.hpp`
- Create: `Sources/Renderer/PathTracer/WideBvh.cpp`
- Test: `tests/pathtracer_bvh_contract.cpp`

**Interfaces:**
- Produces `Renderer::PathTracerAccel::Triangle`, `WideNode`, `BuildResult`, and `buildWideBvh()`.
- `WideNode` is std430-compatible and contains parent bounds, eight packed child references and two packed 8-bit bound words per child.

- [ ] Write a CPU contract that builds a small triangle scene, checks every source triangle appears once, verifies quantized child bounds conservatively enclose their primitives, and compares BVH traversal hits against brute force.
- [ ] Build a 16-bin SAH binary BVH and reorder triangles into leaf-contiguous DFS order.
- [ ] Collapse binary nodes into up to eight-child frontier nodes and quantize each child bound against its BVH8 parent.
- [ ] Run the contract locally when a compiler is available; otherwise perform source-level structural verification and leave runtime verification explicit.
- [ ] Commit BVH8 implementation.

### Task 2: GPU Resource and Wavefront Layout

**Files:**
- Create: `Sources/Renderer/PathTracer/PathTracerGpu.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.cpp`

**Interfaces:**
- Defines std430-compatible CPU mirrors for ray, surface, reservoir, radiance-cache and dispatch-counter buffers.
- PathTracer owns two history sets and queue/counter/indirect buffers sized to output pixels.

- [ ] Define GPU structs with explicit 16-byte alignment and static assertions for required sizes.
- [ ] Allocate buffers for primary rays, hit queue, bounce rays, secondary hits, two reservoir histories, two surface histories, lighting histories, moments, variance, radiance cache and indirect dispatch.
- [ ] Add allocation cleanup and resize logic.
- [ ] Commit resource layout.

### Task 3: Wavefront Shaders

**Files:**
- Create: `Sources/Renderer/PathTracer/WavefrontShaders.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracerShaders.hpp`

**Interfaces:**
- Exposes shader strings for `primary_generate`, `primary_intersect`, `primary_shade`, `bounce_intersect`, `bounce_shade`, and queue-to-dispatch preparation.

- [ ] Add shared GLSL BVH8 decode/traversal functions with nearest-hit and any-hit modes.
- [ ] Generate one primary ray per output pixel.
- [ ] Primary intersection writes surface data and atomically compacts hit pixels.
- [ ] Primary shade computes point-light NEE and atomically compacts one cosine-weighted secondary ray per visible pixel.
- [ ] Bounce intersection writes secondary hit records.
- [ ] Bounce shade evaluates secondary point-light NEE plus radiance-cache query and emits one initial GI reservoir per primary pixel.
- [ ] Convert queue counts to indirect dispatch commands and dispatch later stages with `glDispatchComputeIndirect`.
- [ ] Commit wavefront shaders.

### Task 4: ReSTIR GI

**Files:**
- Create: `Sources/Renderer/PathTracer/RestirShaders.hpp`

**Interfaces:**
- Exposes `temporal_reuse` and `spatial_reuse` compute shaders operating on reservoir/surface history buffers.

- [ ] Implement weighted reservoir update and normalization helpers.
- [ ] Reproject current world-space hit into previous camera space and validate normal, depth and world-position consistency.
- [ ] Merge valid temporal reservoir with capped historical M.
- [ ] Merge four frame-rotated spatial neighbors with geometry validation.
- [ ] Commit ReSTIR GI shaders.

### Task 5: Radiance Cache

**Files:**
- Create: `Sources/Renderer/PathTracer/RadianceCacheShaders.hpp`
- Modify: `Sources/Renderer/PathTracer/WavefrontShaders.hpp`

**Interfaces:**
- Hash table entry is fixed-point RGB accumulation plus count and key.
- Secondary shade performs four-probe query/update.

- [ ] Hash quantized world position plus normal octant into a 64K-entry table.
- [ ] Claim empty entries through `atomicCompSwap`.
- [ ] Accumulate clamped radiance with integer `atomicAdd`.
- [ ] Query average radiance from matching entries and use it only as deeper-bounce contribution.
- [ ] Clear cache on scene/light changes, preserve it on camera movement.
- [ ] Commit radiance cache.

### Task 6: SVGF Reprojection and Filtering

**Files:**
- Create: `Sources/Renderer/PathTracer/SvgfShaders.hpp`

**Interfaces:**
- Exposes `compose`, `temporal_filter`, `atrous`, and final presentation shaders.

- [ ] Compose exact primary direct light plus ReSTIR GI estimate into a one-spp lighting signal.
- [ ] Reproject previous lighting and luminance moments using world-position projection into the previous camera.
- [ ] Validate history using normal/depth/world-position tests and clamp reused luminance to local current statistics.
- [ ] Compute temporal luminance moments and variance.
- [ ] Run three edge-aware a-trous passes using depth, normal and luminance weights.
- [ ] Tone-map and gamma encode the final full-resolution image.
- [ ] Commit SVGF shaders.

### Task 7: Renderer Orchestration

**Files:**
- Replace: `Sources/Renderer/PathTracer/PathTracer.cpp`
- Modify: `Sources/Renderer/PathTracer/PathTracer.hpp`

**Interfaces:**
- Public API remains `init`, `resize`, `render`, `shutdown`, `settings`.

- [ ] Build/cache world triangles and BVH8 only when scene signature changes.
- [ ] Compile all compute programs and bind fixed SSBO/image slots.
- [ ] Execute wavefront stages in order with barriers and indirect dispatch where queues are compacted.
- [ ] Swap reservoir/surface/SVGF history each frame.
- [ ] Reset only invalid history on camera motion; preserve radiance cache unless scene/light changes.
- [ ] Restore default `resolution_divisor = 1`, `samples_per_frame = 1`, and remove extreme-resolution fallback behavior.
- [ ] Log BVH build time once per actual rebuild and output resolution on init.
- [ ] Commit orchestration.

### Task 8: Remove Superseded Path

**Files:**
- Delete: `Sources/Renderer/PathTracer/PathTracerFastShaders.hpp`
- Delete: `Sources/Renderer/PathTracer/PathTracerWorldShaders.hpp`
- Modify: `Sources/Renderer/PathTracer/PathTracerShaders.hpp`

- [ ] Ensure only the new wavefront/ReSTIR/SVGF shader set is reachable.
- [ ] Search for `resolution_divisor = 8`, old stackless-world shader names, old TLAS/BLAS buffer names and remove stale references.
- [ ] Commit cleanup.

### Task 9: Verification

**Files:**
- Test: `tests/pathtracer_bvh_contract.cpp`
- Existing GAME consumer remains unchanged unless compilation exposes an API mismatch.

- [ ] Verify C++ contracts locally where toolchain access exists.
- [ ] Verify source contains `glDispatchComputeIndirect` usage and native-resolution default.
- [ ] Verify no workflow files changed.
- [ ] User runtime command: `cd ~/Pro/GAME && c update ecs-model-rasterizer && c build run`.
- [ ] Runtime acceptance: BVH cache line appears once during camera-only movement; no black history reset on motion; 1280x720 output remains native while GI converges temporally.
