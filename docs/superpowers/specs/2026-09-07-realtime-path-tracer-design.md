# Real-Time Path Tracer Design

## Goal

Replace the brute-force low-resolution path tracer with a GPU architecture that spends rays efficiently instead of hiding cost through extreme resolution reduction. The renderer remains OpenGL 4.3 through lwcgl and keeps the current generic ECS, Models, FBX and animation systems.

## Constraints

- OpenGL 4.3 compute is the minimum backend. No Vulkan/DXR migration in this change.
- One explicit ECS point light remains the direct-light source.
- Primary visibility runs at output resolution.
- Indirect lighting is one fresh candidate path per visible pixel, then reused spatially and temporally.
- Camera motion must reproject and validate history rather than clear the image to black.
- Static scene acceleration is cached and must not rebuild when only the camera moves.
- No shadow maps, PCF, PCSS, fake ambient term, or raster G-buffer.
- No GitHub workflow files are added or modified.

## Acceleration Structure

Build a high-quality binary BVH on the CPU using 16-bin SAH, then collapse the binary tree into BVH8 nodes. Each BVH8 node stores eight child references and 8-bit quantized child bounds relative to the parent bounds. Leaf references encode a contiguous triangle range. Triangles are reordered into leaf order once during the build.

The GPU traversal uses the wide nodes directly. It performs near-first traversal with a short stack sized for the shallow BVH8 tree. Shadow rays use a separate any-hit traversal and return at the first blocker. Static geometry remains cached until geometry, transforms, materials or animation pose changes.

## Wavefront Pipeline

The renderer is split into coherent compute stages instead of tracing an entire path in one shader invocation:

1. `PrimaryGenerate`: generate one camera ray per output pixel.
2. `PrimaryIntersect`: traverse BVH8 and write a surface record; compact visible pixels into `HitQueue` using an atomic counter.
3. `PrimaryShade`: dispatch indirectly over `HitQueue`; evaluate point-light NEE and spawn one cosine-weighted secondary ray into `BounceQueue`.
4. `BounceIntersect`: dispatch indirectly over `BounceQueue`; intersect secondary rays and write secondary surface records.
5. `BounceShade`: evaluate point-light NEE at the secondary hit, add a radiance-cache estimate for deeper transport, and create one ReSTIR GI candidate reservoir for the primary pixel.
6. `TemporalReuse`: reproject the previous reservoir using the current world-space primary hit and previous camera matrix. Reuse only when depth, normal and world-position tests pass.
7. `SpatialReuse`: combine the temporal reservoir with four rotated neighboring reservoirs using weighted reservoir sampling and geometry similarity tests.
8. `Compose`: combine exact primary direct lighting with the selected GI reservoir contribution.
9. `TemporalFilter`: SVGF-style motion reprojection, luminance moments, variance estimation and history clamping.
10. `Atrous`: three edge-aware a-trous filtering iterations guided by normal and depth.
11. `Present`: tone map and display at output resolution.

Queue counters are stored in an SSBO. Indirect dispatch commands are written from queue counts so empty/dead paths do not consume later shader invocations.

## ReSTIR GI

Each reservoir stores the secondary sample position and normal, estimated incoming radiance, target weight sum, selected-sample target value, effective sample count and validity. Initial candidates come from one cosine-weighted secondary ray per primary hit.

Reservoir update uses weighted reservoir sampling:

`Wsum += candidate_weight; M += candidate_M; replace selected sample with probability candidate_weight / Wsum`.

Temporal reuse contributes the reprojected previous reservoir after geometry validation. Spatial reuse evaluates up to four neighbors using a frame-rotated pattern. The final estimator uses the selected sample and normalized reservoir weight; reuse is capped to prevent old history from dominating after changes.

## Radiance Cache

Use a persistent GPU hash table keyed by quantized world position and normal octant. Each entry stores a key, fixed-point RGB sum and sample count. Secondary shading performs four-way probing. Atomic compare-and-swap claims empty entries and integer atomics accumulate radiance. Queries return the running average when a matching entry exists.

The cache is cleared on scene or light changes but preserved across camera movement. It is used only as a deeper-bounce estimate; the first indirect bounce is still explicitly traced.

## Reprojection and SVGF

Store current/previous primary position, normal and depth plus current/previous camera view-projection matrices. Reproject the current hit into the previous frame and validate:

- previous UV inside the viewport;
- normal dot product >= 0.85;
- relative depth error <= 5%;
- world-position error bounded by depth-scaled tolerance.

Valid history is blended with current lighting and luminance moments. Invalid history immediately falls back to the current sample. Three a-trous iterations use depth/normal/luminance weights so edges do not smear across geometry.

## Performance Policy

- Output resolution is the default trace resolution; no 1/8 or 1/4 default.
- One primary ray and at most one fresh secondary GI ray per visible pixel.
- Direct/shadow rays use any-hit traversal.
- Dead paths are compacted out of wavefront queues.
- BVH8 reduces traversal depth and improves coherent memory access.
- Temporal/spatial ReSTIR reuse makes the one fresh GI candidate more valuable.
- Radiance cache replaces additional deep path bounces.
- SVGF reconstructs a stable image instead of requiring many spp.
- Existing public `Renderer::PathTracer` API remains compatible.

## Failure Handling

Initialization fails if required OpenGL 4.3 functions are unavailable. Shader compilation/link failures print the driver log. Buffer/texture allocation failures disable rendering cleanly. Indirect queue capacity is capped to pixel count and shader queue appends bounds-check their atomic index.

## Verification

- CPU contract test validates BVH8 coverage, quantized child bounds and ray-hit agreement against brute-force triangles.
- Source contract ensures no legacy extreme-resolution default or old world-BVH shader is selected.
- Local runtime verification must confirm: startup builds the static BVH once, camera movement does not rebuild it, compute shaders compile, and frame pacing remains responsive at 1280x720.
