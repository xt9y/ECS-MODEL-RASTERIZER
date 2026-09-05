# Shared Library + GAME Boundary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `xt9y/ECS-MODEL-RASTERIZER` as a standalone shared library and make `xt9y/GAME` consist only of the supplied `main.cpp` plus a minimal `build.c` that consumes that shared library.

**Architecture:** Preserve the rasterizer repository's current `Sources/...` structure and existing C++ APIs exactly. Add one small C-BuildSystem dependency mode that allows a Git dependency to build one of its own C-BuildSystem targets and link that produced artifact into a consumer, then use it from `GAME`; do not compile rasterizer implementation files into `GAME`.

**Tech Stack:** C++20, C-BuildSystem 1.x, lwcgl v2.9.3, GLFW/OpenGL, POSIX shared libraries (`.so` on Linux, `.dylib` on macOS).

**Spec:** `docs/superpowers/specs/2026-09-05-shared-library-game-boundary-design.md`

## Global Constraints

- Keep the existing rasterizer structure intact.
- Do not add an `Engine` class, engine wrapper, facade, public API folder, or replacement source hierarchy.
- Existing subsystem classes/functions remain the API: `Renderer::Rasterizer`, `Ecs::World`, `Camera::Controller`, and `Models::*`.
- Keep the existing headers at their current paths under `Sources/`.
- Do not copy rasterizer implementation files into `GAME`.
- Do not use `dlopen`, `dlsym`, `LoadLibrary`, or a manual plugin ABI for normal use.
- `GAME/main.cpp` must be exactly the program supplied by the user, with no additional application layer.
- The rasterizer must build as a real shared-library target containing `Sources/*.cpp` and `Sources/*/*.cpp`.
- `GAME` must link that shared-library artifact and must be able to run without manually copying the library beside the executable.
- Do not system-install the rasterizer as a prerequisite.

---

## File Map

### `xt9y/C-BuildSystem`

- Modify `include/cbuild.h`: add the source-compatible C-BuildSystem-target dependency declaration API.
- Modify `src/cli.c`: build an imported C-BuildSystem target, expose its include root, link its artifact, and attach a runtime search path for shared-library imports.
- Create `.github/ci/tests/cbuild_target_dependency.sh`: regression test proving a consumer links/runs against a separately built shared-library Git dependency.
- Modify `.github/ci/run-tests.sh`: execute the new regression.
- Modify `.github/ci/tests/api_1x_surface.sh`: lock the added public API into the 1.x surface test.

### `xt9y/ECS-MODEL-RASTERIZER`

- Modify `build.c`: replace the obsolete executable declaration with a shared-library declaration. No `Sources/` files move or change for this boundary conversion.

### `xt9y/GAME`

- Create `main.cpp`: exact user-supplied program.
- Create `build.c`: compile only `main.cpp`; fetch/build/link the rasterizer shared target; preserve the required lwcgl/OpenGL system linkage.

---

### Task 1: Add a regression for cross-repository C-BuildSystem shared targets

**Files:**
- Create: `xt9y/C-BuildSystem/.github/ci/tests/cbuild_target_dependency.sh`
- Modify: `xt9y/C-BuildSystem/.github/ci/run-tests.sh`
- Test: `xt9y/C-BuildSystem/.github/ci/tests/cbuild_target_dependency.sh`

**Interfaces:**
- Consumes: existing `c_git()`, `c_use()`, `c_shared_library()`, normal `c build [target] [--release]` behavior.
- Produces: a failing integration contract for the new API `c_dep_cbuild(C_Dependency *, const char *, C_TargetKind)`.

- [ ] **Step 1: Create the failing integration test**

Create `.github/ci/tests/cbuild_target_dependency.sh` with this complete test:

