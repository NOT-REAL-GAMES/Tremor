#pragma once

#include "tremor_core.h"
#include "tremor_graphics_platform.h"

namespace tremor::gfx {

    class VulkanBackend;

    class VulkanBackendControls {
    public:
        using SequencerCallback = std::function<void(int)>;

        static void handleInput(VulkanBackend& backend, const SDL_Event& event);
        static void setMainMenuVisible(VulkanBackend& backend, bool visible);
        static void setSequencerCallback(VulkanBackend& backend, SequencerCallback callback);
    };

} // namespace tremor::gfx
