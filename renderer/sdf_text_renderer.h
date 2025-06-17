#pragma once

#include "../vk.h"
#include "../gfx.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

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
        
        // Persistent text mode (for UI)
        void beginPersistentText();  // Start building persistent text
        void endPersistentText();    // Finish and cache the text
        void renderPersistent(VkCommandBuffer commandBuffer, const glm::mat4& projection);
        
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
            std::unordered_map<uint32_t, const Taffy::FontChunk::Glyph*> glyphMap; // Fast lookup
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
        
        // Text caching - separate geometry from color
        struct CachedVertex {
            glm::vec2 pos;
            glm::vec2 uv;
        };
        
        struct TextGeometryCache {
            std::vector<CachedVertex> vertices;  // Position and UV data
            std::vector<uint32_t> textInstanceIndices; // Which text instance each vertex belongs to
            std::vector<uint32_t> vertexCounts; // Number of vertices per text instance
            bool dirty = true;  // Start dirty to force initial build
        };
        TextGeometryCache geometryCache_;
        
        // Keep track of text for cache invalidation
        std::vector<TextInstance> cachedTextInstances_;
        
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
        void rebuildGeometryCache();
        bool needsGeometryRebuild() const;
    };

} // namespace tremor::gfx