```sh
#!/bin/sh
set -eu

C_BIN="$1"
INC="$2"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM HUP

LIB="$TMP/libproject"
APP="$TMP/app"
mkdir -p "$LIB/include" "$LIB/src" "$APP/src"

cat >"$LIB/include/value.h" <<'SRC'
#ifndef VALUE_H
#define VALUE_H
int dependency_value(void);
#endif
SRC

cat >"$LIB/src/value.c" <<'SRC'
#include "value.h"
int dependency_value(void) { return 42; }
SRC

cat >"$LIB/build.c" <<'SRC'
#include <cbuild.h>
void build(C_Build *b) {
    C_Target *lib = c_shared_library(b, "dependency");
    c_sources(lib, "src/value.c");
    c_include(lib, "include");
    c_default_target(b, lib);
}
SRC

(
    cd "$LIB"
    git init -q
    git config user.name test
    git config user.email test@example.invalid
    git add .
    git commit -qm initial
)

cat >"$APP/src/main.c" <<'SRC'
#include <stdio.h>
#include "value.h"
int main(void) {
    printf("%d\n", dependency_value());
    return 0;
}
SRC

cat >"$APP/build.c" <<SRC
#include <cbuild.h>
void build(C_Build *b) {
    C_Target *app = c_executable(b, "app");
    c_sources(app, "src/main.c");

    C_Dependency *dep = c_git(b, "dependency", "$LIB", "master");
    c_dep_cbuild(dep, "dependency", C_TARGET_SHARED_LIBRARY);
    c_dep_include(dep, "include");
    c_use(app, dep);

    c_default_target(b, app);
}
SRC

(
    cd "$APP"
    C_INCLUDE_DIR="$INC" "$C_BIN" build
    test "$(./build/debug/app)" = 42

    if [ "$(uname -s)" = Darwin ]; then
        otool -L ./build/debug/app | grep -q 'libdependency.dylib'
    else
        ldd ./build/debug/app | grep -q 'libdependency.so'
    fi
)

echo "cbuild-target-dependency: ok"
```

- [ ] **Step 2: Register the test in the standard suite**

Add this line immediately after the existing source-dependency regression in `.github/ci/run-tests.sh`:

```sh
sh "$RUNTIME_TESTS/cbuild_target_dependency.sh" "$C_BIN" "$INC"
```

- [ ] **Step 3: Run the new test and verify it fails for the intended reason**

Run:

```sh
make
sh .github/ci/tests/cbuild_target_dependency.sh "$(pwd)/build/c" "$(pwd)/include"
```

Expected: compilation of the temporary consumer's `build.c` fails because `c_dep_cbuild` does not exist yet. It must not fail because Git, the C compiler, or the temporary repository setup is broken.

- [ ] **Step 4: Commit the failing regression**

```sh
git add .github/ci/tests/cbuild_target_dependency.sh .github/ci/run-tests.sh
git commit -m "test: cover cbuild target dependencies"
```

---

### Task 2: Implement C-BuildSystem target dependencies

**Files:**
- Modify: `xt9y/C-BuildSystem/include/cbuild.h`
- Modify: `xt9y/C-BuildSystem/src/cli.c`
- Modify: `xt9y/C-BuildSystem/.github/ci/tests/api_1x_surface.sh`
- Test: `xt9y/C-BuildSystem/.github/ci/tests/cbuild_target_dependency.sh`

**Interfaces:**
- Consumes: `C_Dependency`, `C_TargetKind`, `DepState`, dependency checkout resolution, `executable_path()`, and normal target artifact naming.
- Produces:
  - `C_DEP_CBUILD`
  - `void c_dep_cbuild(C_Dependency *d, const char *target, C_TargetKind kind)`
  - dependency metadata fields `build_target` and `build_target_kind`
  - automatic build/link/runtime-resolution of the imported target artifact.

- [ ] **Step 1: Extend the public dependency model without breaking existing 1.x source users**

In `include/cbuild.h`, extend `C_DepKind` by appending the new value; do not renumber existing values:

```c
typedef enum C_DepKind {
    C_DEP_HEADER_ONLY = 0,
    C_DEP_RESERVED = 1,
    C_DEP_SOURCE = 2,
    C_DEP_CBUILD = 3
} C_DepKind;
```

Add these fields at the end of `C_Dependency`:

```c
char build_target[C_MAX_NAME];
C_TargetKind build_target_kind;
```

