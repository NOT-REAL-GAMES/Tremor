#pragma once

#include "../vk.h"
#include "../gfx.h"
#include "sdf_text_renderer.h"
#include <glm/glm.hpp>
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace tremor::gfx {

    class SDFTextRenderer;

    // UI Element types
    enum class UIElementType {
        Button,
        Label,
        Panel,
        Checkbox,
        Slider
    };

    // UI Element state
    enum class UIElementState {
        Normal,
        Hovered,
        Pressed,
        Disabled
    };

    // Base UI Element
    struct UIElement {
        UIElementType type;
        UIElementState state = UIElementState::Normal;
        glm::vec2 position;
        glm::vec2 size;
        bool visible = true;
        bool enabled = true;
        uint32_t id;
        
        // Visual properties
        uint32_t backgroundColor = 0x202020FF;
        uint32_t hoverColor = 0x303030FF;
        uint32_t pressedColor = 0x404040FF;
        uint32_t borderColor = 0x505050FF;
        float borderWidth = 1.0f;
        
        // Callbacks
        std::function<void()> onClick;
        std::function<void()> onHover;
        
        // Check if point is inside element
        bool contains(glm::vec2 point) const {
            return point.x >= position.x && point.x <= position.x + size.x &&
                   point.y >= position.y && point.y <= position.y + size.y;
        }
    };

    // Button element
    struct UIButton : UIElement {
        std::string text;
        uint32_t textColor = 0xFFFFFFFF;
        float textScale = 0.5f;
        glm::vec2 textPadding = glm::vec2(10, 10);
        
        UIButton() { type = UIElementType::Button; }
    };

    // Label element
    struct UILabel : UIElement {
        std::string text;
        uint32_t textColor = 0xFFFFFFFF;
        float textScale = 0.5f;
        
        UILabel() { type = UIElementType::Label; }
    };

    // UI Renderer class
    class UIRenderer {
    public:
        UIRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
                  VkCommandPool commandPool, VkQueue graphicsQueue);
        ~UIRenderer();

        // Initialize with render pass info
        bool initialize(VkRenderPass renderPass, VkFormat colorFormat,
                       VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);

        // Set text renderer (must be called before rendering)
        void setTextRenderer(SDFTextRenderer* textRenderer) { 
            m_textRenderer = textRenderer; 
        }

        // Element management
        uint32_t addButton(const std::string& text, glm::vec2 position, glm::vec2 size,
                          std::function<void()> onClick = nullptr);
        uint32_t addLabel(const std::string& text, glm::vec2 position, uint32_t color = 0xFFFFFFFF);
        
        void removeElement(uint32_t id);
        void clearElements();
        
        // Update UI state with input
        void updateInput(const SDL_Event& event);
        void updateMousePosition(int x, int y);
        
        // Render all UI elements
        void render(VkCommandBuffer commandBuffer, const glm::mat4& projection);
        
        // Get/Set element properties
        UIElement* getElement(uint32_t id);
        void setElementVisible(uint32_t id, bool visible);
        void setElementEnabled(uint32_t id, bool enabled);
        void setElementPosition(uint32_t id, glm::vec2 position);
        
    private:
        // Vertex structure for UI quads
        struct UIVertex {
            glm::vec2 position;
            glm::vec2 texCoord;
            uint32_t color;
        };
        
        // Quad rendering
        void updateQuadBuffer();
        void renderQuads(VkCommandBuffer commandBuffer, const glm::mat4& projection);
        
        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        VkCommandPool m_commandPool;
        VkQueue m_graphicsQueue;
        VkSampleCountFlagBits m_sampleCount;
        
        // UI elements storage
        std::vector<std::unique_ptr<UIElement>> m_elements;
        uint32_t m_nextElementId = 1;
        
        // Input state
        glm::vec2 m_mousePosition;
        bool m_mousePressed = false;
        UIElement* m_hoveredElement = nullptr;
        UIElement* m_pressedElement = nullptr;
        
        // Text renderer reference
        SDFTextRenderer* m_textRenderer = nullptr;
        
        // Dirty flags for optimization
        bool m_textDirty = true;  // True when text needs to be regenerated
        bool m_quadsDirty = true; // True when quads need to be regenerated
        
        // Track previous frame's element states for optimization
        std::vector<UIElementState> m_previousStates;
        
        // Rendering resources
        VkPipeline m_pipeline;
        VkPipelineLayout m_pipelineLayout;
        VkDescriptorSetLayout m_descriptorSetLayout;
        VkDescriptorPool m_descriptorPool;
        VkDescriptorSet m_descriptorSet;
        
        // Vertex buffer for UI quads
        VkBuffer m_vertexBuffer;
        VkDeviceMemory m_vertexBufferMemory;
        size_t m_vertexBufferSize;
        std::vector<UIVertex> m_quadVertices;
        
        // Uniform buffer
        VkBuffer m_uniformBuffer;
        VkDeviceMemory m_uniformBufferMemory;
        
        // Helper functions
        bool createPipeline(VkRenderPass renderPass, VkFormat colorFormat);
        bool createDescriptorSets();
        bool createVertexBuffer(size_t size);
        void updateVertexBuffer();
        void renderQuad(VkCommandBuffer commandBuffer, glm::vec2 position, 
                       glm::vec2 size, uint32_t color);
        
        // Update element states based on mouse
        void updateElementStates();
        
        // Get appropriate color for element based on state
        uint32_t getElementColor(const UIElement* element) const;
    };

} // namespace tremor::gfx