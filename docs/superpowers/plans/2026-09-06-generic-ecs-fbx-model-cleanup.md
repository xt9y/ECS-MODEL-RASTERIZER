# Generic ECS + FBX Model Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the fixed ECS with a generic typed registry, move all engine components out of `Ecs/`, reorganize the model loader by responsibility, and add temporary FBX static-mesh import through pinned `ufbx` while preserving the existing public Models API.

**Architecture:** `Ecs::World` owns only entity IDs, type-erased component pools, typed sparse/dense storage, queries, and revision tracking. Renderer/camera components are declared by their owning modules. `Models::load()` dispatches to custom OBJ or temporary ufbx-backed FBX importers hidden under `Models/Formats`.

**Tech Stack:** C++20, lwcgl v2.9.3, C-BuildSystem source dependencies, ufbx v0.23.0 commit `fcc5d6ba444cfd3eb80677dba5e37e493941abe5`.

**Spec:** `docs/superpowers/specs/2026-09-06-generic-ecs-models-fbx-animation-design.md`

## Global Constraints

- Work directly on `main`, as explicitly requested.
- `Ecs/` defines no camera/renderer/game component types and no math types.
- New component types must require zero changes to ECS internals.
- Keep one `Ecs::World`; do not create subsystem-specific ECS instances.
- Preserve normal public model entry points: `Models::load()`, `mesh()`, `material()`, `part()`, `partCount()`, `clearCache()`.
- OBJ remains the custom parser.
- FBX uses temporary pinned ufbx and imports static geometry/material data only; runtime animation is not implemented in this change.
- GAME-owned components stay in GAME; engine components live with their owning engine module.

---

### Task 1: Generic ECS core

**Files:**
- Replace: `Sources/Ecs/Ecs.hpp`
- Replace: `Sources/Ecs/Ecs.cpp`

**Interfaces:**
- Produces: `World::add<T>()`, `get<T>()`, `has<T>()`, `remove<T>()`, `each<Ts...>()`, `createEntity()`, `entities()`, `changeRevision()`, `markChanged()`.

- [ ] Replace fixed optional arrays with type-erased `StorageBase` + typed sparse/dense `Storage<T>`.
- [ ] Implement O(1) add/get/remove and dense iteration.
- [ ] Keep entity/revision logic in `Ecs.cpp`; templates remain in `Ecs.hpp`.
- [ ] Remove all fixed component declarations and named add/get methods.

### Task 2: Move components to owners and migrate engine systems

**Files:**
- Create: `Sources/Renderer/Components.hpp`
- Modify: `Sources/Camera.hpp`
- Modify: `Sources/Camera.cpp`
- Modify: `Sources/Renderer/Render.hpp`
- Modify: `Sources/Renderer/Render.cpp`
- Modify: `Sources/Renderer/Gi/Gi.cpp`

**Interfaces:**
- `Renderer::Transform`, `MeshComponent`, `RenderableComponent`, `LightComponent` live in renderer.
- `Camera::CameraComponent` and `Camera::activeCamera()` live in camera.

- [ ] Move renderer-owned component types out of ECS.
- [ ] Move camera component and active-camera lookup into Camera.
- [ ] Convert renderer/camera/GI to `world.get<T>()` and typed queries.
- [ ] Preserve `Rasterizer` public API.

### Task 3: Models directory cleanup

**Files:**
- Keep public facade: `Sources/Models/Models.cpp`, `Sources/Models/Models.hpp`
- Move to `Sources/Models/Core/`: `Types`, `Material`, `Texture`
- Move to `Sources/Models/Formats/`: `Obj`
- Move to `Sources/Models/Images/`: `Tga`
- Create: `Sources/Models/ThirdParty/Ufbx.hpp`

**Interfaces:**
- Public Models facade names remain unchanged.

- [ ] Move files and update every include to the new responsibility-based paths.
- [ ] Keep texture/material/cache behavior unchanged for OBJ.
- [ ] Isolate ufbx include behind `ThirdParty/Ufbx.hpp`.

### Task 4: Temporary FBX static importer

**Files:**
- Create: `Sources/Models/Formats/Fbx.hpp`
- Create: `Sources/Models/Formats/Fbx.cpp`
- Modify: `Sources/Models/Models.cpp`
- Modify: `build.c`

**Interfaces:**
- `Fbx::load(path, Document*, error)` returns flat `MeshData + MaterialData` parts compatible with the existing Models facade.

- [ ] Add pinned ufbx git source dependency using `c_dep_source()`, `c_dep_include(".")`, `c_dep_sources("ufbx.c")`.
- [ ] Load FBX with generated/normalized normals.
- [ ] Flatten node geometry transforms into imported vertices because the current Models facade exposes flat parts without a node hierarchy.
- [ ] Split imported geometry by FBX material assignment.
- [ ] Triangulate polygon faces through ufbx.
- [ ] Import UVs, normals, base color, opacity, and external diffuse/base-color texture path where supported by the current texture loader.
- [ ] Dispatch `.fbx` case-insensitively from `Models::load()` while retaining OBJ behavior.

### Task 5: GAME migration

**Files:**
- Modify: `xt9y/GAME/main.cpp`

**Interfaces:**
- GAME uses `world.add<T>()` with engine-owned `Renderer::Transform`, `Renderer::MeshComponent`, `Renderer::RenderableComponent`, and `Camera::CameraComponent`.

- [ ] Replace old named ECS calls with typed generic calls.
- [ ] Preserve current GAME structure, model loading, camera behavior, Glock/Sponza assets, and OpenGL context setup.

### Task 6: Static verification

- [ ] Search both repos for removed APIs (`addTransform`, `getTransform`, `Ecs::TransformComponent`, etc.) and eliminate all remaining references.
- [ ] Search for stale old Models include paths and eliminate all remaining references.
- [ ] Verify C-Build source globs include new nested C++ folders and ufbx is supplied as an explicit source dependency.
- [ ] Verify final `main` heads and provide local `c build` / `c build run` commands; do not claim runtime success without actual local output.
