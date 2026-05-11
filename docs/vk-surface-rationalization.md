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

- `loadMeshFromFile`
- `createMaterialFromDesc`
- `addObjectToScene`
- `createTexture`
- `createBuffer`
- `createShader`

Long-term home:

- renderer-facing asset/scene services
- render-core resource creation services
- gameplay/editor-facing scene submission helpers

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

1. Keep `handleInput`, `setMainMenuVisible`, and `setSequencerCallback` public
   until app/editor routing has a narrower home.
2. Keep resource creation methods public for now, but treat them as migration
   candidates rather than stable final API.
3. Make dev/demo fields and overlay internals private immediately.
4. Move overlay/demo bootstrap helpers out of the public section and keep them
   backend-private.
5. Later, split legacy render-pass compatibility from dynamic rendering once
   UI/text/editor paths stop depending on the old topology.
