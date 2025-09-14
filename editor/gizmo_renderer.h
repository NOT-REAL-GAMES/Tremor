#pragma once

#include "../vk.h"
#include "../gfx.h"
#include "model_editor.h"
#include <glm/glm.hpp>
#include <vector>

namespace tremor::editor {

    enum class EditorMode;

    /**
     * RAII wrapper for Vulkan buffer management in gizmos
     */
    class GizmoBuffer {
    public:
        GizmoBuffer(VkDevice device, VkPhysicalDevice physicalDevice);
        ~GizmoBuffer();
        
        // Non-copyable but movable
        GizmoBuffer(const GizmoBuffer&) = delete;
        GizmoBuffer& operator=(const GizmoBuffer&) = delete;
        GizmoBuffer(GizmoBuffer&& other) noexcept;
        GizmoBuffer& operator=(GizmoBuffer&& other) noexcept;
        
        // Create buffer with specified size and usage
        bool create(VkDeviceSize size, VkBufferUsageFlags usage, 
                   VkMemoryPropertyFlags properties, const std::string& name = "");
        
        // Update buffer data
        bool updateData(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
        
        // Getters
        VkBuffer getBuffer() const { return m_buffer; }
        VkDeviceMemory getMemory() const { return m_memory; }
        VkDeviceSize getSize() const { return m_size; }
        uint32_t getCapacity() const { return m_capacity; }
        uint32_t getCount() const { return m_count; }
        
        // Setters for count tracking
        void setCapacity(uint32_t capacity) { m_capacity = capacity; }
        void setCount(uint32_t count) { m_count = count; }
        
        // Check if buffer is valid
        bool isValid() const { return m_buffer != VK_NULL_HANDLE; }
        
    private:
        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        VkBuffer m_buffer;
        VkDeviceMemory m_memory;
        VkDeviceSize m_size;
        uint32_t m_capacity;
        uint32_t m_count;
        std::string m_name;
        
        void cleanup();
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    };

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

        // Render selected triangle edges using separate buffer to avoid conflicts
        void renderSelectedTriangleEdges(VkCommandBuffer commandBuffer,
                                        const std::vector<std::pair<glm::vec3, glm::vec3>>& edges,
                                        const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                        const glm::vec3& color = glm::vec3(0.2f, 1.0f, 0.3f));

        // Render multiple triangle sets using indirect draw calls for maximum performance
        struct TriangleDrawSet {
            std::vector<glm::vec3> vertices;
            std::vector<uint32_t> indices;
            glm::vec3 color;
            float alpha = 1.0f;
        };

        void renderTrianglesIndirect(VkCommandBuffer commandBuffer,
                                   const std::vector<TriangleDrawSet>& drawSets,
                                   const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                   bool enableBackfaceCulling = true);

        // Render multiple edge sets using indirect draw calls for maximum performance
        struct EdgeDrawSet {
            std::vector<std::pair<glm::vec3, glm::vec3>> edges;
            glm::vec3 color;
        };

        void renderEdgesIndirect(VkCommandBuffer commandBuffer,
                                const std::vector<EdgeDrawSet>& drawSets,
                                const glm::mat4& viewMatrix, const glm::mat4& projMatrix);
        
        // Render filled triangles (for selected triangles)
        void renderFilledTriangles(VkCommandBuffer commandBuffer,
                                  const std::vector<glm::vec3>& vertices,
                                  const std::vector<uint32_t>& indices,
                                  const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                  const glm::vec3& color = glm::vec3(0.8f, 0.3f, 0.3f),
                                  float alpha = 0.5f,
                                  bool enableBackfaceCulling = true);

        // Render selected triangles using separate buffer to avoid conflicts
        void renderSelectedTriangles(VkCommandBuffer commandBuffer,
                                   const std::vector<glm::vec3>& vertices,
                                   const std::vector<uint32_t>& indices,
                                   const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                   const glm::vec3& color = glm::vec3(0.3f, 1.0f, 0.4f),
                                   float alpha = 0.5f,
                                   bool enableBackfaceCulling = true);
        
        // Render mesh wireframe
        void renderWireframe(VkCommandBuffer commandBuffer,
                            const std::vector<glm::vec3>& vertices,
                            const std::vector<uint32_t>& indices,
                            const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                            const glm::vec3& color = glm::vec3(0.7f, 0.7f, 0.7f));

