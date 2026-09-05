# Shared Library + GAME Boundary Design

Date: 2026-09-05

## Goal

Turn `xt9y/ECS-MODEL-RASTERIZER` into a shared library while preserving its current source structure exactly, then use that library from `xt9y/GAME`.

## Non-negotiable constraints

- Keep the existing rasterizer structure intact.
- Do not add an `Engine` class, engine wrapper, facade, public API folder, or replacement source hierarchy.
- Existing subsystem classes/functions remain the API:
  - `Renderer::Rasterizer`
  - `Ecs::World`
  - `Camera::Controller`
  - `Models::*`
- Keep the existing headers at their current paths under `Sources/`.
- Do not copy rasterizer implementation files into `GAME`.
- Do not use `dlopen`, `dlsym`, `LoadLibrary`, or a manual plugin ABI for normal use.
- `GAME/main.cpp` must be exactly the program supplied by the user, with no additional application layer.

## ECS-MODEL-RASTERIZER

Current layout remains:

```text
ECS-MODEL-RASTERIZER/
├── Assets/
├── Sources/
│   ├── Camera.cpp
│   ├── Camera.hpp
│   ├── Ecs/
│   ├── Models/
│   └── Renderer/
├── tests/
├── README
└── build.c
```

Only the build boundary changes. `build.c` will stop declaring the obsolete executable target and instead declare a C-BuildSystem shared-library target containing:

- `Sources/*.cpp`
- `Sources/*/*.cpp`

The current compiler/link/platform configuration remains equivalent, including C++20, lwcgl, GLFW/OpenGL and platform libraries/frameworks.

Expected Linux artifact:

```text
build/debug/libecs-model-rasterizer.so
build/release/libecs-model-rasterizer.so
```

The rasterizer repository remains library-only; no `main.cpp` is reintroduced.

## GAME

`xt9y/GAME` stays intentionally minimal:

```text
GAME/
├── build.c
└── main.cpp
```

`main.cpp` is the exact user-supplied example and directly includes:

```cpp
#include "Sources/Camera.hpp"
#include "Sources/Ecs/Ecs.hpp"
#include "Sources/Models/Models.hpp"
#include "Sources/Renderer/Render.hpp"
```

`GAME/build.c` is responsible only for compiling `main.cpp`, exposing the rasterizer repository root as an include path, linking the produced shared library, and carrying any runtime/system link requirements needed by the final executable.

## Dependency strategy

Use C-BuildSystem's existing dependency/build-target mechanisms where possible. The desired data flow is:

```text
ECS-MODEL-RASTERIZER sources
        ↓
shared library
        ↓
libecs-model-rasterizer.so
        ↓
GAME/main.cpp
        ↓
GAME executable
```

No new engine abstraction is inserted between the executable and the existing subsystem APIs.

If the current C-BuildSystem cannot directly consume a shared-library target from a separate Git dependency's own `build.c`, make only the smallest build-system change required to support this use case. Do not work around it by compiling the rasterizer sources directly into `GAME`, because that would defeat the requested shared-library boundary.

## Runtime linking

The executable must be able to find the rasterizer shared library without requiring the user to manually copy it for every run. Prefer an executable runtime search path that resolves the library from the build/dependency output location, or another minimal C-BuildSystem-supported mechanism. Do not system-install the library as a prerequisite unless the existing build system makes that unavoidable.

## Verification

The implementation is complete only when:

1. `ECS-MODEL-RASTERIZER` builds successfully as a shared library.
2. Its existing contract tests still build/pass where applicable.
3. `GAME` contains the exact supplied `main.cpp` and a minimal `build.c`.
4. `GAME` compiles and links against the shared rasterizer artifact rather than compiling rasterizer `.cpp` files itself.
5. Runtime shared-library resolution is verified.
6. No `Engine.*`, wrapper facade, copied subsystem implementation, or structural rewrite was introduced.
