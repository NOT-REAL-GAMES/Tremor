#pragma once

#include "../vk.h"
#include "../gfx.h"
#include <glm/glm.hpp>
#include <vector>

namespace tremor::editor {

    /**
     * Renders a 3D grid in the editor viewport
     */
    class GridRenderer {
    public:
        GridRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkCommandPool commandPool, VkQueue graphicsQueue);
        ~GridRenderer();

        // Initialize with render pass
        bool initialize(VkRenderPass renderPass, VkFormat colorFormat,
                       VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);

        // Render the grid
        void render(VkCommandBuffer commandBuffer, const glm::mat4& viewMatrix, 
                   const glm::mat4& projMatrix, VkExtent2D viewportExtent = {1920, 1080},
                   VkExtent2D scissorExtent = {1920, 1080});

        // Static flag to control grid rendering globally (for UI layering)
        static void setGlobalRenderingBlocked(bool blocked);
        static bool isGlobalRenderingBlocked();

    private:
        // Static flag for global rendering control
        static bool s_globalRenderingBlocked;

        // Configuration
        void setGridSize(float size) { m_gridSize = size; }
        void setGridSpacing(float spacing) { m_gridSpacing = spacing; }
        void setMajorLineInterval(int interval) { m_majorLineInterval = interval; }
        void setGridColor(const glm::vec3& color) { m_gridColor = color; }
        void setMajorGridColor(const glm::vec3& color) { m_majorGridColor = color; }

    private:
        // Vertex structure for grid lines
        struct GridVertex {
            glm::vec3 position;
            glm::vec3 color;
        };

        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        VkCommandPool m_commandPool;
        VkQueue m_graphicsQueue;
        VkSampleCountFlagBits m_sampleCount;

        // Grid configuration
        float m_gridSize = 50.0f;           // Grid extends from -size to +size
        float m_gridSpacing = 1.0f;         // Distance between grid lines
        int m_majorLineInterval = 10;       // Every Nth line is a major line
        glm::vec3 m_gridColor = glm::vec3(0.3f, 0.3f, 0.3f);      // Minor grid color
        glm::vec3 m_majorGridColor = glm::vec3(0.5f, 0.5f, 0.5f); // Major grid color

        // Rendering resources
        VkPipeline m_pipeline;
        VkPipelineLayout m_pipelineLayout;
        VkDescriptorSetLayout m_descriptorSetLayout;
        VkDescriptorPool m_descriptorPool;
        VkDescriptorSet m_descriptorSet;

        // Vertex buffer for grid lines
        VkBuffer m_vertexBuffer;
        VkDeviceMemory m_vertexBufferMemory;
        uint32_t m_vertexCount;

        // Uniform buffer for MVP matrix
        VkBuffer m_uniformBuffer;
        VkDeviceMemory m_uniformBufferMemory;

        // Shader modules
        VkShaderModule m_vertexShader;
        VkShaderModule m_fragmentShader;

        // Helper functions
        bool createShaders();
        bool createPipeline(VkRenderPass renderPass, VkFormat colorFormat);
        bool createDescriptorSets();
        bool createVertexBuffer();
        bool createUniformBuffer();
        void generateGridVertices(std::vector<GridVertex>& vertices);
        void updateUniformBuffer(const glm::mat4& mvpMatrix);
        
        // Vulkan helper functions
        bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkBuffer& buffer,
                         VkDeviceMemory& bufferMemory);
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    };

} // namespace tremor::editor