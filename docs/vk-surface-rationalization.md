# Vulkan Surface Rationalization

This note tracks which parts of [C:\Projects\Tremor\vk.h](C:\Projects\Tremor\vk.h)
and [C:\Projects\Tremor\vk.cpp](C:\Projects\Tremor\vk.cpp) should remain core
runtime API, which should move to narrower services, and which should be treated
as temporary app/dev glue.

## Keep

These belong on the runtime backend surface because they answer "how do we talk
to Vulkan and submit a frame safely?":

- `VulkanBackend::initialize`
- `VulkanBackend::shutdown`
- `VulkanBackend::beginFrame`
- `VulkanBackend::endFrame`
- `VulkanBackend::getDevice`
- `VulkanBackend::getPhysicalDevice`
- `VulkanBackend::getGraphicsQueue`
- `VulkanBackend::getCurrentCommandBuffer`
- `VulkanBackend::getSwapchainExtent`
- `VulkanBackend::isFrameReady`
- `VulkanBackend::getUIRenderer`
- `VulkanBackend::getClusteredRenderer`
- `VulkanBackend::getOverlayManager`
- `VulkanBackend::enqueueUiMessage`

RHI-level classes that remain first-class:

- `VulkanDevice`
- `SwapChain`
- `Framebuffer`
- `DynamicRenderer`

## Move

These are useful engine capabilities, but they should not stay as broad public
methods on `VulkanBackend` forever:

- `createTexture`
- `createBuffer`
- `createShader`

Long-term home:

- renderer-facing asset/scene services
- render-core resource creation services
- gameplay/editor-facing scene submission helpers

The older scene-facing helpers (`loadMeshFromFile`,
`createMaterialFromDesc`, `addObjectToScene`) were dead public declarations
with no active engine callers, so they have already been removed from the
public `VulkanBackend` surface. If we reintroduce them, they should come back
as part of a narrower renderer-facing scene or asset submission service rather
than as backend methods.

The resource creation trio also had no active engine callers on the backend
surface, and `createTexture` was duplicating logic that already exists in
`VulkanResourceManager`. They have now been removed from `VulkanBackend` too.
If we need public runtime resource creation again, it should come back through a
small RHI/render-core service rather than as backend catch-all methods.

## Deprecate From Public Backend Surface

These are app/demo/editor/dev-tool helpers and should not remain public backend
API:

- `updateMeshShaderStatusLabel`
- `createEnhancedScene`
- `createTaffyScene`
- `createSceneLighting`
- `simpleColorCyclingTest`
- `initializeOverlayWorkflow`
- `initializeOverlaySystem`
- `createDevelopmentOverlays`
- `loadTestAssetWithOverlays`
- `updateOverlaySystem`
- `initializeUiMessageOverlay`
- `updateUiMessageOverlay`
- `createTestMasterAssetFromGLSL`
- `renderWithOverlays`
- `demonstrateOverlayControls`
- `loadShader(const std::string&)`

These should live in:

- app/bootstrap code near [C:\Projects\Tremor\main.cpp](C:\Projects\Tremor\main.cpp)
- dev overlay helpers
- editor integration services
- renderer-private implementation

Several of the older scene/bootstrap helpers were not just “misplaced”; they
were completely dead:

- `createEnhancedScene`
- `createTaffyMeshes`
- `createTaffyScene`
- `createSceneLighting`
- `simpleColorCyclingTest`
- `createTestMasterAssetFromGLSL`
- `renderWithOverlays`
- `demonstrateOverlayControls`
- `loadShader(const std::string&)`

Those have now been removed from the `VulkanBackend` surface entirely where no
live engine callers or matching implementations remained.

## Public Fields To Remove

These should not remain publicly mutable state:

- `hot_pink_enabled`
- `reload_assets_requested`
- `m_overlayManager`
- `m_taffyMeshShaderManager`
- `vkCmdDrawMeshTasksEXT_`

The first cleanup pass moves these behind private state and exposes only the
getter needed by current callers:

- `getOverlayManager()`

## Near-Term Cleanup Order

1. Route app/editor-facing backend controls through a narrower helper or bridge
   instead of leaving them on `VulkanBackend` directly.
2. Route any future public runtime resource creation through a narrow
   RHI/render-core service rather than re-expanding `VulkanBackend`.
3. Make dev/demo fields and overlay internals private immediately.
4. Move overlay/demo bootstrap helpers out of the public section and keep them
   backend-private.
5. Later, split legacy render-pass compatibility from dynamic rendering once
   UI/text/editor paths stop depending on the old topology.

The first step is now underway via
[C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_backend_controls.h](C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_backend_controls.h)
and
[C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_backend_controls.cpp](C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_backend_controls.cpp),
which own:

- input routing into backend UI/editor integration
- main-menu visibility toggling
- sequencer callback wiring

The next step is also underway via
[C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_editor_bridge.h](C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_editor_bridge.h)
and
[C:\Projects\Tremor\editor\vk_editor_bridge.cpp](C:\Projects\Tremor\editor\vk_editor_bridge.cpp),
which replace direct `ModelEditorIntegration` ownership on `VulkanBackend`
with a runtime-facing bridge interface implemented in the editor module.

The overlay/dev bootstrap path is now also moving behind
[C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_overlay_bridge.h](C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_overlay_bridge.h)
and
[C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_overlay_bridge.cpp](C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_overlay_bridge.cpp),
which now own:

- overlay hot-reload ticking
- development asset/bootstrap creation
- default test asset loading

The runtime UI/bootstrap path is now also moving behind
[C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_ui_bridge.h](C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_ui_bridge.h)
and
[C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_ui_bridge.cpp](C:\Projects\Tremor\Source\Runtime\TremorRenderer\vk_ui_bridge.cpp),
which now own:

- main menu / exit / editor button setup
- mesh shader status label setup and refresh
- transient UI message overlay setup and update