Add this public helper beside the existing dependency helpers:

```c
static inline void c_dep_cbuild(C_Dependency *d, const char *target, C_TargetKind kind) {
    if (!d) c__fatal("c_dep_cbuild received a null dependency");
    c__require_name(target, "dependency target");
    if (kind != C_TARGET_STATIC_LIBRARY && kind != C_TARGET_SHARED_LIBRARY)
        c__fatal("c_dep_cbuild currently supports static or shared library targets");
    d->kind = C_DEP_CBUILD;
    c__copy(d->build_target, sizeof(d->build_target), target);
    d->build_target_kind = kind;
}
```

This is an additive 1.x API change: existing enum values and function signatures remain unchanged.

- [ ] **Step 2: Extend the API-surface regression**

In `.github/ci/tests/api_1x_surface.sh`, add `C_DEP_CBUILD` to the dependency-kind array and add a function pointer check:

```c
void (*p_dep_cbuild)(C_Dependency *, const char *, C_TargetKind) = c_dep_cbuild;
```

Consume it in the existing anti-unused section exactly as the other public function pointers are consumed.

- [ ] **Step 3: Add internal artifact state for imported targets**

In `src/cli.c`, extend internal dependency state with an artifact path without exposing it through `cbuild.h`:

```c
typedef struct CBuildDependencyArtifact {
    char path[PATH_MAX];
    char directory[PATH_MAX];
} CBuildDependencyArtifact;
```

Associate one artifact record with each dependency index during build resolution. Keep the existing source/package checkout fields unchanged.

- [ ] **Step 4: Factor target artifact naming into one helper used by normal targets and imported targets**

Add a helper with these semantics:

```c
static void compiler_target_artifact_name(char out[C_MAX_NAME + 32], const char *name, C_TargetKind kind) {
    if (kind == C_TARGET_STATIC_LIBRARY) {
        snprintf(out, C_MAX_NAME + 32, "%s.a", name);
        return;
    }
    if (kind == C_TARGET_SHARED_LIBRARY) {
#ifdef __APPLE__
        snprintf(out, C_MAX_NAME + 32, "lib%s.dylib", name);
#else
        snprintf(out, C_MAX_NAME + 32, "lib%s.so", name);
#endif
        return;
    }
    snprintf(out, C_MAX_NAME + 32, "%s", name);
}
```

Replace the duplicated shared/static output-name branch in the normal target builder with this helper so imported and local target naming cannot diverge.

- [ ] **Step 5: Build a `C_DEP_CBUILD` dependency by invoking the same C-BuildSystem executable inside its checkout**

After Git checkout resolution, when `build_artifacts` is true and `d->kind == C_DEP_CBUILD`:

1. Resolve the dependency project root to `state->source` plus `d->subdir` when present.
2. Resolve the current C-BuildSystem executable using the existing `executable_path()` helper.
3. Execute the equivalent of:

```sh
<c-current-executable> build <d->build_target> [--release] --cc <opt->cc>
```

with the dependency project root as `cwd`.
4. Compute the artifact as:

```text
<project-root>/build/debug/<artifact-name>
```

or:

```text
<project-root>/build/release/<artifact-name>
```

according to `opt->release`.
5. Abort with a concrete error if the nested build returns non-zero or the expected artifact does not exist.
6. Store both the artifact path and its containing directory for the consumer link step.

Use the exact failure wording pattern already used by C-BuildSystem dependency errors, for example:

```c
die("cbuild target build failed for %s", d->name);
```

and:

```c
die("cbuild target %s did not produce %s", d->build_target, artifact);
```

- [ ] **Step 6: Preserve normal include propagation for the imported project**

For `C_DEP_CBUILD`, use the dependency checkout/project root for `c_dep_include()` exactly as header/source dependencies already do. Do not redirect includes into a new install tree. This is what allows `GAME` to write:

```cpp
#include "Sources/Camera.hpp"
```

while keeping the rasterizer's current header paths unchanged.

- [ ] **Step 7: Link the imported artifact instead of compiling dependency sources into the consumer**

