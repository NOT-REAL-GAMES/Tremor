#pragma once

#include "tremor_core.h"

namespace tremor::gfx {

    class VulkanBackend;

    class VulkanUiBridge {
    public:
        static void initializeRuntimeUi(VulkanBackend& backend);
        static void initializeMessageOverlay(VulkanBackend& backend);
        static void updateMessageOverlay(VulkanBackend& backend);
        static void updateMeshShaderStatusLabel(VulkanBackend& backend, bool meshShadersActive);
    };

} // namespace tremor::gfx
