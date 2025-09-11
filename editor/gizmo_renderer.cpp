#include "gizmo_renderer.h"
#include "../main.h"
#include <array>
#include <cstring>
#include <fstream>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace tremor::editor {

    // =============================================================================
    // GizmoRenderer Implementation
    // =============================================================================

    GizmoRenderer::GizmoRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
                                VkCommandPool commandPool, VkQueue graphicsQueue)
        : m_device(device), m_physicalDevice(physicalDevice),
          m_commandPool(commandPool), m_graphicsQueue(graphicsQueue),
          m_linePipeline(VK_NULL_HANDLE), m_trianglePipeline(VK_NULL_HANDLE),
          m_pipelineLayout(VK_NULL_HANDLE), m_descriptorSetLayout(VK_NULL_HANDLE),
          m_descriptorPool(VK_NULL_HANDLE), m_descriptorSet(VK_NULL_HANDLE),
          m_translationVertexBuffer(VK_NULL_HANDLE), m_translationVertexBufferMemory(VK_NULL_HANDLE),
          m_rotationVertexBuffer(VK_NULL_HANDLE), m_rotationVertexBufferMemory(VK_NULL_HANDLE),
          m_scaleVertexBuffer(VK_NULL_HANDLE), m_scaleVertexBufferMemory(VK_NULL_HANDLE),
          m_indexBuffer(VK_NULL_HANDLE), m_indexBufferMemory(VK_NULL_HANDLE),
          m_uniformBuffer(VK_NULL_HANDLE), m_uniformBufferMemory(VK_NULL_HANDLE),
          m_vertexShader(VK_NULL_HANDLE), m_fragmentShader(VK_NULL_HANDLE),
          m_translationVertexCount(0), m_rotationVertexCount(0), m_scaleVertexCount(0),
          m_indexCount(0),
          m_vertexMarkerBuffer(VK_NULL_HANDLE), m_vertexMarkerBufferMemory(VK_NULL_HANDLE),
          m_vertexMarkerCapacity(0), m_vertexMarkerCount(0),
          m_vertexMarkerIndexBuffer(VK_NULL_HANDLE), m_vertexMarkerIndexBufferMemory(VK_NULL_HANDLE),
          m_vertexMarkerIndexCapacity(0), m_vertexMarkerIndexCount(0),
          m_triangleEdgeBuffer(VK_NULL_HANDLE), m_triangleEdgeBufferMemory(VK_NULL_HANDLE),
          m_triangleEdgeCapacity(0), m_triangleEdgeCount(0),
          m_selectedVertexMarkerBuffer(VK_NULL_HANDLE), m_selectedVertexMarkerBufferMemory(VK_NULL_HANDLE),
          m_selectedVertexMarkerCapacity(0), m_selectedVertexMarkerCount(0),
          m_selectedVertexMarkerIndexBuffer(VK_NULL_HANDLE), m_selectedVertexMarkerIndexBufferMemory(VK_NULL_HANDLE),
          m_selectedVertexMarkerIndexCapacity(0), m_selectedVertexMarkerIndexCount(0) {
    }

    GizmoRenderer::~GizmoRenderer() {
        if (m_device != VK_NULL_HANDLE) {
            // Clean up Vulkan resources
            if (m_translationVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_translationVertexBuffer, nullptr);
            }
            if (m_translationVertexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_translationVertexBufferMemory, nullptr);
            }
            if (m_rotationVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_rotationVertexBuffer, nullptr);
            }
            if (m_rotationVertexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_rotationVertexBufferMemory, nullptr);
            }
            if (m_scaleVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_scaleVertexBuffer, nullptr);
            }
            if (m_scaleVertexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_scaleVertexBufferMemory, nullptr);
            }
            if (m_indexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
            }
            if (m_indexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
            }
            if (m_uniformBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_uniformBuffer, nullptr);
            }
            if (m_uniformBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_uniformBufferMemory, nullptr);
            }
            if (m_vertexMarkerBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_vertexMarkerBuffer, nullptr);
            }
            if (m_vertexMarkerBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_vertexMarkerBufferMemory, nullptr);
            }
            if (m_vertexMarkerIndexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_vertexMarkerIndexBuffer, nullptr);
            }
            if (m_vertexMarkerIndexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_vertexMarkerIndexBufferMemory, nullptr);
            }
            if (m_triangleEdgeBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_triangleEdgeBuffer, nullptr);
            }
            if (m_triangleEdgeBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_triangleEdgeBufferMemory, nullptr);
            }
            if (m_selectedVertexMarkerBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_selectedVertexMarkerBuffer, nullptr);
            }
            if (m_selectedVertexMarkerBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_selectedVertexMarkerBufferMemory, nullptr);
            }
            if (m_selectedVertexMarkerIndexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_selectedVertexMarkerIndexBuffer, nullptr);
            }
            if (m_selectedVertexMarkerIndexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_selectedVertexMarkerIndexBufferMemory, nullptr);
            }
            if (m_descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
            }
            if (m_descriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
            }
            if (m_linePipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(m_device, m_linePipeline, nullptr);
            }
            if (m_trianglePipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(m_device, m_trianglePipeline, nullptr);
            }
            if (m_pipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
            }
            if (m_vertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(m_device, m_vertexShader, nullptr);
            }
            if (m_fragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(m_device, m_fragmentShader, nullptr);
            }
        }
    }

    bool GizmoRenderer::initialize(VkRenderPass renderPass, VkFormat colorFormat,
                                  VkSampleCountFlagBits sampleCount) {
        m_sampleCount = sampleCount;

        Logger::get().info("Initializing GizmoRenderer");

        if (!createShaders()) {
            Logger::get().error("Failed to create gizmo shaders");
            return false;
        }

        if (!createDescriptorSets()) {
            Logger::get().error("Failed to create gizmo descriptor sets");
            return false;
        }

        if (!createPipelines(renderPass, colorFormat)) {
            Logger::get().error("Failed to create gizmo pipelines");
            return false;
        }

        if (!createVertexBuffers()) {
            Logger::get().error("Failed to create gizmo vertex buffers");
            return false;
        }

        if (!createUniformBuffer()) {
            Logger::get().error("Failed to create gizmo uniform buffer");
            return false;
        }

        Logger::get().info("GizmoRenderer initialized successfully");
        return true;
    }

    void GizmoRenderer::renderGizmo(VkCommandBuffer commandBuffer, EditorMode mode,
                                   const glm::vec3& position, const glm::mat4& viewMatrix,
                                   const glm::mat4& projMatrix, int activeAxis) {
        // Update uniform buffer
        glm::mat4 mvpMatrix = projMatrix * viewMatrix;
        updateUniformBuffer(mvpMatrix, position);

        // Bind descriptor sets
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        // Select appropriate vertex buffer and pipeline based on mode
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        uint32_t vertexCount = 0;
        VkPipeline pipeline = m_linePipeline;

        switch (mode) {
            case EditorMode::Move:
                vertexBuffer = m_translationVertexBuffer;
                vertexCount = m_translationVertexCount;
                pipeline = m_linePipeline; // Translation gizmo uses lines
                break;
            case EditorMode::Rotate:
                vertexBuffer = m_rotationVertexBuffer;
                vertexCount = m_rotationVertexCount;
                pipeline = m_linePipeline; // Rotation gizmo uses line circles
                break;
            case EditorMode::Scale:
                vertexBuffer = m_scaleVertexBuffer;
                vertexCount = m_scaleVertexCount;
                pipeline = m_linePipeline; // Scale gizmo uses lines with boxes
                break;
            default:
                return; // No gizmo for select mode
        }

        if (vertexBuffer == VK_NULL_HANDLE || vertexCount == 0) {
            return;
        }

        // Bind pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Draw gizmo
        if (m_indexCount > 0) {
            vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);
        } else {
            vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
        }
    }

    void GizmoRenderer::renderVertexMarkers(VkCommandBuffer commandBuffer, 
                                           const std::vector<glm::vec3>& positions,
                                           const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                           const glm::vec3& color, float size) {
        if (positions.empty() || m_linePipeline == VK_NULL_HANDLE) {
            return;
        }

        // Generate vertex marker geometry (small boxes for each vertex)
        std::vector<GizmoVertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t vertexOffset = 0;

        // Generate a small box for each vertex position
        for (const auto& pos : positions) {
            generateBox(vertices, indices, pos, size, color, vertexOffset);
        }

        // Check if we need to recreate the vertex buffer
        if (m_vertexMarkerBuffer == VK_NULL_HANDLE || vertices.size() > m_vertexMarkerCapacity) {
            // Clean up old buffer if it exists
            if (m_vertexMarkerBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_vertexMarkerBuffer, nullptr);
                vkFreeMemory(m_device, m_vertexMarkerBufferMemory, nullptr);
            }

            // Create new buffer with extra capacity
            m_vertexMarkerCapacity = vertices.size() * 2; // Double capacity for growth
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * m_vertexMarkerCapacity;
            
            if (!createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_vertexMarkerBuffer, m_vertexMarkerBufferMemory)) {
                Logger::get().error("Failed to create vertex marker buffer");
                return;
            }
        }

        // Update vertex buffer with new data
        void* data;
        vkMapMemory(m_device, m_vertexMarkerBufferMemory, 0, sizeof(GizmoVertex) * vertices.size(), 0, &data);
        memcpy(data, vertices.data(), sizeof(GizmoVertex) * vertices.size());
        vkUnmapMemory(m_device, m_vertexMarkerBufferMemory);

        m_vertexMarkerCount = vertices.size();

        // Update uniform buffer
        glm::mat4 mvpMatrix = projMatrix * viewMatrix;
        updateUniformBuffer(mvpMatrix, glm::vec3(0.0f)); // No position offset for markers

        // Bind descriptor sets
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        // Bind line pipeline for wireframe boxes
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);

        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {m_vertexMarkerBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Create/update dedicated index buffer for vertex markers
        if (!indices.empty()) {
            // Check if we need to recreate the index buffer
            if (m_vertexMarkerIndexBuffer == VK_NULL_HANDLE || indices.size() > m_vertexMarkerIndexCapacity) {
                // Clean up old buffer if it exists
                if (m_vertexMarkerIndexBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(m_device, m_vertexMarkerIndexBuffer, nullptr);
                    vkFreeMemory(m_device, m_vertexMarkerIndexBufferMemory, nullptr);
                }

                // Create new index buffer with extra capacity
                m_vertexMarkerIndexCapacity = indices.size() * 2;
                VkDeviceSize bufferSize = sizeof(uint32_t) * m_vertexMarkerIndexCapacity;
                
                if (!createBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                m_vertexMarkerIndexBuffer, m_vertexMarkerIndexBufferMemory)) {
                    Logger::get().error("Failed to create vertex marker index buffer");
                    return;
                }
            }

            // Update index buffer with marker indices
            void* indexData;
            VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();
            vkMapMemory(m_device, m_vertexMarkerIndexBufferMemory, 0, indexSize, 0, &indexData);
            memcpy(indexData, indices.data(), indexSize);
            vkUnmapMemory(m_device, m_vertexMarkerIndexBufferMemory);

            m_vertexMarkerIndexCount = indices.size();

            // Draw with dedicated index buffer
            vkCmdBindIndexBuffer(commandBuffer, m_vertexMarkerIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, m_vertexMarkerIndexCount, 1, 0, 0, 0);
        } else {
            // Fallback to drawing without indices
            vkCmdDraw(commandBuffer, m_vertexMarkerCount, 1, 0, 0);
        }
    }

    void GizmoRenderer::renderSelectedVertexMarkers(VkCommandBuffer commandBuffer, 
                                                   const std::vector<glm::vec3>& positions,
                                                   const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                                   const glm::vec3& color, float size) {
        if (positions.empty() || m_linePipeline == VK_NULL_HANDLE) {
            return;
        }
        // Generate vertex marker geometry (small boxes for each vertex)
        std::vector<GizmoVertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t vertexOffset = 0;
        // Generate a small box for each vertex position
        for (const auto& pos : positions) {
            generateBox(vertices, indices, pos, size, color, vertexOffset);
        }
        // Check if we need to recreate the selected vertex buffer
        if (m_selectedVertexMarkerBuffer == VK_NULL_HANDLE || vertices.size() > m_selectedVertexMarkerCapacity) {
            // Clean up old buffer if it exists
            if (m_selectedVertexMarkerBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_selectedVertexMarkerBuffer, nullptr);
                vkFreeMemory(m_device, m_selectedVertexMarkerBufferMemory, nullptr);
            }
            // Create new buffer with extra capacity
            m_selectedVertexMarkerCapacity = vertices.size() * 2; // Double capacity for growth
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * m_selectedVertexMarkerCapacity;
            
            if (!createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_selectedVertexMarkerBuffer, m_selectedVertexMarkerBufferMemory)) {
                Logger::get().error("Failed to create selected vertex marker buffer");
                return;
            }
        }
        // Update selected vertex buffer with new data
        void* data;
        vkMapMemory(m_device, m_selectedVertexMarkerBufferMemory, 0, sizeof(GizmoVertex) * vertices.size(), 0, &data);
        memcpy(data, vertices.data(), sizeof(GizmoVertex) * vertices.size());
        vkUnmapMemory(m_device, m_selectedVertexMarkerBufferMemory);
        m_selectedVertexMarkerCount = vertices.size();
        // Update uniform buffer
        glm::mat4 mvpMatrix = projMatrix * viewMatrix;
        updateUniformBuffer(mvpMatrix, glm::vec3(0.0f)); // No position offset for markers
        // Bind descriptor sets
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
        // Bind line pipeline for wireframe boxes
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);
        // Bind selected vertex buffer
        VkBuffer vertexBuffers[] = {m_selectedVertexMarkerBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        // Create/update dedicated index buffer for selected vertex markers
        if (!indices.empty()) {
            // Check if we need to recreate the selected index buffer
            if (m_selectedVertexMarkerIndexBuffer == VK_NULL_HANDLE || indices.size() > m_selectedVertexMarkerIndexCapacity) {
                // Clean up old buffer if it exists
                if (m_selectedVertexMarkerIndexBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(m_device, m_selectedVertexMarkerIndexBuffer, nullptr);
                    vkFreeMemory(m_device, m_selectedVertexMarkerIndexBufferMemory, nullptr);
                }
                // Create new selected index buffer with extra capacity
                m_selectedVertexMarkerIndexCapacity = indices.size() * 2;
                VkDeviceSize bufferSize = sizeof(uint32_t) * m_selectedVertexMarkerIndexCapacity;
                
                if (!createBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                m_selectedVertexMarkerIndexBuffer, m_selectedVertexMarkerIndexBufferMemory)) {
                    Logger::get().error("Failed to create selected vertex marker index buffer");
                    return;
                }
            }
            // Update selected index buffer with marker indices
            void* indexData;
            VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();
            vkMapMemory(m_device, m_selectedVertexMarkerIndexBufferMemory, 0, indexSize, 0, &indexData);
            memcpy(indexData, indices.data(), indexSize);
            vkUnmapMemory(m_device, m_selectedVertexMarkerIndexBufferMemory);
            m_selectedVertexMarkerIndexCount = indices.size();
            // Draw with dedicated selected index buffer
            vkCmdBindIndexBuffer(commandBuffer, m_selectedVertexMarkerIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, m_selectedVertexMarkerIndexCount, 1, 0, 0, 0);
        } else {
            // Fallback to drawing without indices
            vkCmdDraw(commandBuffer, m_selectedVertexMarkerCount, 1, 0, 0);
        }
    }

    void GizmoRenderer::renderTriangleEdges(VkCommandBuffer commandBuffer,
                                           const std::vector<std::pair<glm::vec3, glm::vec3>>& edges,
                                           const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                           const glm::vec3& color) {
        if (edges.empty() || m_linePipeline == VK_NULL_HANDLE) {
            return;
        }

        // Generate line geometry for triangle edges
        std::vector<GizmoVertex> vertices;
        vertices.reserve(edges.size() * 2);

        for (const auto& edge : edges) {
            vertices.push_back({edge.first, color});
            vertices.push_back({edge.second, color});
        }

        // Use dedicated triangle edge buffer for edge rendering
        if (m_triangleEdgeBuffer == VK_NULL_HANDLE || vertices.size() > m_triangleEdgeCapacity) {
            // Clean up old buffer if it exists
            if (m_triangleEdgeBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_triangleEdgeBuffer, nullptr);
                vkFreeMemory(m_device, m_triangleEdgeBufferMemory, nullptr);
            }

            // Create new buffer with extra capacity
            m_triangleEdgeCapacity = vertices.size() * 2;
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * m_triangleEdgeCapacity;
            
            if (!createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_triangleEdgeBuffer, m_triangleEdgeBufferMemory)) {
                Logger::get().error("Failed to create triangle edge buffer");
                return;
            }
        }

        // Update triangle edge vertex buffer
        void* data;
        vkMapMemory(m_device, m_triangleEdgeBufferMemory, 0, sizeof(GizmoVertex) * vertices.size(), 0, &data);
        memcpy(data, vertices.data(), sizeof(GizmoVertex) * vertices.size());
        vkUnmapMemory(m_device, m_triangleEdgeBufferMemory);
        m_triangleEdgeCount = vertices.size();

        // Update uniform buffer
        glm::mat4 mvpMatrix = projMatrix * viewMatrix;
        updateUniformBuffer(mvpMatrix, glm::vec3(0.0f));

        // Bind descriptor sets
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        // Bind line pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);

        // Bind triangle edge vertex buffer
        VkBuffer vertexBuffers[] = {m_triangleEdgeBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Draw lines (2 vertices per line)
        vkCmdDraw(commandBuffer, m_triangleEdgeCount, 1, 0, 0);
    }

    int GizmoRenderer::hitTest(EditorMode mode, const glm::vec2& screenPos, const glm::vec3& gizmoPos,
                              const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                              const glm::vec2& viewport) {
        // Calculate screen-space size for gizmo
        float screenSize = calculateScreenSpaceSize(gizmoPos, viewMatrix, projMatrix, viewport);
        
        // Project gizmo position to screen space
        glm::vec4 clipPos = projMatrix * viewMatrix * glm::vec4(gizmoPos, 1.0f);
        if (clipPos.w <= 0.0f) return -1; // Behind camera
        
        glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;
        glm::vec2 screenCenter = glm::vec2(
            (ndcPos.x + 1.0f) * 0.5f * viewport.x,
            (1.0f - ndcPos.y) * 0.5f * viewport.y
        );

        float hitTolerance = 10.0f; // Pixels

        switch (mode) {
            case EditorMode::Move:
                return hitTestTranslationGizmo(screenPos, screenCenter, screenSize, hitTolerance);
            case EditorMode::Rotate:
                return hitTestRotationGizmo(screenPos, screenCenter, screenSize, hitTolerance);
            case EditorMode::Scale:
                return hitTestScaleGizmo(screenPos, screenCenter, screenSize, hitTolerance);
            default:
                return -1;
        }
    }

    bool GizmoRenderer::createShaders() {
        // Load gizmo shaders
        auto loadShader = [this](const std::string& filename) -> VkShaderModule {
            std::ifstream file(filename, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                Logger::get().error("Failed to open shader file: {}", filename);
                return VK_NULL_HANDLE;
            }
            
            size_t fileSize = (size_t)file.tellg();
            std::vector<char> code(fileSize);
            file.seekg(0);
            file.read(code.data(), fileSize);
            file.close();
            
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
            
            VkShaderModule shaderModule;
            if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                Logger::get().error("Failed to create shader module");
                return VK_NULL_HANDLE;
            }
            
            return shaderModule;
        };
        
        m_vertexShader = loadShader("shaders/gizmo.vert.spv");
        m_fragmentShader = loadShader("shaders/gizmo.frag.spv");
        
        if (m_vertexShader == VK_NULL_HANDLE || m_fragmentShader == VK_NULL_HANDLE) {
            Logger::get().error("Failed to load gizmo shaders");
            return false;
        }
        
        Logger::get().info("Gizmo shaders loaded successfully");
        return true;
    }

    bool GizmoRenderer::createPipelines(VkRenderPass renderPass, VkFormat colorFormat) {
        // Vertex input description (same as grid renderer)
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(GizmoVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(GizmoVertex, position);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(GizmoVertex, color);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        // Create pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;

        if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            Logger::get().error("Failed to create gizmo pipeline layout");
            return false;
        }

        // Shader stage creation
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = m_vertexShader;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = m_fragmentShader;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        // Input assembly for lines
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport state
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Rasterizer
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        rasterizer.lineWidth = 2.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        // Multisampling
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = m_sampleCount;

        // Color blending
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // Dynamic states
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // Depth stencil
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Create line pipeline
        VkGraphicsPipelineCreateInfo linePipelineInfo{};
        linePipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        linePipelineInfo.stageCount = 2;
        linePipelineInfo.pStages = shaderStages;
        linePipelineInfo.pVertexInputState = &vertexInputInfo;
        linePipelineInfo.pInputAssemblyState = &inputAssembly;
        linePipelineInfo.pViewportState = &viewportState;
        linePipelineInfo.pRasterizationState = &rasterizer;
        linePipelineInfo.pMultisampleState = &multisampling;
        linePipelineInfo.pDepthStencilState = &depthStencil;
        linePipelineInfo.pColorBlendState = &colorBlending;
        linePipelineInfo.pDynamicState = &dynamicState;
        linePipelineInfo.layout = m_pipelineLayout;
        linePipelineInfo.renderPass = renderPass;
        linePipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &linePipelineInfo, nullptr, &m_linePipeline) != VK_SUCCESS) {
            Logger::get().error("Failed to create gizmo line pipeline");
            return false;
        }

        // Create triangle pipeline (same as line but with triangle topology)
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;

        linePipelineInfo.pInputAssemblyState = &inputAssembly;
        linePipelineInfo.pRasterizationState = &rasterizer;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &linePipelineInfo, nullptr, &m_trianglePipeline) != VK_SUCCESS) {
            Logger::get().error("Failed to create gizmo triangle pipeline");
            return false;
        }

        Logger::get().info("Gizmo pipelines created successfully");
        return true;
    }

    bool GizmoRenderer::createDescriptorSets() {
        // Same as grid renderer
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboLayoutBinding;

        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
            Logger::get().error("Failed to create gizmo descriptor set layout");
            return false;
        }

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
            Logger::get().error("Failed to create gizmo descriptor pool");
            return false;
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;

        if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate gizmo descriptor set");
            return false;
        }

        return true;
    }

    bool GizmoRenderer::createVertexBuffers() {
        // Generate gizmo geometry
        std::vector<GizmoVertex> translationVertices;
        std::vector<uint32_t> translationIndices;
        generateTranslationGizmo(translationVertices, translationIndices);

        std::vector<GizmoVertex> rotationVertices;
        std::vector<uint32_t> rotationIndices;
        generateRotationGizmo(rotationVertices, rotationIndices);

        std::vector<GizmoVertex> scaleVertices;
        std::vector<uint32_t> scaleIndices;
        generateScaleGizmo(scaleVertices, scaleIndices);

        // Create translation vertex buffer
        if (!translationVertices.empty()) {
            m_translationVertexCount = static_cast<uint32_t>(translationVertices.size());
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * translationVertices.size();

            if (createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           m_translationVertexBuffer, m_translationVertexBufferMemory)) {
                
                void* data;
                vkMapMemory(m_device, m_translationVertexBufferMemory, 0, bufferSize, 0, &data);
                memcpy(data, translationVertices.data(), bufferSize);
                vkUnmapMemory(m_device, m_translationVertexBufferMemory);
            }
        }

        // Create rotation vertex buffer
        if (!rotationVertices.empty()) {
            m_rotationVertexCount = static_cast<uint32_t>(rotationVertices.size());
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * rotationVertices.size();

            if (createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           m_rotationVertexBuffer, m_rotationVertexBufferMemory)) {
                
                void* data;
                vkMapMemory(m_device, m_rotationVertexBufferMemory, 0, bufferSize, 0, &data);
                memcpy(data, rotationVertices.data(), bufferSize);
                vkUnmapMemory(m_device, m_rotationVertexBufferMemory);
            }
        }

        // Create scale vertex buffer
        if (!scaleVertices.empty()) {
            m_scaleVertexCount = static_cast<uint32_t>(scaleVertices.size());
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * scaleVertices.size();

            if (createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           m_scaleVertexBuffer, m_scaleVertexBufferMemory)) {
                
                void* data;
                vkMapMemory(m_device, m_scaleVertexBufferMemory, 0, bufferSize, 0, &data);
                memcpy(data, scaleVertices.data(), bufferSize);
                vkUnmapMemory(m_device, m_scaleVertexBufferMemory);
            }
        }

        Logger::get().info("Created gizmo vertex buffers: translation={}, rotation={}, scale={}",
                         m_translationVertexCount, m_rotationVertexCount, m_scaleVertexCount);

        return true;
    }

    bool GizmoRenderer::createUniformBuffer() {
        VkDeviceSize bufferSize = sizeof(glm::mat4) + sizeof(glm::vec3); // MVP matrix + gizmo position

        if (!createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         m_uniformBuffer, m_uniformBufferMemory)) {
            Logger::get().error("Failed to create gizmo uniform buffer");
            return false;
        }

        // Update descriptor set
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = bufferSize;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);

        return true;
    }

    void GizmoRenderer::generateTranslationGizmo(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        uint32_t vertexOffset = 0;

        // Generate three arrows for X, Y, Z axes
        generateArrow(vertices, indices, glm::vec3(0.0f), glm::vec3(m_gizmoSize, 0.0f, 0.0f), m_xAxisColor, vertexOffset);
        generateArrow(vertices, indices, glm::vec3(0.0f), glm::vec3(0.0f, m_gizmoSize, 0.0f), m_yAxisColor, vertexOffset);
        generateArrow(vertices, indices, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, m_gizmoSize), m_zAxisColor, vertexOffset);

        Logger::get().debug("Generated translation gizmo: {} vertices, {} indices", vertices.size(), indices.size());
    }

    void GizmoRenderer::generateRotationGizmo(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        uint32_t vertexOffset = 0;
        float radius = m_gizmoSize;
        uint32_t segments = 32;

        // Generate three circles for X, Y, Z rotation
        generateCircle(vertices, indices, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), radius, m_xAxisColor, segments, vertexOffset);
        generateCircle(vertices, indices, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), radius, m_yAxisColor, segments, vertexOffset);
        generateCircle(vertices, indices, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), radius, m_zAxisColor, segments, vertexOffset);

        Logger::get().debug("Generated rotation gizmo: {} vertices, {} indices", vertices.size(), indices.size());
    }

    void GizmoRenderer::generateScaleGizmo(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices) {
        vertices.clear();
        indices.clear();

        uint32_t vertexOffset = 0;
        float handleSize = m_gizmoSize * 0.1f;

        // Generate lines with boxes at the ends for X, Y, Z axes
        // X axis
        vertices.push_back({{0.0f, 0.0f, 0.0f}, m_xAxisColor});
        vertices.push_back({{m_gizmoSize, 0.0f, 0.0f}, m_xAxisColor});
        generateBox(vertices, indices, glm::vec3(m_gizmoSize, 0.0f, 0.0f), handleSize, m_xAxisColor, vertexOffset);

        // Y axis
        vertices.push_back({{0.0f, 0.0f, 0.0f}, m_yAxisColor});
        vertices.push_back({{0.0f, m_gizmoSize, 0.0f}, m_yAxisColor});
        generateBox(vertices, indices, glm::vec3(0.0f, m_gizmoSize, 0.0f), handleSize, m_yAxisColor, vertexOffset);

        // Z axis
        vertices.push_back({{0.0f, 0.0f, 0.0f}, m_zAxisColor});
        vertices.push_back({{0.0f, 0.0f, m_gizmoSize}, m_zAxisColor});
        generateBox(vertices, indices, glm::vec3(0.0f, 0.0f, m_gizmoSize), handleSize, m_zAxisColor, vertexOffset);

        // Center box for uniform scaling
        generateBox(vertices, indices, glm::vec3(0.0f), handleSize * 1.5f, glm::vec3(0.8f, 0.8f, 0.8f), vertexOffset);

        Logger::get().debug("Generated scale gizmo: {} vertices, {} indices", vertices.size(), indices.size());
    }

    void GizmoRenderer::generateArrow(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices,
                                     const glm::vec3& start, const glm::vec3& end, const glm::vec3& color,
                                     uint32_t& vertexOffset) {
        // Simple arrow as a line with a triangle at the end
        glm::vec3 direction = glm::normalize(end - start);
        float arrowHeadSize = glm::length(end - start) * 0.2f;
        glm::vec3 arrowStart = end - direction * arrowHeadSize;

        // Arrow shaft (line)
        vertices.push_back({start, color});
        vertices.push_back({arrowStart, color});

        // Arrow head (simplified as additional lines)
        glm::vec3 perpendicular = glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::length(perpendicular) < 0.1f) {
            perpendicular = glm::cross(direction, glm::vec3(1.0f, 0.0f, 0.0f));
        }
        perpendicular = glm::normalize(perpendicular) * arrowHeadSize * 0.5f;

        vertices.push_back({end, color});
        vertices.push_back({arrowStart + perpendicular, color});
        vertices.push_back({end, color});
        vertices.push_back({arrowStart - perpendicular, color});

        vertexOffset = static_cast<uint32_t>(vertices.size());
    }

    void GizmoRenderer::generateCircle(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices,
                                      const glm::vec3& center, const glm::vec3& normal, float radius,
                                      const glm::vec3& color, uint32_t segments, uint32_t& vertexOffset) {
        // Create a circle in the plane perpendicular to the normal
        glm::vec3 tangent = glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::length(tangent) < 0.1f) {
            tangent = glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f));
        }
        tangent = glm::normalize(tangent);
        glm::vec3 bitangent = glm::cross(normal, tangent);

        for (uint32_t i = 0; i < segments; ++i) {
            float angle = 2.0f * glm::pi<float>() * i / segments;
            glm::vec3 position = center + radius * (cos(angle) * tangent + sin(angle) * bitangent);
            vertices.push_back({position, color});

            // Add line indices to form the circle
            if (i > 0) {
                indices.push_back(vertexOffset + i - 1);
                indices.push_back(vertexOffset + i);
            }
        }

        // Close the circle
        if (segments > 0) {
            indices.push_back(vertexOffset + segments - 1);
            indices.push_back(vertexOffset);
        }

        vertexOffset += segments;
    }

    void GizmoRenderer::generateBox(std::vector<GizmoVertex>& vertices, std::vector<uint32_t>& indices,
                                   const glm::vec3& position, float size, const glm::vec3& color,
                                   uint32_t& vertexOffset) {
        // Generate a simple wireframe box
        float halfSize = size * 0.5f;
        
        // 8 corners of the box
        std::vector<glm::vec3> corners = {
            position + glm::vec3(-halfSize, -halfSize, -halfSize),
            position + glm::vec3( halfSize, -halfSize, -halfSize),
            position + glm::vec3( halfSize,  halfSize, -halfSize),
            position + glm::vec3(-halfSize,  halfSize, -halfSize),
            position + glm::vec3(-halfSize, -halfSize,  halfSize),
            position + glm::vec3( halfSize, -halfSize,  halfSize),
            position + glm::vec3( halfSize,  halfSize,  halfSize),
            position + glm::vec3(-halfSize,  halfSize,  halfSize)
        };

        // Add vertices
        for (const auto& corner : corners) {
            vertices.push_back({corner, color});
        }

        // Add edges (12 edges for a cube)
        std::vector<std::pair<uint32_t, uint32_t>> edges = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, // Bottom face
            {4, 5}, {5, 6}, {6, 7}, {7, 4}, // Top face
            {0, 4}, {1, 5}, {2, 6}, {3, 7}  // Vertical edges
        };

        for (const auto& edge : edges) {
            indices.push_back(vertexOffset + edge.first);
            indices.push_back(vertexOffset + edge.second);
        }

        vertexOffset += 8;
    }

    void GizmoRenderer::updateUniformBuffer(const glm::mat4& mvpMatrix, const glm::vec3& gizmoPos) {
        struct UniformData {
            glm::mat4 mvp;
            glm::vec3 position;
            float padding; // Align to 16 bytes
        };

        UniformData data;
        data.mvp = mvpMatrix;
        data.position = gizmoPos;
        data.padding = 0.0f;

        void* mappedData;
        vkMapMemory(m_device, m_uniformBufferMemory, 0, sizeof(data), 0, &mappedData);
        memcpy(mappedData, &data, sizeof(data));
        vkUnmapMemory(m_device, m_uniformBufferMemory);
    }

    float GizmoRenderer::calculateScreenSpaceSize(const glm::vec3& worldPos, const glm::mat4& viewMatrix,
                                                 const glm::mat4& projMatrix, const glm::vec2& viewport) {
        // Calculate distance from camera
        glm::vec4 viewPos = viewMatrix * glm::vec4(worldPos, 1.0f);
        float distance = -viewPos.z; // Distance in view space

        // Calculate how much world space corresponds to a pixel
        float fov = 45.0f; // Default FOV
        float pixelSize = 2.0f * tan(glm::radians(fov) * 0.5f) * distance / viewport.y;
        
        // Return a size that maintains constant screen space size
        return pixelSize * 100.0f; // 100 pixels in world space
    }

    int GizmoRenderer::hitTestTranslationGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                                              float screenSize, float tolerance) {
        // Test hit against three axis lines
        glm::vec2 xEnd = center + glm::vec2(screenSize, 0.0f);
        glm::vec2 yEnd = center + glm::vec2(0.0f, -screenSize); // Y is inverted in screen space
        glm::vec2 zEnd = center + glm::vec2(screenSize * 0.7f, screenSize * 0.7f); // Diagonal for Z

        float xDist = distanceToLine(screenPos, center, xEnd);
        float yDist = distanceToLine(screenPos, center, yEnd);
        float zDist = distanceToLine(screenPos, center, zEnd);

        float minDist = std::min({xDist, yDist, zDist});
        if (minDist > tolerance) return -1;

        if (minDist == xDist) return 0; // X axis
        if (minDist == yDist) return 1; // Y axis
        return 2; // Z axis
    }

    int GizmoRenderer::hitTestRotationGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                                           float screenSize, float tolerance) {
        float distance = glm::length(screenPos - center);
        float radius = screenSize;

        // Check if we're near the circle radius
        if (abs(distance - radius) > tolerance) return -1;

        // For rotation gizmos, we could distinguish between X/Y/Z rings based on position
        // For simplicity, return the closest ring (implementation needed)
        return 1; // Y axis (most common)
    }

    int GizmoRenderer::hitTestScaleGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                                        float screenSize, float tolerance) {
        // Similar to translation gizmo but also check center box for uniform scaling
        float centerDist = glm::length(screenPos - center);
        if (centerDist < tolerance) return 3; // Uniform scale

        return hitTestTranslationGizmo(screenPos, center, screenSize, tolerance);
    }

    float GizmoRenderer::distanceToLine(const glm::vec2& point, const glm::vec2& lineStart, 
                                       const glm::vec2& lineEnd) {
        glm::vec2 line = lineEnd - lineStart;
        float lineLength = glm::length(line);
        if (lineLength < 0.001f) return glm::length(point - lineStart);

        float t = glm::dot(point - lineStart, line) / (lineLength * lineLength);
        t = std::clamp(t, 0.0f, 1.0f);
        glm::vec2 projection = lineStart + t * line;
        return glm::length(point - projection);
    }

    bool GizmoRenderer::pointInCircle(const glm::vec2& point, const glm::vec2& center, float radius) {
        return glm::length(point - center) <= radius;
    }

    bool GizmoRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags properties, VkBuffer& buffer,
                                    VkDeviceMemory& bufferMemory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            vkDestroyBuffer(m_device, buffer, nullptr);
            return false;
        }

        vkBindBufferMemory(m_device, buffer, bufferMemory, 0);
        return true;
    }

    uint32_t GizmoRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && 
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    }

} // namespace tremor::editor