When assembling a consumer target's link command, for every attached `C_DEP_CBUILD` dependency:

```text
<link command> ... <absolute-path-to-imported-library>
```

Do not add `d->source_patterns` to the consumer's object list for this dependency kind.

For shared-library imports, also add the artifact directory as a runtime search path:

Linux/ELF:

```text
-Wl,-rpath,<artifact-directory>
```

macOS:

```text
-Wl,-rpath,<artifact-directory>
```

Keep the artifact path itself in the link command; do not require a system install or manual copy.

- [ ] **Step 8: Run the focused regression**

Run:

```sh
make clean
make
sh .github/ci/tests/cbuild_target_dependency.sh "$(pwd)/build/c" "$(pwd)/include"
```

Expected:

```text
cbuild-target-dependency: ok
```

- [ ] **Step 9: Run API compatibility and target-graph regressions**

Run:

```sh
sh .github/ci/tests/api_1x_surface.sh "$(pwd)/include"
sh .github/ci/tests/target_graph.sh "$(pwd)/build/c" "$(pwd)/include"
```

Expected: both scripts exit 0.

- [ ] **Step 10: Run the complete C-BuildSystem suite**

Run:

```sh
make test
```

Expected: exit 0, including the new `cbuild-target-dependency: ok` regression.

- [ ] **Step 11: Commit the implementation**

```sh
git add include/cbuild.h src/cli.c .github/ci/tests/api_1x_surface.sh
git commit -m "feat: link C-BuildSystem dependency targets"
```

---

### Task 3: Convert ECS-MODEL-RASTERIZER from executable to shared library

**Files:**
- Modify: `xt9y/ECS-MODEL-RASTERIZER/build.c`
- Test: existing `Sources/` compilation plus existing contract tests where applicable.

**Interfaces:**
- Consumes: current unchanged sources under `Sources/`, lwcgl, GLFW/OpenGL, C-BuildSystem `c_shared_library()`.
- Produces: target `ecs-model-rasterizer`, yielding `libecs-model-rasterizer.so` on Linux and `libecs-model-rasterizer.dylib` on macOS.

- [ ] **Step 1: Replace only the target declaration and source entry point in `build.c`**

Replace the current executable-oriented `configureApp()`/`build()` with the following complete build file while preserving the existing platform-link configuration:

```c
#include <cbuild.h>

static void configurePlatform(C_Target *target)
{
#ifdef __APPLE__
    c_define(target, "GL_SILENCE_DEPRECATION");
    c_include(target, "/opt/homebrew/include");
    c_include(target, "/usr/local/include");
    c_link_flag(target, "-L/opt/homebrew/lib");
    c_link_flag(target, "-L/usr/local/lib");
    c_link_system(target, "glfw");
    c_framework(target, "OpenGL");
    c_framework(target, "Cocoa");
    c_framework(target, "IOKit");
    c_framework(target, "CoreVideo");
#else
    c_link_system(target, "glfw");
    c_link_system(target, "GL");
    c_link_system(target, "GLU");
    c_link_system(target, "m");
    c_link_system(target, "dl");
    c_link_system(target, "pthread");
#endif
}

static void configureLibrary(C_Target *target)
{
    c_warnings_strict(target);
    c_include(target, ".");
    c_include(target, "Sources");
    c_include(target, "/usr/local/include");
    c_link_system(target, "lwcgl");
    configurePlatform(target);
}

void build(C_Build *b)
{
    C_Target *library = c_shared_library(b, "ecs-model-rasterizer");

    c_sources(library, "Sources/*.cpp");
    c_sources(library, "Sources/*/*.cpp");

    c_flag(library, "-std=c++20");
    configureLibrary(library);
    c_link_system(library, "stdc++");
    c_default_target(b, library);
}
```

Do not add `main.cpp`, `Engine.hpp`, an `include/` tree, or any wrapper source.

- [ ] **Step 2: Build the debug shared library**

Run:

```sh
c build
```

Expected on Linux:

```text
build/debug/libecs-model-rasterizer.so
```

