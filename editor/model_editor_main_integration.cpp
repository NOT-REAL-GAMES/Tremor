// Integration example for adding Model Editor to Tremor's main engine
// This shows how to modify main.cpp to include the model editor

#include "model_editor_integration.h"
#include "../main.h"

/*
To integrate the model editor into the main engine, modify the Engine class in main.cpp as follows:

1. Add the integration member:
   std::unique_ptr<tremor::editor::ModelEditorIntegration> m_editorIntegration;

2. In the Engine constructor, after creating the VulkanBackend:
   
   Logger::get().critical("Creating RenderBackend...");
   rb = tremor::gfx::RenderBackend::create(window);
   Logger::get().critical("RenderBackend created: {}", (void*)rb.get());

   // ADD THIS: Initialize model editor
   auto* vulkanBackend = static_cast<tremor::gfx::VulkanBackend*>(rb.get());
   if (vulkanBackend) {
       m_editorIntegration = std::make_unique<tremor::editor::ModelEditorIntegration>(*vulkanBackend);
       if (!m_editorIntegration->initialize()) {
           Logger::get().warning("Model Editor failed to initialize");
       } else {
           Logger::get().info("Model Editor initialized successfully");
       }
   }

3. In the Loop() method, handle input first:

   bool Loop() {
       SDL_Event event{};
       while (SDL_PollEvent(&event)) {
           // ADD THIS: Let editor handle input first
           if (m_editorIntegration) {
               m_editorIntegration->handleInput(event);
               
               // Skip other input handling if editor is active and consumed the event
               if (m_editorIntegration->isEditorEnabled()) {
                   continue;
               }
           }

           // Pass event to render backend for UI handling
           if (rb) {
               static_cast<tremor::gfx::VulkanBackend*>(rb.get())->handleInput(event);
           }
           
           // ... existing input handling ...
       }

4. In the Loop() method, add update and render calls:

   // ADD THIS: Update editor
   if (m_editorIntegration) {
       m_editorIntegration->update(deltaTime);
   }

   rb.get()->beginFrame();
   
   // ADD THIS: Render editor
   if (m_editorIntegration) {
       m_editorIntegration->render();
   }
   
   rb.get()->endFrame();

5. In the Engine destructor:
   
   ~Engine() {
       // Clean up editor before other resources
       m_editorIntegration.reset();
       
       if (audioDevice != 0) {
           SDL_CloseAudioDevice(audioDevice);
       }
   }
*/

namespace tremor::editor {

    /**
     * Helper function to integrate model editor into existing Engine class
     * Call this from Engine constructor after VulkanBackend is created
     */
    std::unique_ptr<ModelEditorIntegration> createModelEditorIntegration(tremor::gfx::VulkanBackend& backend) {
        Logger::get().info("Creating Model Editor Integration");
        
        auto integration = std::make_unique<ModelEditorIntegration>(backend);
        if (!integration->initialize()) {
            Logger::get().error("Failed to initialize Model Editor Integration");
            return nullptr;
        }
        
        Logger::get().info("Model Editor Integration created successfully");
        Logger::get().info("Press F1 to toggle the model editor");
        return integration;
    }

    /**
     * Helper function to handle model editor input
     * Call this at the beginning of the main input loop
     */
    bool handleModelEditorInput(ModelEditorIntegration* integration, const SDL_Event& event) {
        if (!integration) return false;
        
        integration->handleInput(event);
        
        // Return true if editor is active and should consume input
        return integration->isEditorEnabled();
    }

    /**
     * Helper function to update model editor
     * Call this in the main update loop
     */
    void updateModelEditor(ModelEditorIntegration* integration, float deltaTime) {
        if (integration) {
            integration->update(deltaTime);
        }
    }

    /**
     * Helper function to render model editor
     * Call this after beginFrame() but before endFrame()
     */
    void renderModelEditor(ModelEditorIntegration* integration) {
        if (integration) {
            integration->render();
        }
    }

} // namespace tremor::editor

/*
Example modified Engine class structure:

class Engine {
    // ... existing members ...
    std::unique_ptr<tremor::editor::ModelEditorIntegration> m_editorIntegration;

public:
    Engine() : audioDevice(0) {
        // ... existing initialization ...
        
        // Create model editor after VulkanBackend
        auto* vulkanBackend = static_cast<tremor::gfx::VulkanBackend*>(rb.get());
        if (vulkanBackend) {
            m_editorIntegration = tremor::editor::createModelEditorIntegration(*vulkanBackend);
        }
    }

    ~Engine() {
        m_editorIntegration.reset();
        // ... existing cleanup ...
    }

    bool Loop() {
        static auto lastTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            // Handle model editor input first
            if (tremor::editor::handleModelEditorInput(m_editorIntegration.get(), event)) {
                continue; // Editor consumed the event
            }

            // ... existing input handling ...
        }

        // Update model editor
        tremor::editor::updateModelEditor(m_editorIntegration.get(), deltaTime);

        rb.get()->beginFrame();
        
        // Render model editor
        tremor::editor::renderModelEditor(m_editorIntegration.get());
        
        rb.get()->endFrame();
        
        return true;
    }
};
*/