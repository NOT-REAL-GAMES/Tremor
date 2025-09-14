#pragma once

#include "model_editor.h"
#include "../vk.h"
#include "../main.h"

namespace tremor::gfx {
    class UIRenderer;
    class VulkanBackend;
}

namespace tremor::editor {

    class ModelEditor;

    /**
     * Integration layer between ModelEditor and VulkanBackend
     * Manages the model editor lifecycle and integration with the main rendering system
     */
    class ModelEditorIntegration {
    public:
        ModelEditorIntegration(tremor::gfx::VulkanBackend& backend);
        ~ModelEditorIntegration();

        // Initialize the model editor
        bool initialize();
        void shutdown();

        // Main loop integration
        void update(float deltaTime);
        void render();
        void handleInput(const SDL_Event& event);

        // Editor state
        void toggleEditor();
        bool isEditorEnabled() const { return m_editorEnabled; }
        void setEditorEnabled(bool enabled);
        
        // Grid rendering control (to prevent callback loops)
        void setGridRenderingEnabled(bool enabled);

        // Model editor access
        ModelEditor* getEditor() const { return m_modelEditor.get(); }

    private:
        tremor::gfx::VulkanBackend& m_backend;
        std::unique_ptr<ModelEditor> m_modelEditor;
        tremor::gfx::UIRenderer* m_uiRenderer;
        
        bool m_editorEnabled = false;
        bool m_initialized = false;

        // Vulkan resources
        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        VkCommandPool m_commandPool = VK_NULL_HANDLE;

        // Helper methods
        bool createRenderPass();
        bool createCommandPool();
        void logEditorControls();
    };

} // namespace tremor::editor