Expected on macOS:

```text
build/debug/libecs-model-rasterizer.dylib
```

- [ ] **Step 3: Build the release shared library**

Run:

```sh
c build --release
```

Expected on Linux:

```text
build/release/libecs-model-rasterizer.so
```

Expected on macOS:

```text
build/release/libecs-model-rasterizer.dylib
```

- [ ] **Step 4: Verify the shared object exports the existing subsystem symbols rather than an Engine facade**

Linux:

```sh
nm -D -C build/debug/libecs-model-rasterizer.so | grep -E 'Renderer::Rasterizer|Ecs::World|Camera::Controller|Models::'
```

macOS:

```sh
nm -gC build/debug/libecs-model-rasterizer.dylib | grep -E 'Renderer::Rasterizer|Ecs::World|Camera::Controller|Models::'
```

Expected: matches for the existing subsystem APIs and no requirement for any `Engine::*` symbol.

- [ ] **Step 5: Run existing contract tests where they are host-buildable**

Compile/run the current contract tests using the same C++20/include environment they already require. At minimum, the boundary conversion must not require edits to:

```text
tests/camera_direction_contract.cpp
tests/ecs_contract.cpp
tests/material_contract.cpp
tests/models_contract.cpp
tests/models_independence_contract.cpp
tests/renderer_contract.cpp
tests/subsystems_contract.cpp
tests/texture_contract.cpp
```

Any failure caused by the library conversion must be fixed in `build.c`/link configuration rather than by moving or wrapping source APIs.

- [ ] **Step 6: Commit the library conversion**

```sh
git add build.c
git commit -m "build: produce rasterizer shared library"
```

---

### Task 4: Create the minimal GAME consumer

**Files:**
- Create: `xt9y/GAME/main.cpp`
- Create: `xt9y/GAME/build.c`
- Test: build/link/runtime integration against `xt9y/ECS-MODEL-RASTERIZER`.

**Interfaces:**
- Consumes:
  - `Sources/Camera.hpp`
  - `Sources/Ecs/Ecs.hpp`
  - `Sources/Models/Models.hpp`
  - `Sources/Renderer/Render.hpp`
  - `libecs-model-rasterizer.so` / `.dylib`
  - lwcgl
- Produces: executable target `game` containing only the supplied application source.

- [ ] **Step 1: Create `GAME/main.cpp` exactly as supplied**