        // Hit testing for gizmo interaction
        int hitTest(EditorMode mode, const glm::vec2& screenPos, const glm::vec3& gizmoPos,
                   const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                   const glm::vec2& viewport);
        
        // Debug: Render a marker at mouse ray position
        void renderMouseRayDebug(VkCommandBuffer commandBuffer, const glm::vec2& screenPos,
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
        VkPipeline m_triangleNoCullPipeline;  // For filled geometry without backface culling
        VkPipelineLayout m_pipelineLayout;
        VkDescriptorSetLayout m_descriptorSetLayout;
        VkDescriptorPool m_descriptorPool;
        VkDescriptorSet m_descriptorSet;

        // RAII buffer management for different gizmo types
        GizmoBuffer m_translationVertexBuffer;
        GizmoBuffer m_rotationVertexBuffer; 
        GizmoBuffer m_scaleVertexBuffer;

        // Dynamic buffers for markers and edges
        GizmoBuffer m_vertexMarkerBuffer;
        GizmoBuffer m_selectedVertexMarkerBuffer;
        GizmoBuffer m_triangleEdgeBuffer;
        GizmoBuffer m_selectedTriangleEdgeBuffer;
        GizmoBuffer m_filledTriangleBuffer;
        GizmoBuffer m_selectedTriangleBuffer;
        GizmoBuffer m_wireframeBuffer;
        GizmoBuffer m_filledTriangleIndexBuffer;
        GizmoBuffer m_selectedTriangleIndexBuffer;
        GizmoBuffer m_vertexMarkerIndexBuffer;
        GizmoBuffer m_selectedVertexMarkerIndexBuffer;
        GizmoBuffer m_mouseRayDebugBuffer;

        // Indirect draw buffers for high-performance multi-draw rendering
        GizmoBuffer m_indirectDrawBuffer;
        GizmoBuffer m_indirectTriangleVertexBuffer;
        GizmoBuffer m_indirectTriangleIndexBuffer;

        // Indirect edge draw buffers
        GizmoBuffer m_indirectEdgeDrawBuffer;
        GizmoBuffer m_indirectEdgeVertexBuffer;

        // Index and uniform buffers
        GizmoBuffer m_indexBuffer;
        GizmoBuffer m_uniformBuffer;
        GizmoBuffer m_gizmoUniformBuffer;

        // Separate descriptor set for gizmo transforms  
        VkDescriptorSet m_gizmoDescriptorSet;

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
        
        // Ray casting utilities
        struct Ray {
            glm::vec3 origin;
            glm::vec3 direction; // normalized
        };
        
        Ray screenToWorldRay(const glm::vec2& screenPos, const glm::mat4& viewMatrix, 
                            const glm::mat4& projMatrix, const glm::vec2& viewport);
        float distanceFromRayToLineSegment(const Ray& ray, const glm::vec3& lineStart, 
                                          const glm::vec3& lineEnd, float& rayT, float& lineT);
        float distanceFromRayToCircle(const Ray& ray, const glm::vec3& circleCenter,
                                     const glm::vec3& circleNormal, float circleRadius);

        // Transform and rendering helpers
        void updateUniformBuffer(const glm::mat4& mvpMatrix, const glm::vec3& gizmoPos);
        void updateGizmoUniformBuffer(const glm::mat4& mvpMatrix, const glm::vec3& gizmoPos);
        float calculateScreenSpaceSize(const glm::vec3& worldPos, const glm::mat4& viewMatrix,
                                      const glm::mat4& projMatrix, const glm::vec2& viewport);

        // Hit testing helpers
        int hitTestTranslationGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                                   float screenSize, float tolerance, const glm::vec3& gizmoPos,
                                   const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                   const glm::vec2& viewport);
        int hitTestRotationGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                                float screenSize, float tolerance, const glm::vec3& gizmoPos,
                                const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                const glm::vec2& viewport);
        int hitTestScaleGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                             float screenSize, float tolerance, const glm::vec3& gizmoPos,
                             const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                             const glm::vec2& viewport);
        float distanceToLine(const glm::vec2& point, const glm::vec2& lineStart, 
                           const glm::vec2& lineEnd);
        bool pointInCircle(const glm::vec2& point, const glm::vec2& center, float radius);
    };

} // namespace tremor::editor