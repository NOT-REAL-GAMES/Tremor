# Tremor Engine Module Refactor Roadmap

## Goal

Refactor Tremor toward a layered engine structure closer to:

```text
Source/
  Foundation/
  Runtime/
  Editor/
  Tools/
  Extensions/
```

with the hard dependency rules:

- `Foundation` cannot depend on `Runtime`
- `Runtime` cannot depend on `Editor`
- `Editor` must not leak into cook/runtime builds

This is not a cosmetic folder move. The purpose is to make build boundaries,
ownership, and future runtime/editor separation real.

## Current Pressure Points

These are the main reasons the refactor is worth doing:

1. `main.h` is still a broad umbrella header with platform, SDL, Vulkan, and
   miscellaneous utility concerns.
2. `vk.h` / `vk.cpp` own too many responsibilities at once: swapchain
   lifecycle, renderer coordination, overlays, editor integration, and app-ish
   orchestration.
3. `dmc_survivors.h` is still a large gameplay choke point even as more
   behavior moves into Taffyscript.
4. Editor code is compiled directly into the same target and currently has
   broad access to runtime internals.
5. CMake previously described the entire engine as one flat list of sources,
   which made intended ownership harder to see and enforce.

## Migration Strategy

We should migrate in layers rather than move every file immediately.

### Phase 1: Structural Scaffolding

Land the target shape without destabilizing the build:

- add `Source/` as the canonical destination for future modules
- define a module ownership map in CMake
- group sources in the IDE according to the future engine layout
- document dependency rules and current-to-target mapping

This phase is now in place via:

- [C:\Projects\Tremor\cmake\TremorModules.cmake](C:\Projects\Tremor\cmake\TremorModules.cmake)
- [C:\Projects\Tremor\Source\README.md](C:\Projects\Tremor\Source\README.md)
- initial low-level Vulkan split in [C:\Projects\Tremor\vk_rhi.h](C:\Projects\Tremor\vk_rhi.h)
- first physical runtime header moves under `Source/Runtime/...`, including
  [C:\Projects\Tremor\Source\Runtime\TremorRenderCore\gfx_resource_types.h](C:\Projects\Tremor\Source\Runtime\TremorRenderCore\gfx_resource_types.h),
  [C:\Projects\Tremor\Source\Runtime\TremorRenderCore\gfx_resource_handles.h](C:\Projects\Tremor\Source\Runtime\TremorRenderCore\gfx_resource_handles.h),
  [C:\Projects\Tremor\Source\Runtime\TremorRHI\vk_resource_wrappers.h](C:\Projects\Tremor\Source\Runtime\TremorRHI\vk_resource_wrappers.h),
  [C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_renderer_support.h](C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_renderer_support.h),
  [C:\Projects\Tremor\Source\Runtime\TremorRenderer\taffy_mesh.h](C:\Projects\Tremor\Source\Runtime\TremorRenderer\taffy_mesh.h),
  and [C:\Projects\Tremor\Source\Runtime\TremorRenderer\taffy_integration.h](C:\Projects\Tremor\Source\Runtime\TremorRenderer\taffy_integration.h)

### Phase 2: Foundation Extraction

First clean split targets:

- `logger.h`
- `mem.h`
- `handle.h`
- `res.h`
- eventual platform/bootstrap split from `main.h`

Target result:

- `Foundation/TremorCore`
- `Foundation/TremorMemory`
- `Foundation/TremorPlatform`

Priority work:

- break `main.h` apart
- isolate SDL/Vulkan/platform includes from generic engine utilities
- move low-level helpers out of renderer/gameplay headers

### Phase 3: Runtime Core Separation

Split the current runtime into explicit ownership zones:

- `TremorAssets`
  - `gltf_importer.*`
  - Taffy-facing runtime asset loading glue

- `TremorGameplay`
  - `flecs_interpreter.*`
  - `ui_message_*`
  - `script_ecs_components.*`
  - `script_render_system.*`
  - eventually the remaining `DMC Survivors` host shell

- `TremorPhysics`
  - `dmc_physics.*`
  - `jolt_physics_world.*`
  - `jolt_physics_adapter.*`
  - `physics_interop.*`

- `TremorAudio`
  - `audio/taffy_audio_processor.*`
  - `audio/taffy_polyphonic_processor.*`

- `TremorRHI`
  - `vk_rhi.h`
  - `vk.*`
  - `volk.*`
  - low-level render hardware interface and swapchain/device/resource lifetime

- `TremorRenderer`
  - `vk_renderer_support.h`
  - `renderer/*`
  - scene submission, UI/text rendering, Taffy renderer integration

- `TremorVM`
  - `vm*`
  - likely kept dormant or optional for 1.0, but physically separated

### Phase 4: Editor Isolation

Move `editor/*` behind clearer module seams:

- `TremorEditorCore`
- future specialized editors like `TremorAssetEditor` / `TremorSceneEditor`

Priority work:

- stop editor code from reaching into runtime through broad umbrella headers
- reduce direct dependencies on `vk.h`
- introduce runtime-facing service interfaces for editor rendering and asset
  manipulation

### Phase 5: Build Target Split

Once module ownership is physically cleaner:

- split monolithic `Tremor` executable sources into actual static/object
  libraries per module
- create runtime-only and editor-enabled targets
- gate editor code behind a build option without stubbing half the engine

This is the phase where the “Editor cannot leak into runtime” rule becomes a
real linker/build rule instead of a convention.

## Current-to-Target Ownership Map

### Foundation

- `logger.h` -> `Foundation/TremorCore`
- `main.h` -> split across `TremorCore` and `TremorPlatform`
- `mem.h` -> `Foundation/TremorMemory`
- `handle.h`, `res.h` -> likely `Foundation/TremorCore` or `TremorContainers`
- `gfx.h` -> likely split between math/scene/render-core concerns

### Runtime

- `vk.*`, `volk.*` -> `TremorRHI`
- `vk_rhi.h` -> `TremorRHI`
- `vk_renderer_support.h`, `renderer/*` -> `TremorRenderer`
- `flecs_interpreter.*`, `ui_message_*`, `script_*`, `render_interop.*` ->
  `TremorGameplay`
- `dmc_physics.*`, `jolt_physics_*`, `physics_interop.*` -> `TremorPhysics`
- `audio/*` -> `TremorAudio`
- `gltf_importer.*` -> `TremorAssets`
- `vm*` -> `TremorVM`

### Editor

- `editor/*` -> `TremorEditorCore` to start

## Immediate Next Refactors

The next best structural moves are:

1. Extract `main.h` into smaller headers:
   - bootstrap/app concerns
   - platform/SDL/Vulkan includes
   - generic engine utility types

2. Split `vk.h` / `vk.cpp` into:
   - `TremorRHI` device/swapchain/resource management
   - `TremorRenderer` scene/UI/text/editor-facing rendering

3. Reduce `dmc_survivors.h` into:
   - gameplay host shell
   - script callback registration
   - temporary native fallback systems only

4. Start physically moving leaf modules into `Source/Runtime/...` and
   `Source/Editor/...` once include paths are ready.

## Success Criteria

We are done when:

- the build graph communicates the same layering we intend architecturally
- runtime builds can omit editor code
- `Foundation` no longer drags SDL/Vulkan/gameplay/editor headers around
- `DMC Survivors` is mostly Taffyscript and host services rather than a giant
  gameplay header