```cpp
#include "Sources/Camera.hpp"
#include "Sources/Ecs/Ecs.hpp"
#include "Sources/Models/Models.hpp"
#include "Sources/Renderer/Render.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

class Example
{
public:
    static inline int run(int argc, char **argv)
    {
        constexpr int initial_width = 1280;
        constexpr int initial_height = 720;

        lwcglInstallFastRuntime();

        Display.setDisplayMode(new DisplayMode(initial_width, initial_height));
        Display.create();

        Display.setTitle("Test");

        Keyboard.create();
        Mouse.create();

        Renderer::Rasterizer renderer;
        renderer.init();

        int framebuffer_width  = std::max(Display.getWidth(),  1),
            framebuffer_height = std::max(Display.getHeight(), 1);

        renderer.resize(framebuffer_width, framebuffer_height);

        Ecs::World world;
        Camera::Controller camera_controller;

        const Ecs::Entity camera = world.createEntity();
        world.addTransform(camera, {
               .position = {.x = 0.0f, .y = 1.5f, .z = 5.0f},
               .rotation = {.x = 0.0f, .y = 0.0f, .z = 0.0f},
               .scale    = {.x = 1.0f, .y = 1.0f, .z = 1.0f},
        });

        world.addCamera(camera, {60.0f, 0.1f, true});

        std::string error;

        const Models::ModelHandle model = Models::load((argc > 1 ? argv[1] : "Assets/Sponza/sponza.obj"), &error);

        if (model == Models::INVALID_MODEL)
        {
            std::fprintf(stderr, "[LOG]: %s\n", error.c_str());

            renderer.shutdown();
            Models::clearCache();
            Mouse.destroy();
            Keyboard.destroy();
            Display.destroy();
            return 3;
        }

        if (Models::partCount(model) == 0u)
        {
            std::fprintf(stderr, "[LOG]: model has no renderable parts\n");

            renderer.shutdown();
            Models::clearCache();
            Mouse.destroy();
            Keyboard.destroy();
            Display.destroy();
            return 3;
        }

        for (std::size_t i = 0; i < Models::partCount(model); ++i)
        {
            if (!Models::part(model, i)) continue;

            const Ecs::Entity entity = world.createEntity();
            world.addTransform(entity, {});
            world.addMesh(entity, {Models::part(model, i)->mesh, Models::part(model, i)->material});
            world.addRenderable(entity, {true});
        }

        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();

        while (!Display.isCloseRequested())
        {
            Display.processMessages();
            if (Keyboard.isKeyDown(Keyboard.KEY_ESCAPE)) break;

            const auto now = Clock::now();
            const float delta_seconds = std::chrono::duration<float>(now - previous).count();
            previous = now;

            camera_controller.update(world, std::min(delta_seconds, 0.1f));

            const int width  = std::max(Display.getWidth(), 1);
            const int height = std::max(Display.getHeight(), 1);

            if ((width != framebuffer_width) || height != framebuffer_height)
            {
                framebuffer_height = height;
                framebuffer_width  = width;
                renderer.resize(width, height);
            }

            renderer.render(world);
            Display.updateNoMessages();
        }

        renderer.shutdown();
        Models::clearCache();
        Mouse.destroy();
        Keyboard.destroy();
        Display.destroy();

        return 0;
    }
};

int main(int argc, char **argv) {
    return Example::run(argc, argv);
}
```

No edits, wrapper class, helper source, or alternate include paths are permitted.

- [ ] **Step 2: Create the minimal `GAME/build.c`**

```c
#include <cbuild.h>

static void configurePlatform(C_Target *target)
{
#ifdef __APPLE__
    c_define(target, "GL_SILENCE_DEPRECATION");
    c_include(target, "/opt/homebrew/include");
    c_include(target, "/usr/local/include");
    c_link_flag(target, "-L/opt/homebrew/lib");
    c_link_flag(target, "-L/usr/local/lib");
    c_link_system(target, "glfw");
    c_framework(target, "OpenGL");
    c_framework(target, "Cocoa");
    c_framework(target, "IOKit");
    c_framework(target, "CoreVideo");
#else
    c_link_system(target, "glfw");
    c_link_system(target, "GL");
    c_link_system(target, "GLU");
    c_link_system(target, "m");
    c_link_system(target, "dl");
    c_link_system(target, "pthread");
#endif
}

void build(C_Build *b)
{
    C_Target *game = c_executable(b, "game");

    c_sources(game, "main.cpp");
    c_flag(game, "-std=c++20");
    c_warnings_strict(game);
    c_include(game, "/usr/local/include");
    c_link_system(game, "lwcgl");
    configurePlatform(game);
    c_link_system(game, "stdc++");

    C_Dependency *rasterizer = c_git(
        b,
        "ecs-model-rasterizer",
        "https://github.com/xt9y/ECS-MODEL-RASTERIZER.git",
        "main"
    );
    c_dep_cbuild(rasterizer, "ecs-model-rasterizer", C_TARGET_SHARED_LIBRARY);
    c_dep_include(rasterizer, ".");
    c_use(game, rasterizer);

    c_default_target(b, game);
}
```

The dependency must remain a built shared target. Do not replace `c_dep_cbuild(...)` with `c_dep_source(...)`.

- [ ] **Step 3: Build GAME from an empty dependency cache**

Run:

```sh
c deps clean
c build --explain
```

Expected behavior:

1. C-BuildSystem resolves `xt9y/ECS-MODEL-RASTERIZER`.
2. It invokes the rasterizer's own `build.c` target `ecs-model-rasterizer`.
3. It produces the shared library.
4. It compiles only `GAME/main.cpp` as GAME-owned application source.
5. It links the executable against the produced shared library.

