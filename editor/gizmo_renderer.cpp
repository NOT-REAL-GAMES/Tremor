#include "gizmo_renderer.h"
#include "../main.h"
#include <array>
#include <cstring>
#include <fstream>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace tremor::editor {

    // =============================================================================
    // GizmoBuffer Implementation  
    // =============================================================================

    GizmoBuffer::GizmoBuffer(VkDevice device, VkPhysicalDevice physicalDevice)
        : m_device(device), m_physicalDevice(physicalDevice),
          m_buffer(VK_NULL_HANDLE), m_memory(VK_NULL_HANDLE), 
          m_size(0), m_capacity(0), m_count(0) {
    }

    GizmoBuffer::~GizmoBuffer() {
        cleanup();
    }

    GizmoBuffer::GizmoBuffer(GizmoBuffer&& other) noexcept
        : m_device(other.m_device), m_physicalDevice(other.m_physicalDevice),
          m_buffer(other.m_buffer), m_memory(other.m_memory),
          m_size(other.m_size), m_capacity(other.m_capacity), 
          m_count(other.m_count), m_name(std::move(other.m_name)) {
        other.m_buffer = VK_NULL_HANDLE;
        other.m_memory = VK_NULL_HANDLE;
        other.m_size = 0;
        other.m_capacity = 0;
        other.m_count = 0;
    }

    GizmoBuffer& GizmoBuffer::operator=(GizmoBuffer&& other) noexcept {
        if (this != &other) {
            cleanup();
            m_device = other.m_device;
            m_physicalDevice = other.m_physicalDevice;
            m_buffer = other.m_buffer;
            m_memory = other.m_memory;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            m_count = other.m_count;
            m_name = std::move(other.m_name);
            
            other.m_buffer = VK_NULL_HANDLE;
            other.m_memory = VK_NULL_HANDLE;
            other.m_size = 0;
            other.m_capacity = 0;
            other.m_count = 0;
        }
        return *this;
    }

    bool GizmoBuffer::create(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags properties, const std::string& name) {
        cleanup();
        
        m_name = name;
        m_size = size;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS) {
            Logger::get().error("Failed to create buffer: {}", name);
            return false;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, m_buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate buffer memory: {}", name);
            cleanup();
            return false;
        }

        vkBindBufferMemory(m_device, m_buffer, m_memory, 0);
        return true;
    }

    bool GizmoBuffer::updateData(const void* data, VkDeviceSize size, VkDeviceSize offset) {
        if (!isValid() || offset + size > m_size) {
            return false;
        }

        void* mappedData;
        vkMapMemory(m_device, m_memory, offset, size, 0, &mappedData);
        memcpy(mappedData, data, size);
        vkUnmapMemory(m_device, m_memory);
        return true;
    }

    void GizmoBuffer::cleanup() {
        if (m_device != VK_NULL_HANDLE) {
            if (m_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(m_device, m_buffer, nullptr);
                m_buffer = VK_NULL_HANDLE;
            }
            if (m_memory != VK_NULL_HANDLE) {
                vkFreeMemory(m_device, m_memory, nullptr);
                m_memory = VK_NULL_HANDLE;
            }
        }
        m_size = 0;
        m_capacity = 0;
        m_count = 0;
    }

    uint32_t GizmoBuffer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        Logger::get().error("Failed to find suitable memory type for buffer: {}", m_name);
        return 0;
    }

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
          m_translationVertexBuffer(device, physicalDevice),
          m_rotationVertexBuffer(device, physicalDevice),
          m_scaleVertexBuffer(device, physicalDevice),
          m_vertexMarkerBuffer(device, physicalDevice),
          m_vertexMarkerIndexBuffer(device, physicalDevice),
          m_triangleEdgeBuffer(device, physicalDevice),
          m_selectedVertexMarkerBuffer(device, physicalDevice),
          m_selectedVertexMarkerIndexBuffer(device, physicalDevice),
          m_mouseRayDebugBuffer(device, physicalDevice),
          m_indexBuffer(device, physicalDevice),
          m_uniformBuffer(device, physicalDevice),
          m_gizmoUniformBuffer(device, physicalDevice),
          m_gizmoDescriptorSet(VK_NULL_HANDLE),
          m_vertexShader(VK_NULL_HANDLE), m_fragmentShader(VK_NULL_HANDLE) {
    }

    GizmoRenderer::~GizmoRenderer() {
        if (m_device != VK_NULL_HANDLE) {
            // Clean up non-buffer Vulkan resources (buffers are handled by RAII GizmoBuffer)
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
        updateGizmoUniformBuffer(mvpMatrix, position);

        // Bind gizmo descriptor set
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               m_pipelineLayout, 0, 1, &m_gizmoDescriptorSet, 0, nullptr);

        // Select appropriate vertex buffer and pipeline based on mode
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        uint32_t vertexCount = 0;
        VkPipeline pipeline = m_linePipeline;

        switch (mode) {
            case EditorMode::Move:
                vertexBuffer = m_translationVertexBuffer.getBuffer();
                vertexCount = m_translationVertexBuffer.getCount();
                pipeline = m_linePipeline; // Translation gizmo uses lines
                /*
                
                Logger::get().info("GIZMO DEBUG: Translation mode - vertexBuffer={}, vertexCount={}", 
                                 (void*)vertexBuffer, vertexCount);
                
                */
                
                break;
            case EditorMode::Rotate:
                vertexBuffer = m_rotationVertexBuffer.getBuffer();
                vertexCount = m_rotationVertexBuffer.getCount();
                pipeline = m_linePipeline; // Rotation gizmo uses line circles
                break;
            case EditorMode::Scale:
                vertexBuffer = m_scaleVertexBuffer.getBuffer();
                vertexCount = m_scaleVertexBuffer.getCount();
                pipeline = m_linePipeline; // Scale gizmo uses lines with boxes
                break;
            default:
                return; // No gizmo for select mode
        }

        if (!m_translationVertexBuffer.isValid() && !m_rotationVertexBuffer.isValid() && !m_scaleVertexBuffer.isValid()) {
            return;
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
        if (m_indexBuffer.getCount() > 0) {
            //Logger::get().info("GIZMO DEBUG: Drawing with indices - indexCount={}", m_indexBuffer.getCount());
            vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.getBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, m_indexBuffer.getCount(), 1, 0, 0, 0);
        } else {
            //Logger::get().info("GIZMO DEBUG: Drawing vertices - vertexCount={}", vertexCount);
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
        if (!m_vertexMarkerBuffer.isValid() || vertices.size() > m_vertexMarkerBuffer.getCapacity()) {
            // Create new buffer with extra capacity
            uint32_t newCapacity = vertices.size() * 2; // Double capacity for growth
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * newCapacity;
            
            if (!m_vertexMarkerBuffer.create(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            "vertex_marker_buffer")) {
                Logger::get().error("Failed to create vertex marker buffer");
                return;
            }
            
            m_vertexMarkerBuffer.setCapacity(newCapacity);
        }

        // Update vertex buffer with new data
        if (!m_vertexMarkerBuffer.updateData(vertices.data(), sizeof(GizmoVertex) * vertices.size())) {
            Logger::get().error("Failed to update vertex marker buffer data");
            return;
        }
        
        m_vertexMarkerBuffer.setCount(vertices.size());

        // Update uniform buffer
        glm::mat4 mvpMatrix = projMatrix * viewMatrix;
        updateUniformBuffer(mvpMatrix, glm::vec3(0.0f)); // No position offset for markers

        // Bind descriptor sets
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        // Bind line pipeline for wireframe boxes
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);

        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {m_vertexMarkerBuffer.getBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Create/update dedicated index buffer for vertex markers
        if (!indices.empty()) {
            // Check if we need to recreate the index buffer
            if (!m_vertexMarkerIndexBuffer.isValid() || indices.size() > m_vertexMarkerIndexBuffer.getCapacity()) {
                // Create new index buffer with extra capacity
                uint32_t newCapacity = indices.size() * 2;
                VkDeviceSize bufferSize = sizeof(uint32_t) * newCapacity;
                
                if (!m_vertexMarkerIndexBuffer.create(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                "vertex_marker_index_buffer")) {
                    Logger::get().error("Failed to create vertex marker index buffer");
                    return;
                }
                
                m_vertexMarkerIndexBuffer.setCapacity(newCapacity);
            }

            // Update index buffer with marker indices
            if (!m_vertexMarkerIndexBuffer.updateData(indices.data(), sizeof(uint32_t) * indices.size())) {
                Logger::get().error("Failed to update vertex marker index buffer data");
                return;
            }

            m_vertexMarkerIndexBuffer.setCount(indices.size());

            // Draw with dedicated index buffer
            vkCmdBindIndexBuffer(commandBuffer, m_vertexMarkerIndexBuffer.getBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, m_vertexMarkerIndexBuffer.getCount(), 1, 0, 0, 0);
        } else {
            // Fallback to drawing without indices
            vkCmdDraw(commandBuffer, m_vertexMarkerBuffer.getCount(), 1, 0, 0);
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
        if (!m_selectedVertexMarkerBuffer.isValid() || vertices.size() > m_selectedVertexMarkerBuffer.getCapacity()) {
            // Create new buffer with extra capacity
            uint32_t newCapacity = vertices.size() * 2; // Double capacity for growth
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * newCapacity;
            
            if (!m_selectedVertexMarkerBuffer.create(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            "selected_vertex_marker_buffer")) {
                Logger::get().error("Failed to create selected vertex marker buffer");
                return;
            }
            
            m_selectedVertexMarkerBuffer.setCapacity(newCapacity);
        }
        // Update selected vertex buffer with new data
        if (!m_selectedVertexMarkerBuffer.updateData(vertices.data(), sizeof(GizmoVertex) * vertices.size())) {
            Logger::get().error("Failed to update selected vertex marker buffer data");
            return;
        }
        
        m_selectedVertexMarkerBuffer.setCount(vertices.size());
        // Update uniform buffer
        glm::mat4 mvpMatrix = projMatrix * viewMatrix;
        updateUniformBuffer(mvpMatrix, glm::vec3(0.0f)); // No position offset for markers
        // Bind descriptor sets
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
        // Bind line pipeline for wireframe boxes
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);
        // Bind selected vertex buffer
        VkBuffer vertexBuffers[] = {m_selectedVertexMarkerBuffer.getBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        // Create/update dedicated index buffer for selected vertex markers
        if (!indices.empty()) {
            // Check if we need to recreate the selected index buffer
            if (!m_selectedVertexMarkerIndexBuffer.isValid() || indices.size() > m_selectedVertexMarkerIndexBuffer.getCapacity()) {
                // Create new selected index buffer with extra capacity
                uint32_t newCapacity = indices.size() * 2;
                VkDeviceSize bufferSize = sizeof(uint32_t) * newCapacity;
                
                if (!m_selectedVertexMarkerIndexBuffer.create(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                "selected_vertex_marker_index_buffer")) {
                    Logger::get().error("Failed to create selected vertex marker index buffer");
                    return;
                }
                
                m_selectedVertexMarkerIndexBuffer.setCapacity(newCapacity);
            }
            // Update selected index buffer with marker indices
            if (!m_selectedVertexMarkerIndexBuffer.updateData(indices.data(), sizeof(uint32_t) * indices.size())) {
                Logger::get().error("Failed to update selected vertex marker index buffer data");
                return;
            }
            
            m_selectedVertexMarkerIndexBuffer.setCount(indices.size());
            // Draw with dedicated selected index buffer
            vkCmdBindIndexBuffer(commandBuffer, m_selectedVertexMarkerIndexBuffer.getBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, m_selectedVertexMarkerIndexBuffer.getCount(), 1, 0, 0, 0);
        } else {
            // Fallback to drawing without indices
            vkCmdDraw(commandBuffer, m_selectedVertexMarkerBuffer.getCount(), 1, 0, 0);
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
        if (!m_triangleEdgeBuffer.isValid() || vertices.size() > m_triangleEdgeBuffer.getCapacity()) {
            // Create new buffer with extra capacity
            uint32_t newCapacity = vertices.size() * 2;
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * newCapacity;
            
            if (!m_triangleEdgeBuffer.create(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            "triangle_edge_buffer")) {
                Logger::get().error("Failed to create triangle edge buffer");
                return;
            }
            
            m_triangleEdgeBuffer.setCapacity(newCapacity);
        }

        // Update triangle edge vertex buffer
        if (!m_triangleEdgeBuffer.updateData(vertices.data(), sizeof(GizmoVertex) * vertices.size())) {
            Logger::get().error("Failed to update triangle edge buffer data");
            return;
        }
        
        m_triangleEdgeBuffer.setCount(vertices.size());

        // Update uniform buffer
        glm::mat4 mvpMatrix = projMatrix * viewMatrix;
        updateUniformBuffer(mvpMatrix, glm::vec3(0.0f));

        // Bind descriptor sets
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        // Bind line pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);

        // Bind triangle edge vertex buffer
        VkBuffer vertexBuffers[] = {m_triangleEdgeBuffer.getBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Draw lines (2 vertices per line)
        vkCmdDraw(commandBuffer, m_triangleEdgeBuffer.getCount(), 1, 0, 0);
    }

    int GizmoRenderer::hitTest(EditorMode mode, const glm::vec2& screenPos, const glm::vec3& gizmoPos,
                              const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                              const glm::vec2& viewport) {
        // Calculate screen-space size for gizmo
        float screenSize = calculateScreenSpaceSize(gizmoPos, viewMatrix, projMatrix, viewport);
        
        // Project gizmo position to screen space
        glm::vec4 clipPos = projMatrix * viewMatrix * glm::vec4(gizmoPos, 1.0f);
        if (clipPos.w <= 0.0f) {
            return -1; // Behind camera
        }
        
        glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;
        glm::vec2 screenCenter = glm::vec2(
            (ndcPos.x + 1.0f) * 0.5f * viewport.x,
            (1.0f - ndcPos.y) * 0.5f * viewport.y
        );

        float hitTolerance = 60.0f; // Pixels - generous tolerance for easy gizmo interaction

        int result = -1;
        switch (mode) {
            case EditorMode::Move:
                result = hitTestTranslationGizmo(screenPos, screenCenter, screenSize, hitTolerance, 
                                                 gizmoPos, viewMatrix, projMatrix, viewport);
                break;
            case EditorMode::Rotate:
                result = hitTestRotationGizmo(screenPos, screenCenter, screenSize, hitTolerance,
                                            gizmoPos, viewMatrix, projMatrix, viewport);
                break;
            case EditorMode::Scale:
                result = hitTestScaleGizmo(screenPos, screenCenter, screenSize, hitTolerance, 
                                         gizmoPos, viewMatrix, projMatrix, viewport);
                break;
            default:
                result = -1;
                break;
        }
        
        if (result >= 0) {
            Logger::get().info("Gizmo hit successful! Mode: {}, Axis: {}", (int)mode, result);
        }
        return result;
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
        poolSize.descriptorCount = 2; // Two uniform buffers

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 2; // Two descriptor sets

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
        
        // Allocate second descriptor set for gizmo transforms
        if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_gizmoDescriptorSet) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate gizmo transform descriptor set");
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
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * translationVertices.size();

            if (m_translationVertexBuffer.create(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           "translation_vertex_buffer")) {
                
                m_translationVertexBuffer.setCount(static_cast<uint32_t>(translationVertices.size()));
                m_translationVertexBuffer.setCapacity(static_cast<uint32_t>(translationVertices.size()));
                m_translationVertexBuffer.updateData(translationVertices.data(), bufferSize);
            }
        }

        // Create rotation vertex buffer
        if (!rotationVertices.empty()) {
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * rotationVertices.size();

            if (m_rotationVertexBuffer.create(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           "rotation_vertex_buffer")) {
                
                m_rotationVertexBuffer.setCount(static_cast<uint32_t>(rotationVertices.size()));
                m_rotationVertexBuffer.setCapacity(static_cast<uint32_t>(rotationVertices.size()));
                m_rotationVertexBuffer.updateData(rotationVertices.data(), bufferSize);
            }
        }

        // Create scale vertex buffer
        if (!scaleVertices.empty()) {
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * scaleVertices.size();

            if (m_scaleVertexBuffer.create(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           "scale_vertex_buffer")) {
                
                m_scaleVertexBuffer.setCount(static_cast<uint32_t>(scaleVertices.size()));
                m_scaleVertexBuffer.setCapacity(static_cast<uint32_t>(scaleVertices.size()));
                m_scaleVertexBuffer.updateData(scaleVertices.data(), bufferSize);
            }
        }

        Logger::get().info("Created gizmo vertex buffers: translation={}, rotation={}, scale={}",
                         m_translationVertexBuffer.getCount(), m_rotationVertexBuffer.getCount(), m_scaleVertexBuffer.getCount());

        return true;
    }

    bool GizmoRenderer::createUniformBuffer() {
        // Define the uniform data structure to get proper alignment
        struct UniformData {
            glm::mat4 mvp;
            glm::vec3 position;
            float padding; // Align to 16 bytes
        };
        
        VkDeviceSize bufferSize = sizeof(UniformData); // Properly aligned size

        if (!m_uniformBuffer.create(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         "gizmo_uniform_buffer")) {
            Logger::get().error("Failed to create gizmo uniform buffer");
            return false;
        }

        // Update descriptor set
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffer.getBuffer();
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
        
        // Create second uniform buffer for gizmo transforms
        if (!m_gizmoUniformBuffer.create(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         "gizmo_transform_uniform_buffer")) {
            Logger::get().error("Failed to create gizmo transform uniform buffer");
            return false;
        }
        
        // Update gizmo descriptor set
        VkDescriptorBufferInfo gizmoBufferInfo{};
        gizmoBufferInfo.buffer = m_gizmoUniformBuffer.getBuffer();
        gizmoBufferInfo.offset = 0;
        gizmoBufferInfo.range = bufferSize;
        
        VkWriteDescriptorSet gizmoDescriptorWrite{};
        gizmoDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        gizmoDescriptorWrite.dstSet = m_gizmoDescriptorSet;
        gizmoDescriptorWrite.dstBinding = 0;
        gizmoDescriptorWrite.dstArrayElement = 0;
        gizmoDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        gizmoDescriptorWrite.descriptorCount = 1;
        gizmoDescriptorWrite.pBufferInfo = &gizmoBufferInfo;
        
        vkUpdateDescriptorSets(m_device, 1, &gizmoDescriptorWrite, 0, nullptr);

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
        // Check if buffer is valid before updating
        if (!m_uniformBuffer.isValid()) {
            return; // Buffer not yet created, skip update
        }
        
        struct UniformData {
            glm::mat4 mvp;
            glm::vec3 position;
            float padding; // Align to 16 bytes
        };

        UniformData data;
        data.mvp = mvpMatrix;
        data.position = gizmoPos;
        data.padding = 0.0f;
        
        /*
        
        Logger::get().info("UNIFORM BUFFER DEBUG: gizmoPos=({:.3f}, {:.3f}, {:.3f})", 
                          gizmoPos.x, gizmoPos.y, gizmoPos.z);
        Logger::get().info("UNIFORM BUFFER DEBUG: data.position=({:.3f}, {:.3f}, {:.3f})", 
                          data.position.x, data.position.y, data.position.z);
        
        */

        m_uniformBuffer.updateData(&data, sizeof(data));
    }
    
    void GizmoRenderer::updateGizmoUniformBuffer(const glm::mat4& mvpMatrix, const glm::vec3& gizmoPos) {
        // Check if buffer is valid before updating
        if (!m_gizmoUniformBuffer.isValid()) {
            return; // Buffer not yet created, skip update
        }
        
        struct UniformData {
            glm::mat4 mvp;
            glm::vec3 position;
            float padding; // Align to 16 bytes
        };

        UniformData data;
        data.mvp = mvpMatrix;
        data.position = gizmoPos;
        data.padding = 0.0f;

        m_gizmoUniformBuffer.updateData(&data, sizeof(data));
    }

    float GizmoRenderer::calculateScreenSpaceSize(const glm::vec3& worldPos, const glm::mat4& viewMatrix,
                                                 const glm::mat4& projMatrix, const glm::vec2& viewport) {
        // Return a fixed screen space size in pixels for gizmo elements
        // This should be the length of gizmo arrows/handles in screen space
        return 80.0f; // 80 pixels for gizmo elements
    }

    int GizmoRenderer::hitTestTranslationGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                                              float screenSize, float tolerance, const glm::vec3& gizmoPos,
                                              const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                              const glm::vec2& viewport) {
        // Generate mouse ray from screen position
        Ray mouseRay = screenToWorldRay(screenPos, viewMatrix, projMatrix, viewport);
        
        // Define 3D arrow line segments (flip X and Z coordinates for ray casting)
        float arrowLength = m_gizmoSize;
        glm::vec3 xEnd = gizmoPos + glm::vec3(arrowLength, 0.0f, 0.0f);
        glm::vec3 yEnd = gizmoPos + glm::vec3(0.0f, arrowLength, 0.0f);
        glm::vec3 zEnd = gizmoPos + glm::vec3(0.0f, 0.0f, arrowLength);
        
        // Convert pixel tolerance to world space tolerance
        // Use screen-space tolerance scaled by distance to gizmo (reduced for more precise selection)
        float distanceToGizmo = glm::length(gizmoPos - glm::vec3(glm::inverse(viewMatrix)[3]));
        float worldTolerance = (tolerance / 400.0f) * distanceToGizmo;
        // Ensure minimum tolerance to prevent issues at certain viewing angles
        worldTolerance = std::max(worldTolerance, 0.1f);
        
        // Test ray intersection with each axis line segment
        float rayT, lineT;
        float xRayT, xLineT, yRayT, yLineT, zRayT, zLineT;
        float xDist = distanceFromRayToLineSegment(mouseRay, gizmoPos, xEnd, xRayT, xLineT);
        float yDist = distanceFromRayToLineSegment(mouseRay, gizmoPos, yEnd, yRayT, yLineT);
        float zDist = distanceFromRayToLineSegment(mouseRay, gizmoPos, zEnd, zRayT, zLineT);

        // Find the closest axis
        float minDist = std::min({xDist, yDist, zDist});
        
        // Debug: Log the closest point on the ray for visualization
        if (minDist <= worldTolerance) {
            float closestRayT = 0.0f;
            glm::vec3 closestLineStart, closestLineEnd;
            if (minDist == xDist) {
                closestRayT = xRayT;
                closestLineStart = gizmoPos;
                closestLineEnd = xEnd;
            } else if (minDist == yDist) {
                closestRayT = yRayT;
                closestLineStart = gizmoPos;
                closestLineEnd = yEnd;
            } else {
                closestRayT = zRayT;
                closestLineStart = gizmoPos;
                closestLineEnd = zEnd;
            }
            glm::vec3 rayPoint = mouseRay.origin + closestRayT * mouseRay.direction;
            Logger::get().info("Mouse ray hit point: ({:.2f}, {:.2f}, {:.2f}) at distance {:.3f}", 
                             rayPoint.x, rayPoint.y, rayPoint.z, minDist);
        }
        if (minDist > worldTolerance) {
            return -1;
        }

        // Return the correct axis (coordinates are already flipped in ray casting)
        if (minDist == xDist) return 0; // X axis
        if (minDist == yDist) return 1; // Y axis
        if (minDist == zDist) return 2; // Z axis

        return -1;
    }

    int GizmoRenderer::hitTestRotationGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                                           float screenSize, float tolerance, const glm::vec3& gizmoPos,
                                           const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                           const glm::vec2& viewport) {
        // Generate mouse ray from screen position
        Ray mouseRay = screenToWorldRay(screenPos, viewMatrix, projMatrix, viewport);
        
        float radius = m_gizmoSize;
        float hitTolerance = m_gizmoSize * 0.1f; // 3D tolerance
        
        // Test against each rotation circle (X, Y, Z)
        float minDistance = std::numeric_limits<float>::max();
        int closestAxis = -1;
        
        // X-axis circle (rotation around X, circle lies in YZ plane)
        float xDistance = distanceFromRayToCircle(mouseRay, gizmoPos, glm::vec3(1.0f, 0.0f, 0.0f), radius);
        if (xDistance < minDistance && xDistance < hitTolerance) {
            minDistance = xDistance;
            closestAxis = 0; // X axis
        }
        
        // Y-axis circle (rotation around Y, circle lies in XZ plane)  
        float yDistance = distanceFromRayToCircle(mouseRay, gizmoPos, glm::vec3(0.0f, 1.0f, 0.0f), radius);
        if (yDistance < minDistance && yDistance < hitTolerance) {
            minDistance = yDistance;
            closestAxis = 1; // Y axis
        }
        
        // Z-axis circle (rotation around Z, circle lies in XY plane)
        float zDistance = distanceFromRayToCircle(mouseRay, gizmoPos, glm::vec3(0.0f, 0.0f, 1.0f), radius);
        if (zDistance < minDistance && zDistance < hitTolerance) {
            minDistance = zDistance;
            closestAxis = 2; // Z axis
        }
        
        return closestAxis;
    }

    int GizmoRenderer::hitTestScaleGizmo(const glm::vec2& screenPos, const glm::vec2& center,
                                        float screenSize, float tolerance, const glm::vec3& gizmoPos,
                                        const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                        const glm::vec2& viewport) {
        // Generate mouse ray from screen position
        Ray mouseRay = screenToWorldRay(screenPos, viewMatrix, projMatrix, viewport);
        
        // Check for uniform scale first (center sphere)
        float centerRadius = m_gizmoSize * 0.3f; // Smaller center sphere
        glm::vec3 centerToRayOrigin = mouseRay.origin - gizmoPos;
        float rayDotDir = glm::dot(centerToRayOrigin, mouseRay.direction);
        glm::vec3 closestPointOnRay = mouseRay.origin + (-rayDotDir) * mouseRay.direction;
        float centerDist = glm::length(closestPointOnRay - gizmoPos);
        
        if (centerDist < centerRadius) {
            return 3; // Uniform scale
        }
        
        // Define 3D scale handle line segments (same as translation but shorter)
        float handleLength = m_gizmoSize * 0.8f; // Shorter than translation handles
        glm::vec3 xEnd = gizmoPos + glm::vec3(handleLength, 0.0f, 0.0f);
        glm::vec3 yEnd = gizmoPos + glm::vec3(0.0f, handleLength, 0.0f);
        glm::vec3 zEnd = gizmoPos + glm::vec3(0.0f, 0.0f, handleLength);
        
        // Convert pixel tolerance to world space tolerance
        float distanceToGizmo = glm::length(gizmoPos - glm::vec3(glm::inverse(viewMatrix)[3]));
        float worldTolerance = (tolerance / 400.0f) * distanceToGizmo;
        worldTolerance = std::max(worldTolerance, 0.1f);
        
        // Test ray intersection with each axis line segment
        float xRayT, xLineT, yRayT, yLineT, zRayT, zLineT;
        float xDist = distanceFromRayToLineSegment(mouseRay, gizmoPos, xEnd, xRayT, xLineT);
        float yDist = distanceFromRayToLineSegment(mouseRay, gizmoPos, yEnd, yRayT, yLineT);
        float zDist = distanceFromRayToLineSegment(mouseRay, gizmoPos, zEnd, zRayT, zLineT);
        
        // Find the closest axis
        float minDist = std::min({xDist, yDist, zDist});
        if (minDist > worldTolerance) {
            return -1;
        }

        // Return the closest axis
        if (minDist == xDist) return 0; // X axis
        if (minDist == yDist) return 1; // Y axis
        return 2; // Z axis
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
    
    void GizmoRenderer::renderMouseRayDebug(VkCommandBuffer commandBuffer, const glm::vec2& screenPos,
                                           const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                                           const glm::vec2& viewport) {
        // Generate the mouse ray
        Ray mouseRay = screenToWorldRay(screenPos, viewMatrix, projMatrix, viewport);
        
        // Create vertices for ray visualization
        std::vector<GizmoVertex> vertices;
        
        // Line along ray (yellow)
        vertices.push_back({mouseRay.origin, glm::vec3(1.0f, 1.0f, 0.0f)});
        vertices.push_back({mouseRay.origin + mouseRay.direction * 10.0f, glm::vec3(1.0f, 1.0f, 0.0f)});
        
        // Marker sphere at fixed distance (magenta)
        float markerDistance = 5.0f;
        glm::vec3 markerPos = mouseRay.origin + mouseRay.direction * markerDistance;
        float markerSize = 0.2f;
        
        // Add a small cross at the marker position
        vertices.push_back({markerPos - glm::vec3(markerSize, 0, 0), glm::vec3(1.0f, 0.0f, 1.0f)});
        vertices.push_back({markerPos + glm::vec3(markerSize, 0, 0), glm::vec3(1.0f, 0.0f, 1.0f)});
        vertices.push_back({markerPos - glm::vec3(0, markerSize, 0), glm::vec3(1.0f, 0.0f, 1.0f)});
        vertices.push_back({markerPos + glm::vec3(0, markerSize, 0), glm::vec3(1.0f, 0.0f, 1.0f)});
        vertices.push_back({markerPos - glm::vec3(0, 0, markerSize), glm::vec3(1.0f, 0.0f, 1.0f)});
        vertices.push_back({markerPos + glm::vec3(0, 0, markerSize), glm::vec3(1.0f, 0.0f, 1.0f)});
        
        // Ensure buffer exists and is large enough
        if (!m_mouseRayDebugBuffer.isValid() || m_mouseRayDebugBuffer.getCapacity() < vertices.size()) {
            // Create new buffer
            uint32_t newCapacity = std::max(size_t(100), vertices.size() * 2);
            VkDeviceSize bufferSize = sizeof(GizmoVertex) * newCapacity;
            
            if (!m_mouseRayDebugBuffer.create(bufferSize, 
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        "mouse_ray_debug_buffer")) {
                Logger::get().error("Failed to create mouse ray debug buffer");
                return;
            }
            
            m_mouseRayDebugBuffer.setCapacity(newCapacity);
        }
        
        // Upload vertices
        if (!m_mouseRayDebugBuffer.updateData(vertices.data(), sizeof(GizmoVertex) * vertices.size())) {
            Logger::get().error("Failed to update mouse ray debug buffer data");
            return;
        }
        
        m_mouseRayDebugBuffer.setCount(vertices.size());
        
        // Update uniform buffer
        glm::mat4 mvpMatrix = projMatrix * viewMatrix;
        updateUniformBuffer(mvpMatrix, glm::vec3(0.0f));
        
        // Bind pipeline and descriptor set
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                              m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
        
        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {m_mouseRayDebugBuffer.getBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        
        // Draw lines
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    }

    // ================================================================================================
    // Ray Casting Implementation
    // ================================================================================================
    
    GizmoRenderer::Ray GizmoRenderer::screenToWorldRay(const glm::vec2& screenPos, const glm::mat4& viewMatrix, 
                                                       const glm::mat4& projMatrix, const glm::vec2& viewport) {
        // Convert screen coordinates to NDC
        glm::vec2 ndc = glm::vec2(
            (2.0f * screenPos.x) / viewport.x - 1.0f,
            (2.0f * screenPos.y) / viewport.y - 1.0f
        );
        
        // Create points at near and far planes in NDC
        glm::vec4 nearPoint = glm::vec4(ndc.x, ndc.y, -1.0f, 1.0f); // Near plane
        glm::vec4 farPoint = glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);   // Far plane
        
        // Transform to world space
        glm::mat4 invViewProj = glm::inverse(projMatrix * viewMatrix);
        glm::vec4 worldNear = invViewProj * nearPoint;
        glm::vec4 worldFar = invViewProj * farPoint;
        
        // Perspective divide
        worldNear /= worldNear.w;
        worldFar /= worldFar.w;
        
        // Create ray
        Ray ray;
        ray.origin = glm::vec3(worldNear);
        ray.direction = glm::normalize(glm::vec3(worldFar) - glm::vec3(worldNear));
        
        return ray;
    }
    
    float GizmoRenderer::distanceFromRayToLineSegment(const Ray& ray, const glm::vec3& lineStart, 
                                                     const glm::vec3& lineEnd, float& rayT, float& lineT) {
        // Implementation of ray-line segment distance calculation
        // Based on "Real-Time Rendering" by Möller & Haines
        
        glm::vec3 lineDir = lineEnd - lineStart;
        float lineLength = glm::length(lineDir);
        if (lineLength < 0.001f) {
            // Degenerate line, treat as point
            glm::vec3 toPoint = lineStart - ray.origin;
            rayT = glm::dot(toPoint, ray.direction);
            lineT = 0.0f;
            return glm::length(toPoint - rayT * ray.direction);
        }
        
        lineDir = lineDir / lineLength; // normalize
        
        glm::vec3 w0 = ray.origin - lineStart;
        float a = glm::dot(ray.direction, ray.direction);  // always >= 0
        float b = glm::dot(ray.direction, lineDir);
        float c = glm::dot(lineDir, lineDir);              // always >= 0
        float d = glm::dot(ray.direction, w0);
        float e = glm::dot(lineDir, w0);
        
        float denom = a * c - b * b;  // always >= 0
        
        if (denom < 0.001f) {
            // Ray and line are parallel
            rayT = 0.0f;
            lineT = (b > c ? d / b : e / c);  // use the larger denominator
        } else {
            rayT = (b * e - c * d) / denom;
            lineT = (a * e - b * d) / denom;
        }
        
        // Clamp lineT to line segment bounds
        lineT = std::clamp(lineT * lineLength, 0.0f, lineLength) / lineLength;
        
        // Calculate closest points
        glm::vec3 rayPoint = ray.origin + rayT * ray.direction;
        glm::vec3 linePoint = lineStart + lineT * lineDir * lineLength;
        
        return glm::length(rayPoint - linePoint);
    }

    float GizmoRenderer::distanceFromRayToCircle(const Ray& ray, const glm::vec3& circleCenter,
                                                 const glm::vec3& circleNormal, float circleRadius) {
        // Project the ray onto the plane containing the circle
        float denom = glm::dot(circleNormal, ray.direction);
        
        if (abs(denom) < 0.001f) {
            // Ray is parallel to the circle plane
            return std::numeric_limits<float>::max();
        }
        
        // Find intersection of ray with the circle's plane
        float t = glm::dot(circleNormal, circleCenter - ray.origin) / denom;
        if (t < 0.0f) {
            // Intersection is behind the ray origin
            return std::numeric_limits<float>::max();
        }
        
        // Point where ray intersects the plane
        glm::vec3 planePoint = ray.origin + t * ray.direction;
        
        // Distance from plane intersection point to circle center
        float distToCenter = glm::length(planePoint - circleCenter);
        
        // Find closest point on the circle to the plane intersection point
        glm::vec3 toPoint = planePoint - circleCenter;
        if (glm::length(toPoint) < 0.001f) {
            // Point is at circle center, pick any point on circle
            // Find a vector perpendicular to the normal
            glm::vec3 perpendicular;
            if (abs(circleNormal.x) < 0.9f) {
                perpendicular = glm::cross(circleNormal, glm::vec3(1.0f, 0.0f, 0.0f));
            } else {
                perpendicular = glm::cross(circleNormal, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            toPoint = glm::normalize(perpendicular) * circleRadius;
        } else {
            // Normalize and scale to circle radius
            toPoint = glm::normalize(toPoint) * circleRadius;
        }
        
        glm::vec3 closestCirclePoint = circleCenter + toPoint;
        
        // Return distance from ray to closest point on circle
        glm::vec3 rayToCircle = closestCirclePoint - ray.origin;
        float rayProjection = glm::dot(rayToCircle, ray.direction);
        glm::vec3 closestRayPoint = ray.origin + std::max(0.0f, rayProjection) * ray.direction;
        
        return glm::length(closestRayPoint - closestCirclePoint);
    }


} // namespace tremor::editor