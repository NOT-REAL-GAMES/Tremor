#pragma once

#include "tremor_core.h"

namespace tremor::gfx {

    class VulkanBackend;

    class VulkanOverlayBridge {
    public:
        static void initialize(VulkanBackend& backend);
        static void initializeWorkflow(VulkanBackend& backend);
        static void createDevelopmentAssets(VulkanBackend& backend);
        static void loadDefaultAssets(VulkanBackend& backend);
        static void loadTestAssetWithOverlays(VulkanBackend& backend);
        static void update(VulkanBackend& backend);
    };

} // namespace tremor::gfx