- [ ] **Step 4: Prove GAME is dynamically linked to the rasterizer**

Linux:

```sh
ldd build/debug/game | grep 'libecs-model-rasterizer.so'
```

macOS:

```sh
otool -L build/debug/game | grep 'libecs-model-rasterizer.dylib'
```

Expected: one dynamic dependency on the rasterizer library.

- [ ] **Step 5: Prove GAME did not absorb rasterizer implementation objects**

Run:

```sh
find build/debug -type f \( -name 'Camera*.o' -o -name 'Ecs*.o' -o -name 'Models*.o' -o -name 'Render*.o' \) -print
```

Expected: no rasterizer implementation object files under GAME's own build directory. The rasterizer's objects may exist only inside its dependency build directory used to produce the `.so`/`.dylib`.

- [ ] **Step 6: Verify runtime shared-library resolution without manual copying**

Run the executable directly from the GAME repository:

```sh
./build/debug/game
```

Expected: the dynamic loader successfully resolves the rasterizer shared library. A later model/asset error is not a dynamic-loader failure; errors such as `libecs-model-rasterizer.so: cannot open shared object file` are failures of this task.

- [ ] **Step 7: Verify the supplied default asset behavior without modifying `main.cpp`**

Because the supplied program uses:

```text
Assets/Sponza/sponza.obj
```

run either with that relative asset path available from the GAME working directory or provide the rasterizer Sponza OBJ explicitly as `argv[1]` during runtime verification. Do not change the supplied default string in `main.cpp` merely to accommodate repository separation.

- [ ] **Step 8: Commit GAME**

```sh
git add main.cpp build.c
git commit -m "build: use rasterizer shared library"
```

---

### Task 5: Final cross-repository verification

**Files:**
- Verify only; no structural changes are permitted unless a test exposes a concrete build/link defect.

**Interfaces:**
- Consumes: completed C-BuildSystem import support, rasterizer shared target, minimal GAME consumer.
- Produces: verified shared-library boundary matching the approved spec.

- [ ] **Step 1: Verify rasterizer repository structure did not drift**

Run:

```sh
find Sources -maxdepth 2 -type f | sort
```

Expected: existing `Camera`, `Ecs`, `Models`, and `Renderer` files remain at their current paths. There must be no `Engine.cpp`, `Engine.hpp`, replacement `include/` API tree, or copied `GAME` source.

- [ ] **Step 2: Verify GAME remains exactly two project files**

Ignoring Git metadata and generated build/cache files, run:

```sh
find . -maxdepth 1 -type f -printf '%f\n' | sort
```

Expected project source/build-definition files:

```text
build.c
main.cpp
```

Do not add application wrappers, renderer copies, ECS copies, model copies, or helper source files.

- [ ] **Step 3: Rebuild both debug and release from clean state**

Rasterizer:

```sh
c clean
c build
c build --release
```

GAME:

```sh
c clean
c build
c build --release
```

Expected: all commands exit 0.

- [ ] **Step 4: Verify dynamic linkage in both profiles**

Linux:

```sh
ldd build/debug/game | grep 'libecs-model-rasterizer.so'
ldd build/release/game | grep 'libecs-model-rasterizer.so'
```

macOS:

```sh
otool -L build/debug/game | grep 'libecs-model-rasterizer.dylib'
otool -L build/release/game | grep 'libecs-model-rasterizer.dylib'
```

Expected: both profiles resolve the imported shared library.

- [ ] **Step 5: Run C-BuildSystem's full tests one final time**

```sh
make test
```

Expected: exit 0.

- [ ] **Step 6: Inspect final diffs for prohibited architecture**

Run in each changed repository:

```sh
git status --short
git diff HEAD~1 --stat
```

Confirm that no engine facade, source reorganization, rasterizer-source copy into GAME, or manual dynamic-loader API was introduced.

- [ ] **Step 7: Push the completed commits to `main` only after all verification is green**

```sh
git push origin main
```

Do not claim completion if any build, link, runtime-loader, or C-BuildSystem regression remains failing.
