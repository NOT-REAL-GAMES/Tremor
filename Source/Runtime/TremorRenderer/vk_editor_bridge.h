#pragma once

#include "tremor_core.h"
#include "tremor_graphics_platform.h"

namespace tremor::gfx {

    class VulkanBackend;

    class VulkanEditorBridge {
    public:
        virtual ~VulkanEditorBridge() = default;

        virtual bool initialize() = 0;
        virtual void update(float deltaTime) = 0;
        virtual void render() = 0;
        virtual void handleInput(const SDL_Event& event) = 0;
        virtual bool isEditorEnabled() const = 0;
        virtual void setEditorEnabled(bool enabled) = 0;
        virtual void onUiRendered() = 0;
    };

    std::unique_ptr<VulkanEditorBridge> createVulkanEditorBridge(VulkanBackend& backend);

} // namespace tremor::gfx
