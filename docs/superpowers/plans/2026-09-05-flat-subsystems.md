# Flat Subsystems Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the `RW` namespace and engine façade so independent subsystems are composed directly in `main.cpp`.

**Architecture:** `Ecs`, `Models`, `Renderer`, and `Camera` are top-level namespaces with explicit dependency edges. `main.cpp` owns application lifecycle and connects them; there is no umbrella header or engine object.

**Tech Stack:** C++17, LWCGL v2.9.3, OpenGL compatibility renderer, custom C build system.

**Spec:** `docs/superpowers/specs/2026-09-05-flat-subsystems-design.md`

## Global Constraints

- Delete `Core.h`, `Sources/Engine.cpp`, and `Sources/Engine.hpp`.
- Remove every `RW::` and `namespace RW` reference.
- Keep subsystem implementations in their existing directories/files.
- Keep the default OBJ run path and existing basic rasterizer behavior.
- Do not add an alternative compatibility layer.

---

### Task 1: Flat namespace contract

**Files:**
- Create: `tests/subsystems_contract.cpp`
- Delete: `tests/core_contract.cpp`

**Interfaces:**
- Consumes: `Sources/Ecs/Ecs.hpp`, `Sources/Models/Models.hpp`, `Sources/Renderer/Render.hpp`, `Sources/Camera.hpp`
- Produces: compile-time contract for `Ecs::World`, `Models::SpawnOptions`, `Renderer::Rasterizer`, `Camera::Controller`

- [ ] Write the contract using only direct subsystem headers and flat namespace names.
- [ ] Compile it against the current code and confirm it fails because the public APIs are still under `RW`.

### Task 2: Flatten subsystem namespaces

**Files:**
- Modify: `Sources/Ecs/Ecs.hpp`, `Sources/Ecs/Ecs.cpp`
- Modify: all files under `Sources/Models/`
- Modify: `Sources/Renderer/Render.hpp`, `Sources/Renderer/Render.cpp`
- Modify: `Sources/Camera.hpp`, `Sources/Camera.cpp`
- Modify: existing tests under `tests/`

**Interfaces:**
- Produces: `Ecs::*`, `Models::*`, `Renderer::*`, and `Camera::Controller`

- [ ] Replace `RW::Ecs` with `Ecs`, `RW::Models` with `Models`, `RW::Renderer` with `Renderer`, and `RW::CameraController` with `Camera::Controller`.
- [ ] Update all direct references and tests.
- [ ] Compile non-graphics contracts and confirm the flat namespace contract passes.

### Task 3: Make main the composition root

**Files:**
- Modify: `main.cpp`
- Delete: `Core.h`, `Sources/Engine.cpp`, `Sources/Engine.hpp`

**Interfaces:**
- Consumes: LWCGL, `Ecs::World`, `Models`, `Renderer::Rasterizer`, `Camera::Controller`
- Produces: executable lifecycle with direct subsystem composition

- [ ] Move display/input initialization, camera creation, model load/spawn, frame loop, resize handling, render/present, and shutdown into `main.cpp`.
- [ ] Preserve `Assets/default.obj` when no CLI model path is supplied.
- [ ] Ensure cleanup executes on every initialization failure after display creation.

### Task 4: Verify architecture

**Files:**
- Inspect: complete repository

- [ ] Search for `RW::`, `namespace RW`, `Core.h`, and `Engine.hpp` references; expect none.
- [ ] Verify deleted façade files are absent from the tree.
- [ ] Run strict syntax/behavior checks available in the current environment and report any unverified LWCGL link/runtime step explicitly.