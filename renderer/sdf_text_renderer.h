#pragma once

#include "../vk.h"
#include "../gfx.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace tremor::gfx {

    // Text instance for rendering
    struct TextInstance {
        glm::vec2 position;
        float scale;
        float font_spacing;
        uint32_t color;  // Packed RGBA
        std::string text;
        uint32_t flags;  // Outline, shadow, etc.
    };

    class SDFTextRenderer {
    public:
        SDFTextRenderer(VkDevice device, VkPhysicalDevice physicalDevice, 
                       VkCommandPool commandPool, VkQueue graphicsQueue);
        ~SDFTextRenderer();

        // Initialize with render pass info
        bool initialize(VkRenderPass renderPass, VkFormat colorFormat, 
                       VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);
        
        // Load a font from TAF file
        bool loadFont(const std::string& fontPath);
        
        // Text rendering
        void addText(const TextInstance& text);
        void clearText();
        
        // Render all queued text
        void render(VkCommandBuffer commandBuffer, const glm::mat4& projection);
        
        // Text measurement
        glm::vec2 measureText(const std::string& text, float scale);

    private:
        VkDevice device_;
        VkPhysicalDevice physicalDevice_;
        VkCommandPool commandPool_;
        VkQueue graphicsQueue_;
        VkSampleCountFlagBits sampleCount_;
        
        // Font data
        struct FontData {
            VkImage texture;
            VkImageView textureView;
            VkDeviceMemory textureMemory;
            VkSampler sampler;
            
            std::vector<Taffy::FontChunk::Glyph> glyphs;
            float fontSize;
            float lineHeight;
            float ascent;
            float descent;
        };
        
        std::unique_ptr<FontData> currentFont_;
        
        // Rendering resources
        VkPipeline pipeline_;
        VkPipelineLayout pipelineLayout_;
        VkDescriptorSetLayout descriptorSetLayout_;
        VkDescriptorPool descriptorPool_;
        VkDescriptorSet descriptorSet_;
        
        // Text instance data
        std::vector<TextInstance> textInstances_;
        
        // Vertex buffer for text quads
        VkBuffer vertexBuffer_;
        VkDeviceMemory vertexBufferMemory_;
        size_t vertexBufferSize_;
        
        // Uniform buffer
        VkBuffer uniformBuffer_;
        VkDeviceMemory uniformBufferMemory_;
        
        // Helper functions
        bool createPipeline(VkRenderPass renderPass, VkFormat colorFormat);
        bool createDescriptorSets();
        bool createVertexBuffer(size_t size);
        void updateVertexBuffer();
    };

} // namespace tremor::gfx