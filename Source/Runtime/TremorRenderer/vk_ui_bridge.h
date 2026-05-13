#pragma once

#include "tremor_core.h"

namespace tremor::gfx {

    class VulkanBackend;

    class VulkanUiBridge {
    public:
        static void initializeRuntimeUi(VulkanBackend& backend);
        static void initializeMessageOverlay(VulkanBackend& backend);
        static void initializeProfilerOverlay(VulkanBackend& backend);
        static void updateMessageOverlay(VulkanBackend& backend);
        static void updateProfilerOverlay(VulkanBackend& backend);
        static void updateMeshShaderStatusLabel(VulkanBackend& backend, bool meshShadersActive);
        static void setProfilerOverlayVisible(VulkanBackend& backend, bool visible);
    };

} // namespace tremor::gfx
