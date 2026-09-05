# Flat Subsystems Design

## Goal

Remove the `RW` hierarchy and `Engine`/`Core.h` compatibility façade. `Ecs`, `Models`, `Renderer`, and camera control remain independently addressable modules and `main.cpp` becomes the composition root.

## Architecture

- `Ecs` is exposed as `namespace Ecs` and owns only entity/component state.
- `Models` is exposed as `namespace Models`; loaders, materials, textures, OBJ, and TGA stay under it. Model spawning may consume an `Ecs::World`, but Models does not own a world or renderer.
- `Renderer` is exposed as `namespace Renderer`; it consumes an `Ecs::World` and model handles but owns only rendering state.
- Camera control is exposed as `namespace Camera` with `Camera::Controller`; it consumes `Ecs::World` and input state but owns no engine lifecycle.
- `main.cpp` directly owns LWCGL display/input initialization, `Ecs::World`, `Renderer::Rasterizer`, `Camera::Controller`, model loading/spawning, the frame loop, resize handling, and shutdown.
- `Sources/Engine.cpp`, `Sources/Engine.hpp`, and `Core.h` are deleted.
- No source or test may contain `namespace RW`, `namespace RW::`, or `RW::` references.

## Runtime flow

`main.cpp` initializes LWCGL and input, initializes the rasterizer, creates the default camera entity, loads the requested OBJ (or `Assets/default.obj`), spawns it into the world, then loops: process messages -> input/camera update -> resize -> render -> present. Shutdown is explicit in reverse order.

## Validation

Existing ECS/model/material/texture/renderer contracts are updated to the flat namespaces. The obsolete `core_contract.cpp` is removed and replaced with an independence contract that includes subsystem headers directly and verifies their public types are usable without `Core.h` or `Engine.hpp`.