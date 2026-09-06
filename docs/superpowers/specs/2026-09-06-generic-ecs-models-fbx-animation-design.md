# Generic ECS + Model Layout + Temporary FBX Design

## Goal

Make ECS permanently generic, clean the model loader into responsibility-based folders, add temporary FBX loading through `ufbx`, and define the future animation architecture without implementing animation yet.

## ECS

`Ecs/` owns only entity lifetime, component storage, typed lookup/query, and change tracking. It must not define engine/game components.

Target API:

```cpp
Ecs::World world;
Ecs::Entity e = world.createEntity();

world.add<Transform>(e, Transform{});
world.add<Camera>(e, Camera{});

auto *transform = world.get<Transform>(e);
world.remove<Camera>(e);

world.each<Transform, Camera>([](Ecs::Entity e, Transform& t, Camera& c) {
    // system work
});
```

Components are declared where they belong:

- camera-related component types: camera module
- renderer component types: renderer module
- animation component types: future animation module
- GAME-specific components: GAME repository

The ECS must never require edits when a new component type is introduced.

Implementation model:

- `World` contains a type-erased map keyed by `std::type_index`.
- Each component type owns a typed storage pool.
- Storage uses sparse entity-index lookup plus dense component/entity arrays so lookup is O(1) and iteration stays cache-friendly.
- `add<T>()`, `get<T>()`, `has<T>()`, `remove<T>()`, and `each<Ts...>()` are template APIs in `Ecs.hpp`.
- `Ecs.cpp` contains only non-template entity/lifetime/change-revision logic.
- Entity IDs remain lightweight integer handles for now.
- `World::changeRevision()` remains available for renderer cache invalidation.

Existing concrete ECS methods (`addTransform`, `addCamera`, `addMesh`, etc.) are removed after all consumers migrate.

## Component ownership

Move current component definitions out of `Ecs.hpp`.

Suggested ownership:

```text
Sources/
├── Camera.hpp
│   └── CameraComponent
├── Renderer/
│   ├── Render.hpp
│   │   ├── Transform
│   │   ├── MeshComponent
│   │   ├── RenderableComponent
│   │   └── LightComponent
│   └── Gi/
└── Ecs/
    ├── Ecs.hpp
    └── Ecs.cpp
```

If a type is shared by multiple systems, put it in the smallest neutral module that semantically owns it rather than putting it back into ECS.

## Models cleanup

Target layout:

```text
Sources/Models/
├── Models.cpp
├── Models.hpp
├── Core/
│   ├── Types.hpp
│   ├── Material.cpp
│   ├── Material.hpp
│   ├── Texture.cpp
│   └── Texture.hpp
├── Formats/
│   ├── Obj.cpp
│   ├── Obj.hpp
│   ├── Fbx.cpp
│   └── Fbx.hpp
├── Images/
│   ├── Tga.cpp
│   └── Tga.hpp
└── ThirdParty/
    └── ufbx.h
```

`Models::load()` remains the only normal public loading entry point and dispatches by extension.

OBJ keeps the existing custom parser.

FBX is temporary through `ufbx`:

- load static mesh geometry
- indices/vertices/normals/UVs
- material color/texture references where available
- preserve skeleton/animation information in import-side structures only if needed for future compatibility
- do not expose or implement runtime animation yet

`ufbx` is isolated under `Models/ThirdParty` so replacing it later does not affect public APIs.

## Future animation architecture — design only

Do not implement this in this change.

Runtime concepts:

```text
Skeleton
AnimationClip
Pose
AnimatorComponent
AnimationSystem
AnimationStateMachine
```

Recommended separation:

- `Skeleton`: parent indices + inverse bind matrices + bind pose
- `AnimationClip`: immutable compressed/keyframed channels
- `Pose`: transient local/global bone transforms
- `AnimatorComponent`: current state, current clip/time, blend target/time, speed
- `AnimationSystem`: evaluates all entities with `AnimatorComponent` + skeleton/model binding
- state machine is data/gameplay driven; states such as `IDLE`, `WALK`, `HOLSTER`, `RELOAD` are not engine enums

Example future GAME-side component/state data:

```cpp
struct WeaponAnimationState {
    enum class State { Idle, Fire, Reload, Holster } state = State::Idle;
};
```

The generic ECS stores this without knowing what it means.

For skeletal rendering, the future flow is:

```text
AnimationStateMachine
    -> chooses/blends clips
AnimationSystem
    -> evaluates local pose
    -> hierarchy/global pose
    -> skin matrices
Renderer
    -> GPU skinning
```

Avoid one ECS per subsystem. Use one generic `World`; systems are ordinary functions/classes operating on typed component queries.

## GI note

The current screenshot indicates direct shadowing works but indirect energy is still insufficient. Correct target behavior is not to erase shadows; direct illumination remains occluded while diffuse/environment bounce fills shadowed areas. GI tuning/algorithm improvement is a separate renderer task from this ECS/model refactor.

## Compatibility

- Preserve `Models::load()`, `Models::mesh()`, `Models::material()`, `Models::part()`, `Models::partCount()`, and model handles where practical.
- Renderer/GAME source changes are allowed to migrate from named ECS methods to generic typed calls.
- No animation runtime implementation in this scope.
- `ufbx` is explicitly temporary and hidden behind `Formats/Fbx.*`.
