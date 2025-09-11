#pragma once

#include "../vk.h"
#include "../gfx.h"
#include "model_editor.h"
#include <glm/glm.hpp>
#include <vector>

namespace tremor::editor {

    enum class EditorMode;

    /**
     * Renders transform gizmos (move, rotate, scale) in the editor viewport
     */
    class GizmoRenderer {
    public:
        GizmoRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
                     VkCommandPool commandPool, VkQueue graphicsQueue);
        ~GizmoRenderer();

        // Initialize with render pass
        bool initialize(VkRenderPass renderPass, VkFormat colorFormat,
                       VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);

        // Render gizmo at specified position
        void renderGizmo(VkCommandBuffer commandBuffer, EditorMode mode,
                        const glm::vec3& position, const glm::mat4& viewMatrix,
                        const glm::mat4& projMatrix, int activeAxis = -1);

        // Render vertex markers at specified positions
        void renderVertexMarkers(VkCommandBuffer commandBuffer, 
                                const std::vector<glm::vec3>& positions,
                                const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                const glm::vec3& color = glm::vec3(1.0f, 1.0f, 0.0f),
                                float size = 0.1f);

        // Render selected vertex markers (uses separate buffer to avoid conflicts)
        void renderSelectedVertexMarkers(VkCommandBuffer commandBuffer, 
                                        const std::vector<glm::vec3>& positions,
                                        const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                        const glm::vec3& color = glm::vec3(1.0f, 0.3f, 0.3f),
                                        float size = 0.1f);

        // Render triangle edges
        void renderTriangleEdges(VkCommandBuffer commandBuffer,
                                const std::vector<std::pair<glm::vec3, glm::vec3>>& edges,
                                const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                const glm::vec3& color = glm::vec3(0.0f, 1.0f, 0.0f));

        // Hit testing for gizmo interaction
        int hitTest(EditorMode mode, const glm::vec2& screenPos, const glm::vec3& gizmoPos,
                   const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                   const glm::vec2& viewport);

        // Configuration
        void setGizmoSize(float size) { m_gizmoSize = size; }
        void setAxisColors(const glm::vec3& x, const glm::vec3& y, const glm::vec3& z) {
            m_xAxisColor = x; m_yAxisColor = y; m_zAxisColor = z;
        }

    private:
        // Vertex structure for gizmo geometry
        struct GizmoVertex {
            glm::vec3 position;
            glm::vec3 color;
        };

        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        VkCommandPool m_commandPool;
        VkQueue m_graphicsQueue;
        VkSampleCountFlagBits m_sampleCount;

        // Gizmo configuration
        float m_gizmoSize = 1.0f;
        glm::vec3 m_xAxisColor = glm::vec3(1.0f, 0.3f, 0.3f); // Red
        glm::vec3 m_yAxisColor = glm::vec3(0.3f, 1.0f, 0.3f); // Green
        glm::vec3 m_zAxisColor = glm::vec3(0.3f, 0.3f, 1.0f); // Blue
        glm::vec3 m_highlightColor = glm::vec3(1.0f, 1.0f, 0.3f); // Yellow

        // Rendering resources
        VkPipeline m_linePipeline;      // For lines (translation, scale handles)
        VkPipeline m_trianglePipeline;  // For filled geometry (arrows, rotation circles)
        VkPipelineLayout m_pipelineLayout;
        VkDescriptorSetLayout m_descriptorSetLayout;
        VkDescriptorPool m_descriptorPool;
        VkDescriptorSet m_descriptorSet;

        // Vertex buffers for different gizmo types
        VkBuffer m_translationVertexBuffer;
        VkDeviceMemory m_translationVertexBufferMemory;
        uint32_t m_translationVertexCount;

        VkBuffer m_rotationVertexBuffer;
        VkDeviceMemory m_rotationVertexBufferMemory;
        uint32_t m_rotationVertexCount;

        VkBuffer m_scaleVertexBuffer;
        VkDeviceMemory m_scaleVertexBufferMemory;
        uint32_t m_scaleVertexCount;

        // Dynamic vertex buffer for markers
        VkBuffer m_vertexMarkerBuffer;
        VkDeviceMemory m_vertexMarkerBufferMemory;
        uint32_t m_vertexMarkerCapacity;
        uint32_t m_vertexMarkerCount;

        // Dedicated index buffer for vertex markers
        VkBuffer m_vertexMarkerIndexBuffer;
        VkDeviceMemory m_vertexMarkerIndexBufferMemory;
        uint32_t m_vertexMarkerIndexCapacity;
        uint32_t m_vertexMarkerIndexCount;

        // Dynamic vertex buffer for triangle edges
        VkBuffer m_triangleEdgeBuffer;
        VkDeviceMemory m_triangleEdgeBufferMemory;
        uint32_t m_triangleEdgeCapacity;
        uint32_t m_triangleEdgeCount;

        // Dynamic vertex buffer for selected vertex markers
        VkBuffer m_selectedVertexMarkerBuffer;
        VkDeviceMemory m_selectedVertexMarkerBufferMemory;
        uint32_t m_selectedVertexMarkerCapacity;
        uint32_t m_selectedVertexMarkerCount;

        // Dedicated index buffer for selected vertex markers
        VkBuffer m_selectedVertexMarkerIndexBuffer;
        VkDeviceMemory m_selectedVertexMarkerIndexBufferMemory;
        uint32_t m_selectedVertexMarkerIndexCapacity;
        uint32_t m_selectedVertexMarkerIndexCount;

        // Index buffers
        VkBuffer m_indexBuffer;
        VkDeviceMemory m_indexBufferMemory;
        uint32_t m_indexCount;

        // Uniform buffer for transforms
        VkBuffer m_uniformBuffer;
        VkDeviceMemory m_uniformBufferMemory;

        // Shader modules
        VkShaderModule m_vertexShader;
        VkShaderModule m_fragmentShader;

        // Helper functions
        bool createShaders();
        bool createPipelines(VkRenderPass renderPass, VkFormat colorFormat);
        bool createDescriptorSets();
        bool createVertexBuffers();
        bool createUniformBuffer();

        // Geometry generation
        void generateTranslationGizmo(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices);
        void generateRotationGizmo(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices);
        void generateScaleGizmo(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices);

        // Utility functions
        void generateArrow(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices,
                          const glm::vec3& start, const glm::vec3& end, const glm::vec3& color,
                          uint32_t& vertexOffset);
        void generateCircle(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices,
                           const glm::vec3& center, const glm::vec3& normal, float radius,
                           const glm::vec3& color, uint32_t segments, uint32_t& vertexOffset);
        void generateBox(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices,
                        const glm::vec3& position, float size, const glm::vec3& color,
                        uint32_t& vertexOffset);

        // Transform and rendering helpers
        void updateUniformBuffer(const glm::mat4& mvpMatrix, const glm::vec3& gizmoPos);
        float calculateScreenSpaceSize(const glm::vec3& worldPos, const glm::mat4& viewMatrix,
                                      const glm::mat4& projMatrix, const glm::vec2& viewport);

        // Hit testing helpers
        int hitTestTranslationGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                                   float screenSize, float tolerance);
        int hitTestRotationGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                                float screenSize, float tolerance);
        int hitTestScaleGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                             float screenSize, float tolerance);
        float distanceToLine(const glm::vec2& point, const glm::vec2& lineStart, 
                           const glm::vec2& lineEnd);
        bool pointInCircle(const glm::vec2& point, const glm::vec2& center, float radius);

        // Vulkan helper functions
        bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkBuffer& buffer,
                         VkDeviceMemory& bufferMemory);
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    };

} // namespace tremor::editor