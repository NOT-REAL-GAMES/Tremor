// Example integration of the Model Editor into Tremor's main loop
// This shows how to add the model editor to the existing engine

#include "model_editor.h"
#include "../main.h"
#include "../vk.h"

namespace tremor::editor {

    /**
     * Example of how to integrate the model editor into the main engine
     * This would be called from main.cpp or a similar entry point
     */
    class ModelEditorIntegration {
    public:
        ModelEditorIntegration(Engine& engine) : m_engine(engine) {}

        bool initialize() {
            Logger::get().info("Initializing Model Editor Integration");

            // Get renderer references from the engine
            auto* vulkanBackend = static_cast<tremor::gfx::VulkanBackend*>(m_engine.rb.get());
            if (!vulkanBackend) {
                Logger::get().error("Failed to get Vulkan backend");
                return false;
            }

            // TODO: Get clustered renderer reference
            // auto& clusteredRenderer = vulkanBackend->getClusteredRenderer();
            
            // TODO: Get UI renderer reference  
            // auto& uiRenderer = vulkanBackend->getUIRenderer();

            // For now, create placeholder references (this won't compile until renderers are available)
            // m_modelEditor = std::make_unique<ModelEditor>(clusteredRenderer, uiRenderer);
            
            // if (!m_modelEditor->initialize()) {
            //     Logger::get().error("Failed to initialize model editor");
            //     return false;
            // }

            // Set up keyboard shortcut to toggle editor
            m_editorEnabled = false;

            Logger::get().info("Model Editor Integration initialized");
            return true;
        }

        void update(float deltaTime) {
            if (m_modelEditor && m_editorEnabled) {
                m_modelEditor->update(deltaTime);
            }
        }

        void render(VkCommandBuffer commandBuffer, const glm::mat4& projection) {
            if (m_modelEditor && m_editorEnabled) {
                m_modelEditor->render(commandBuffer, projection);
            }
        }

        void handleInput(const SDL_Event& event) {
            // Toggle editor with F1 key
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F1) {
                toggleEditor();
                return;
            }

            // Pass input to editor if enabled
            if (m_modelEditor && m_editorEnabled) {
                m_modelEditor->handleInput(event);
            }
        }

        void toggleEditor() {
            m_editorEnabled = !m_editorEnabled;
            Logger::get().info("Model Editor {}", m_editorEnabled ? "ENABLED" : "DISABLED");
            
            if (m_editorEnabled) {
                Logger::get().info("Model Editor Controls:");
                Logger::get().info("  F1: Toggle editor on/off");
                Logger::get().info("  Esc: Select mode / Clear selection");
                Logger::get().info("  G: Move/translate mode");
                Logger::get().info("  R: Rotate mode");
                Logger::get().info("  S: Scale mode");
                Logger::get().info("  Ctrl+N: New model");
                Logger::get().info("  Ctrl+O: Open model");
                Logger::get().info("  Ctrl+S: Save model");
                Logger::get().info("  Mouse: Navigate viewport (Alt+Drag to orbit, Shift+Drag to pan, Wheel to zoom)");
                Logger::get().info("  Left Click: Select mesh/vertex (Shift+Click for vertex selection)");
            }
        }

        bool isEditorEnabled() const { return m_editorEnabled; }

    private:
        Engine& m_engine;
        std::unique_ptr<ModelEditor> m_modelEditor;
        bool m_editorEnabled = false;
    };

} // namespace tremor::editor

/*
// Example usage in main.cpp:

#include "editor/model_editor_integration.cpp"

// In Engine class:
class Engine {
    // ... existing members ...
    std::unique_ptr<tremor::editor::ModelEditorIntegration> m_editorIntegration;

public:
    Engine() {
        // ... existing initialization ...
        
        // Initialize model editor
        m_editorIntegration = std::make_unique<tremor::editor::ModelEditorIntegration>(*this);
        if (!m_editorIntegration->initialize()) {
            Logger::get().warning("Model Editor integration failed to initialize");
        }
    }

    bool Loop() {
        // ... existing event handling ...
        
        while (SDL_PollEvent(&event)) {
            // Let editor handle input first
            if (m_editorIntegration) {
                m_editorIntegration->handleInput(event);
                
                // Skip other input handling if editor is active and consumed the event
                if (m_editorIntegration->isEditorEnabled()) {
                    continue;
                }
            }

            // ... existing input handling ...
        }

        // ... existing update logic ...
        
        // Update editor
        if (m_editorIntegration) {
            m_editorIntegration->update(deltaTime);
        }

        // ... existing rendering ...
        
        rb.get()->beginFrame();
        
        // Render editor
        if (m_editorIntegration) {
            m_editorIntegration->render(commandBuffer, projectionMatrix);
        }
        
        rb.get()->endFrame();
        
        return true;
    }
};
*/