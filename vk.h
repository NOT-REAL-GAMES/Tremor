#pragma once
#include "main.h"
#include "RenderBackendBase.h"
#include "res.h"
#include "mem.h"
#include <shaderc/shaderc.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>


// Define concepts for Vulkan types
template<typename T>
concept VulkanStructure = requires(T t) {
    { t.sType } -> std::convertible_to<VkStructureType>;
    { t.pNext } -> std::convertible_to<void*>;
};

// Type-safe structure creation
template<VulkanStructure T>
T createVulkanStructure() {
    T result{};
    result.sType = getVulkanStructureType<T>();
    return result;
}

// Instead of ZEROED_STRUCT macro:
template<typename T>
T createStructure() {
    T result{};
    result.sType = getStructureType<T>();
    return result;
}

// Instead of CHAIN_PNEXT macro:
template<typename T>
void chainStructure(void** ppNext, T& structure) {
    *ppNext = &structure;
    ppNext = &structure.pNext;
}

// Helper function to copy buffer data
void copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue,
    VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    try {
        // Create command buffer for transfer
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
        if (result != VK_SUCCESS) {
            Logger::get().error("Failed to allocate transfer command buffer: {}", (int)result);
            return;
        }

        // Begin recording
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
        if (result != VK_SUCCESS) {
            Logger::get().error("Failed to begin command buffer: {}", (int)result);
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            return;
        }

        // Copy from source to destination
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        // End recording
        result = vkEndCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS) {
            Logger::get().error("Failed to end command buffer: {}", (int)result);
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            return;
        }

        // Submit and wait
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        result = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        if (result != VK_SUCCESS) {
            Logger::get().error("Failed to submit transfer command buffer: {}", (int)result);
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            return;
        }

        // Wait for the transfer to complete
        result = vkQueueWaitIdle(queue);
        if (result != VK_SUCCESS) {
            Logger::get().error("Failed to wait for queue idle: {}", (int)result);
        }

        // Free the temporary command buffer
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

        Logger::get().info("Buffer copy completed successfully: {} bytes", size);
    }
    catch (const std::exception& e) {
        Logger::get().error("Exception during buffer copy: {}", e.what());
    }
}
namespace tremor::gfx { 

    struct PBRMaterialUBO {
        // Basic properties
        alignas(16) float baseColor[4];   // vec4 in shader
        alignas(4) float metallic;        // float in shader
        alignas(4) float roughness;       // float in shader
        alignas(4) float ao;              // float in shader
        alignas(16) float emissive[3];    // vec3 in shader (padded to 16 bytes)

        // Texture availability flags
        alignas(4) int hasAlbedoMap;
        alignas(4) int hasNormalMap;
        alignas(4) int hasMetallicRoughnessMap;
        alignas(4) int hasAoMap;
        alignas(4) int hasEmissiveMap;
    };

    struct PBRMaterial {
        // Base maps
        std::unique_ptr<ImageResource> albedoMap;
        std::unique_ptr<ImageResource> normalMap;
        std::unique_ptr<ImageResource> metallicRoughnessMap; // R: metallic, G: roughness
        std::unique_ptr<ImageResource> aoMap;               // Ambient occlusion
        std::unique_ptr<ImageResource> emissiveMap;

        // Default values for when maps aren't available
        float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emissive[3] = {0.0f, 0.0f, 0.0f};

        // Sampler states
        std::unique_ptr<SamplerResource> sampler;

        // Image views for all textures
        std::unique_ptr<ImageViewResource> albedoImageView;
        std::unique_ptr<ImageViewResource> normalImageView;
        std::unique_ptr<ImageViewResource> metallicRoughnessImageView;
        std::unique_ptr<ImageViewResource> aoImageView;
        std::unique_ptr<ImageViewResource> emissiveImageView;
    };

    struct PBRVertex {
        float position[3];
        float normal[3];
        float tangent[4];    // XYZ = tangent direction, W = handedness for bitangent
        float texCoord[2];

        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(PBRVertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};

            // Position
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(PBRVertex, position);

            // Normal
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(PBRVertex, normal);

            // Tangent
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(PBRVertex, tangent);

            // Texture Coordinates
            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(PBRVertex, texCoord);

            return attributeDescriptions;
        }
    };

    // Basic Vertex structure with position and color
    struct Vertex {
        float position[3];    // XYZ position
        float color[3];       // RGB color
        float texCoord[2];    // UV texture coordinates

        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

            // Position attribute
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, position);

            // Color attribute (we'll keep this for debugging)
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, color);

            // Texture coordinate attribute
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

            return attributeDescriptions;
        }
    };    
    
    // Generic Buffer class that can be used for both vertex and index buffers
    class Buffer {
    public:
        Buffer() = default;

        Buffer(VkDevice device, VkPhysicalDevice physicalDevice,
            VkDeviceSize size, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags memoryProps)
            : m_device(device), m_size(size) {

            // Create buffer
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = usage;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VkBuffer bufferHandle = VK_NULL_HANDLE;
            if (vkCreateBuffer(device, &bufferInfo, nullptr, &bufferHandle) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create buffer");
            }
            m_buffer = BufferResource(device, bufferHandle);

            // Get memory requirements
            VkMemoryRequirements memRequirements;
            vkGetBufferMemoryRequirements(device, m_buffer, &memRequirements);

            // Allocate memory
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(
                physicalDevice, memRequirements.memoryTypeBits, memoryProps);

            VkDeviceMemory memoryHandle = VK_NULL_HANDLE;
            if (vkAllocateMemory(device, &allocInfo, nullptr, &memoryHandle) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate buffer memory");
            }
            m_memory = DeviceMemoryResource(device, memoryHandle);

            // Bind memory to buffer
            vkBindBufferMemory(device, m_buffer, m_memory, 0);
        }

        // Map memory and update buffer data
        void update(const void* data, VkDeviceSize size, VkDeviceSize offset = 0) {
            if (!m_memory) {
                Logger::get().error("Attempting to update buffer with invalid memory");
                return;
            }

            if (size > m_size) {
                Logger::get().error("Buffer update size ({}) exceeds buffer size ({})", size, m_size);
                return;
            }

            void* mappedData = nullptr;
            VkResult result = vkMapMemory(m_device, m_memory, offset, size, 0, &mappedData);

            if (result != VK_SUCCESS) {
                Logger::get().error("Failed to map buffer memory: {}", (int)result);
                return;
            }

            memcpy(mappedData, data, static_cast<size_t>(size));
            vkUnmapMemory(m_device, m_memory);
        }


        // Accessors
        VkBuffer getBuffer() const { return m_buffer; }
        VkDeviceSize getSize() const { return m_size; }

    private:
        VkDevice m_device = VK_NULL_HANDLE;
        BufferResource m_buffer;
        DeviceMemoryResource m_memory;
        VkDeviceSize m_size = 0;

        // Helper function to find memory type
        uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
            uint32_t typeFilter,
            VkMemoryPropertyFlags properties) {
            VkPhysicalDeviceMemoryProperties memProperties;
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
                if ((typeFilter & (1 << i)) &&
                    (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }

            throw std::runtime_error("Failed to find suitable memory type");
        }
    };

    // Vertex Buffer Class
    class VertexBuffer {
    public:
        VertexBuffer() = default;

        template<typename T>
        VertexBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
            VkCommandPool commandPool, VkQueue queue,
            const std::vector<T>& vertices)
            : m_vertexCount(static_cast<uint32_t>(vertices.size())),
            m_stride(sizeof(T)) {

            VkDeviceSize bufferSize = vertices.size() * sizeof(T);

            // Create staging buffer (host visible)
            Buffer stagingBuffer(
                device, physicalDevice, bufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Copy data to staging buffer
            stagingBuffer.update(vertices.data(), bufferSize);

            // Create vertex buffer (device local)
            m_buffer = std::make_unique<Buffer>(
                device, physicalDevice, bufferSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            // Copy from staging buffer to vertex buffer
            copyBuffer(device, commandPool, queue,
                stagingBuffer.getBuffer(), m_buffer->getBuffer(), bufferSize);
        }

        // Bind vertex buffer to command buffer
        void bind(VkCommandBuffer cmdBuffer, uint32_t binding = 0) const {
            if (m_buffer) {
                VkBuffer vertexBuffers[] = { m_buffer->getBuffer() };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(cmdBuffer, binding, 1, vertexBuffers, offsets);
            }
        }

        // Getters
        uint32_t getVertexCount() const { return m_vertexCount; }
        uint32_t getStride() const { return m_stride; }

    private:
        std::unique_ptr<Buffer> m_buffer;
        uint32_t m_vertexCount = 0;
        uint32_t m_stride = 0;
    };

    // Index Buffer Class
    class IndexBuffer {
    public:
        IndexBuffer() = default;

        template<typename T>
        IndexBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
            VkCommandPool commandPool, VkQueue queue,
            const std::vector<T>& indices)
            : m_indexCount(static_cast<uint32_t>(indices.size())) {

            static_assert(std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t>,
                "Index type must be uint16_t or uint32_t");

            m_indexType = sizeof(T) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

            VkDeviceSize bufferSize = indices.size() * sizeof(T);

            // Create staging buffer (host visible)
            Buffer stagingBuffer(
                device, physicalDevice, bufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Copy data to staging buffer
            stagingBuffer.update(indices.data(), bufferSize);

            // Create index buffer (device local)
            m_buffer = std::make_unique<Buffer>(
                device, physicalDevice, bufferSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            // Copy from staging buffer to index buffer
            copyBuffer(device, commandPool, queue,
                stagingBuffer.getBuffer(), m_buffer->getBuffer(), bufferSize);
        }

        // Bind index buffer to command buffer
        void bind(VkCommandBuffer cmdBuffer) const {
            if (m_buffer) {
                vkCmdBindIndexBuffer(cmdBuffer, m_buffer->getBuffer(), 0, m_indexType);
            }
        }

        // Getters
        uint32_t getIndexCount() const { return m_indexCount; }
        VkIndexType getIndexType() const { return m_indexType; }

    private:
        std::unique_ptr<Buffer> m_buffer;
        uint32_t m_indexCount = 0;
        VkIndexType m_indexType = VK_INDEX_TYPE_UINT32;
    };

    // Helper function to infer shader type from filename
    inline ShaderType inferShaderTypeFromFilename(const std::string& filename) {
        if (filename.find(".vert") != std::string::npos) return ShaderType::Vertex;
        if (filename.find(".frag") != std::string::npos) return ShaderType::Fragment;
        if (filename.find(".comp") != std::string::npos) return ShaderType::Compute;
        if (filename.find(".geom") != std::string::npos) return ShaderType::Geometry;
        if (filename.find(".tesc") != std::string::npos) return ShaderType::TessControl;
        if (filename.find(".tese") != std::string::npos) return ShaderType::TessEvaluation;
        if (filename.find(".mesh") != std::string::npos) return ShaderType::Mesh;
        if (filename.find(".task") != std::string::npos) return ShaderType::Task;
        if (filename.find(".rgen") != std::string::npos) return ShaderType::RayGen;
        if (filename.find(".rmiss") != std::string::npos) return ShaderType::RayMiss;
        if (filename.find(".rchit") != std::string::npos) return ShaderType::RayClosestHit;
        if (filename.find(".rahit") != std::string::npos) return ShaderType::RayAnyHit;
        if (filename.find(".rint") != std::string::npos) return ShaderType::RayIntersection;
        if (filename.find(".rcall") != std::string::npos) return ShaderType::Callable;

        // Default to vertex if unknown
        return ShaderType::Vertex;
    }

    // First, add these as member variables if they don't exist already
    class CommandPoolResource {
    private:
        VkDevice m_device = VK_NULL_HANDLE;
        VkCommandPool m_handle = VK_NULL_HANDLE;

    public:
        CommandPoolResource() = default;

        CommandPoolResource(VkDevice device, VkCommandPool handle = VK_NULL_HANDLE)
            : m_device(device), m_handle(handle) {
        }

        ~CommandPoolResource() {
            cleanup();
        }

        // Disable copying
        CommandPoolResource(const CommandPoolResource&) = delete;
        CommandPoolResource& operator=(const CommandPoolResource&) = delete;

        // Enable moving
        CommandPoolResource(CommandPoolResource&& other) noexcept
            : m_device(other.m_device), m_handle(other.m_handle) {
            other.m_handle = VK_NULL_HANDLE;
        }

        CommandPoolResource& operator=(CommandPoolResource&& other) noexcept {
            if (this != &other) {
                cleanup();
                m_device = other.m_device;
                m_handle = other.m_handle;
                other.m_handle = VK_NULL_HANDLE;
            }
            return *this;
        }

        // Accessors
        VkCommandPool& handle() { return m_handle; }
        const VkCommandPool& handle() const { return m_handle; }
        operator VkCommandPool() const { return m_handle; }

        // Check if valid
        operator bool() const { return m_handle != VK_NULL_HANDLE; }

        // Release ownership without destroying
        VkCommandPool release() {
            VkCommandPool temp = m_handle;
            m_handle = VK_NULL_HANDLE;
            return temp;
        }

        // Reset with a new handle
        void reset(VkCommandPool newHandle = VK_NULL_HANDLE) {
            cleanup();
            m_handle = newHandle;
        }

    private:
        void cleanup() {
            if (m_handle != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
                vkDestroyCommandPool(m_device, m_handle, nullptr);
                m_handle = VK_NULL_HANDLE;
            }
        }
    };

    // Instance RAII wrapper
    class InstanceResource {
    private:
        VkInstance m_handle = VK_NULL_HANDLE;

    public:
        InstanceResource() = default;

        explicit InstanceResource(VkInstance handle) : m_handle(handle) {}

        ~InstanceResource() {
        }

        // Disable copying
        InstanceResource(const InstanceResource&) = delete;
        InstanceResource& operator=(const InstanceResource&) = delete;

        // Enable moving
        InstanceResource(InstanceResource&& other) noexcept
            : m_handle(other.m_handle) {
            other.m_handle = VK_NULL_HANDLE;
        }

        InstanceResource& operator=(InstanceResource&& other) noexcept {
            if (this != &other) {
                cleanup();
                m_handle = other.m_handle;
                other.m_handle = VK_NULL_HANDLE;
            }
            return *this;
        }

        // Accessors
        VkInstance& handle() { return m_handle; }
        const VkInstance& handle() const { return m_handle; }
        operator VkInstance() const { return m_handle; }

        // Check if valid
        operator bool() const { return m_handle != VK_NULL_HANDLE; }

        // Release ownership without destroying
        VkInstance release() {
            VkInstance temp = m_handle;
            m_handle = VK_NULL_HANDLE;
            return temp;
        }

        // Reset with a new handle
        void reset(VkInstance newHandle = VK_NULL_HANDLE) {
            cleanup();
            m_handle = newHandle;
        }

    private:
        void cleanup() {
            if (m_handle != VK_NULL_HANDLE) {
                vkDestroyInstance(m_handle, nullptr);
                m_handle = VK_NULL_HANDLE;
            }
        }
    };

    // Surface RAII wrapper
    class SurfaceResource {
    private:
        VkInstance m_instance = VK_NULL_HANDLE;
        VkSurfaceKHR m_handle = VK_NULL_HANDLE;

    public:
        SurfaceResource() = default;

        SurfaceResource(VkInstance instance, VkSurfaceKHR handle = VK_NULL_HANDLE)
            : m_instance(instance), m_handle(handle) {
        }

        ~SurfaceResource() {
            cleanup();
        }

        // Disable copying
        SurfaceResource(const SurfaceResource&) = delete;
        SurfaceResource& operator=(const SurfaceResource&) = delete;

        // Enable moving
        SurfaceResource(SurfaceResource&& other) noexcept
            : m_instance(other.m_instance), m_handle(other.m_handle) {
            other.m_handle = VK_NULL_HANDLE;
        }

        SurfaceResource& operator=(SurfaceResource&& other) noexcept {
            if (this != &other) {
                cleanup();
                m_instance = other.m_instance;
                m_handle = other.m_handle;
                other.m_handle = VK_NULL_HANDLE;
            }
            return *this;
        }

        // Accessors
        VkSurfaceKHR& handle() { return m_handle; }
        const VkSurfaceKHR& handle() const { return m_handle; }
        operator VkSurfaceKHR() const { return m_handle; }

        // Check if valid
        operator bool() const { return m_handle != VK_NULL_HANDLE; }

        // Release ownership without destroying
        VkSurfaceKHR release() {
            VkSurfaceKHR temp = m_handle;
            m_handle = VK_NULL_HANDLE;
            return temp;
        }

        // Reset with a new handle
        void reset(VkSurfaceKHR newHandle = VK_NULL_HANDLE) {
            cleanup();
            m_handle = newHandle;
        }

        // Update the instance (needed if instance changes)
        void setInstance(VkInstance instance) {
            m_instance = instance;
        }

    private:
        void cleanup() {
            if (m_handle != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(m_instance, m_handle, nullptr);
                m_handle = VK_NULL_HANDLE;
            }
        }
    };

    class ShaderCompiler {
    public:
        // Options for shader compilation
        struct CompileOptions {
            bool optimize = true;
            bool generateDebugInfo = false;
            std::vector<std::string> includePaths;
            std::unordered_map<std::string, std::string> macros;
        };

        // Initialize the compiler
        ShaderCompiler() {
            m_compiler = std::make_unique<shaderc::Compiler>();
            m_options = std::make_unique<shaderc::CompileOptions>();
        }

        // Compile GLSL or HLSL source to SPIR-V
        std::vector<uint32_t> compileToSpv(
            const std::string& source,
            ShaderType type,
            const std::string& filename,
            int flags = 0) {

            // Set up options
            //m_options->SetOptimizationLevel(options.optimize ?
            //    shaderc_optimization_level_performance :
            //    shaderc_optimization_level_zero);

            //if (options.generateDebugInfo) {
            //    m_options->SetGenerateDebugInfo();
            //}

            // Add macros
            //for (const auto& [name, value] : options.macros) {
            //    m_options->AddMacroDefinition(name, value);
            //}

            // Add include directories
            // Requires implementing a custom include resolver if needed

            // Determine shader stage
            shaderc_shader_kind kind = getShaderKind(type);

            // Compile
            shaderc::SpvCompilationResult result = m_compiler->CompileGlslToSpv(
                source, kind, filename.c_str(), *m_options);

            // Check for errors
            if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
                Logger::get().error("Shader compilation failed: {}", result.GetErrorMessage());
                return {}; // Return empty vector on error
            }

            // Return SPIR-V binary
            std::vector<uint32_t> spirv(result.cbegin(), result.cend());
            return spirv;
        }

        // Compile a shader file to SPIR-V
        std::vector<uint32_t> compileFileToSpv(
            const std::string& filename,
            ShaderType type,
            const CompileOptions& options = {}) {

            // Read file content
            std::ifstream file(filename);
            if (!file.is_open()) {
                Logger::get().error("Failed to open shader file: {}", filename);
                return {};
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            file.close();

            // Compile the source
            return compileToSpv(buffer.str(), type, filename);
        }

    private:
        // Convert ShaderType to shaderc shader kind
        shaderc_shader_kind getShaderKind(ShaderType type) {
            switch (type) {
            case ShaderType::Vertex:
                return shaderc_vertex_shader;
            case ShaderType::Fragment:
                return shaderc_fragment_shader;
            case ShaderType::Compute:
                return shaderc_compute_shader;
            case ShaderType::Geometry:
                return shaderc_geometry_shader;
            case ShaderType::TessControl:
                return shaderc_tess_control_shader;
            case ShaderType::TessEvaluation:
                return shaderc_tess_evaluation_shader;
            case ShaderType::Mesh:
                return shaderc_mesh_shader;
            case ShaderType::Task:
                return shaderc_task_shader;
            case ShaderType::RayGen:
                return shaderc_raygen_shader;
            case ShaderType::RayMiss:
                return shaderc_miss_shader;
            case ShaderType::RayClosestHit:
                return shaderc_closesthit_shader;
            case ShaderType::RayAnyHit:
                return shaderc_anyhit_shader;
            case ShaderType::RayIntersection:
                return shaderc_intersection_shader;
            case ShaderType::Callable:
                return shaderc_callable_shader;
            default:
                return shaderc_vertex_shader;
            }
        }

        std::unique_ptr<shaderc::Compiler> m_compiler;
        std::unique_ptr<shaderc::CompileOptions> m_options;
    };



    class ShaderModule {
    public:

        // Default constructor
        ShaderModule() = default;

        // Constructor with device and raw module
        ShaderModule(VkDevice device, VkShaderModule rawModule, ShaderType type = ShaderType::Vertex)
            : m_device(device), m_type(type), m_entryPoint("main") {
            if (rawModule != VK_NULL_HANDLE) {
                m_module = std::make_unique<ShaderModuleResource>(device, rawModule);
            }
        }

        // Destructor - resource is automatically cleaned up by RAII
        ~ShaderModule() = default;

        // Load from precompiled SPIR-V file
        static std::unique_ptr<ShaderModule> loadFromFile(VkDevice device, const std::string& filename,
            ShaderType type = ShaderType::Vertex,
            const std::string& entryPoint = "main") {
            // Read file...
            std::ifstream file(filename, std::ios::ate | std::ios::binary);

            if (!file.is_open()) {
                Logger::get().error("Failed to open shader file: {}", filename);
                return nullptr;
            }

            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> shaderCode(fileSize);

            file.seekg(0);
            file.read(shaderCode.data(), fileSize);
            file.close();

            // Create module
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = fileSize;
            createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

            VkShaderModule shaderModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                Logger::get().error("Failed to create shader module from file: {}", filename);
                return nullptr;
            }

            // Create ShaderModule object
            auto result = std::make_unique<ShaderModule>();
            result->m_device = device;
            result->m_module = std::make_unique<ShaderModuleResource>(device, shaderModule);
            result->m_type = type;
            result->m_entryPoint = entryPoint;
            result->m_filename = filename;

            return result;
        }

        // Create shader stage info for pipeline creation
        VkPipelineShaderStageCreateInfo createShaderStageInfo() const {
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = getShaderStageFlagBits();
            stageInfo.module = m_module ? m_module->handle() : VK_NULL_HANDLE;
            stageInfo.pName = m_entryPoint.c_str();
            return stageInfo;
        }

        // Get the raw Vulkan handle directly
        VkShaderModule getHandle() const {
            return m_module ? m_module->handle() : VK_NULL_HANDLE;
        }

        // Check if valid
        bool isValid() const {
            return m_module && m_module->handle() != VK_NULL_HANDLE;
        }

        // Explicit conversion operator
        explicit operator bool() const {
            return isValid();
        }

        // Access methods
        ShaderType getType() const { return m_type; }
        const std::string& getEntryPoint() const { return m_entryPoint; }
        const std::string& getFilename() const { return m_filename; }

        // Compile and load from GLSL/HLSL source
        static std::unique_ptr<ShaderModule> compileFromSource(
            VkDevice device,
            const std::string& source,
            ShaderType type,
            const std::string& filename = "unnamed_shader",
            const std::string& entryPoint = "main",
            const ShaderCompiler::CompileOptions& options = {}) {

            // Compile to SPIR-V
            static ShaderCompiler compiler;
            auto spirv = compiler.compileToSpv(source, type, filename);

            if (spirv.empty()) {
                // Compilation failed
                return nullptr;
            }

            // Create shader module
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = spirv.size() * sizeof(uint32_t);
            createInfo.pCode = spirv.data();

            VkShaderModule shaderModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                Logger::get().error("Failed to create shader module from compiled source");
                return nullptr;
            }

            // Create ShaderModule object
            auto result = std::make_unique<ShaderModule>();
            result->m_device = device;
            result->m_module = std::make_unique<ShaderModuleResource>(device, shaderModule);
            result->m_type = type;
            result->m_entryPoint = entryPoint;
            result->m_filename = filename;
            result->m_spirvCode = std::move(spirv);

            return result;
        }

        // Compile and load from GLSL/HLSL file
        static std::unique_ptr<ShaderModule> compileFromFile(
            VkDevice device,
            const std::string& filename,
            const std::string& entryPoint = "main",
            int flags = 0) {

            // Detect shader type from filename
            ShaderType type = inferShaderTypeFromFilename(filename);

            // Compile file to SPIR-V
            static ShaderCompiler compiler;
            auto spirv = compiler.compileFileToSpv(filename, type);

            if (spirv.empty()) {
                // Compilation failed
                return nullptr;
            }

            // Create shader module
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = spirv.size() * sizeof(uint32_t);
            createInfo.pCode = spirv.data();

            VkShaderModule shaderModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                Logger::get().error("Failed to create shader module from compiled file: {}", filename);
                return nullptr;
            }

            // Create ShaderModule object
            auto result = std::make_unique<ShaderModule>();
            result->m_device = device;
            result->m_module = std::make_unique<ShaderModuleResource>(device, shaderModule);
            result->m_type = type;
            result->m_entryPoint = entryPoint;
            result->m_filename = filename;
            result->m_spirvCode = std::move(spirv);

            return result;
        }

    private:    
        std::vector<uint32_t> m_spirvCode; // Store SPIR-V code

        // Convert shader type to Vulkan stage flag
        VkShaderStageFlagBits getShaderStageFlagBits() const {
            switch (m_type) {
            case ShaderType::Vertex:         return VK_SHADER_STAGE_VERTEX_BIT;
            case ShaderType::Fragment:       return VK_SHADER_STAGE_FRAGMENT_BIT;
            case ShaderType::Compute:        return VK_SHADER_STAGE_COMPUTE_BIT;
            case ShaderType::Geometry:       return VK_SHADER_STAGE_GEOMETRY_BIT;
            case ShaderType::TessControl:    return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            case ShaderType::TessEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            case ShaderType::Mesh:           return VK_SHADER_STAGE_MESH_BIT_EXT;
            case ShaderType::Task:           return VK_SHADER_STAGE_TASK_BIT_EXT;
            case ShaderType::RayGen:         return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            case ShaderType::RayMiss:        return VK_SHADER_STAGE_MISS_BIT_KHR;
            case ShaderType::RayClosestHit:  return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            case ShaderType::RayAnyHit:      return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            case ShaderType::RayIntersection:return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            case ShaderType::Callable:       return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            default:                         return VK_SHADER_STAGE_VERTEX_BIT;
            }
        }

    private:
        VkDevice m_device = VK_NULL_HANDLE;
        std::unique_ptr<ShaderModuleResource> m_module;
        ShaderType m_type = ShaderType::Vertex;
        std::string m_entryPoint = "main";
        std::string m_filename;
    };

    
    // Convenience function for loading shaders with automatic type detection
    inline std::unique_ptr<ShaderModule> loadShader(VkDevice device, const std::string& filename, const std::string& entryPoint = "main") {
        ShaderType type = inferShaderTypeFromFilename(filename);
        return ShaderModule::loadFromFile(device, filename, type, entryPoint);
    }


    class ShaderManager {
    public:
        ShaderManager(VkDevice device) : m_device(device) {}

        // Load a shader with automatic compilation
        std::shared_ptr<ShaderModule> loadShader(
            const std::string& filename,
            const std::string& entryPoint = "main",
            const ShaderCompiler::CompileOptions& options = {}) {

            // Check if already loaded
            auto it = m_shaders.find(filename);
            if (it != m_shaders.end()) {
                return it->second;
            }

            // Determine if it's a SPIR-V or source file
            bool isSpirv = filename.ends_with(".spv");

            // Load the shader
            std::shared_ptr<ShaderModule> shader;
            if (isSpirv) {
                shader = std::shared_ptr<ShaderModule>(
                    ShaderModule::loadFromFile(m_device, filename,
                        inferShaderTypeFromFilename(filename),
                        entryPoint).release());
            }
            else {
                shader = std::shared_ptr<ShaderModule>(
                    ShaderModule::compileFromFile(m_device, filename, entryPoint).release());
            }

            // Store and return
            if (shader) {
                m_shaders[filename] = shader;
                m_shaderFileTimestamps[filename] = getFileTimestamp(filename);
            }

            return shader;
        }

        // Check for shader file changes and reload if needed
        void checkForChanges() {
            for (auto& [filename, shader] : m_shaders) {
                auto currentTimestamp = getFileTimestamp(filename);
                if (currentTimestamp > m_shaderFileTimestamps[filename]) {
                    Logger::get().info("Shader file changed, reloading: {}", filename);

                    // Get current options
                    bool isSpirv = filename.ends_with(".spv");
                    auto entryPoint = shader->getEntryPoint();

                    // Reload the shader
                    std::shared_ptr<ShaderModule> newShader;
                    if (isSpirv) {
                        newShader = std::shared_ptr<ShaderModule>(
                            ShaderModule::loadFromFile(m_device, filename,
                                inferShaderTypeFromFilename(filename),
                                entryPoint).release());
                    }
                    else {
                        ShaderCompiler::CompileOptions options;
                        newShader = std::shared_ptr<ShaderModule>(
                            ShaderModule::compileFromFile(m_device, filename, entryPoint).release());
                    }

                    // Update the shader if reload succeeded
                    if (newShader) {
                        m_shaders[filename] = newShader;
                        m_shaderFileTimestamps[filename] = currentTimestamp;

                        // Notify any systems that need to know about shader changes
                        // This could include pipeline objects that need to be rebuilt
                        notifyShaderReloaded(filename, newShader);
                    }
                }
            }
        }

    private:
        // Get file modification timestamp
        std::filesystem::file_time_type getFileTimestamp(const std::string& filename) {
            try {
                return std::filesystem::last_write_time(filename);
            }
            catch (const std::exception& e) {
                Logger::get().error("Failed to get file timestamp: {}", e.what());
                return std::filesystem::file_time_type();
            }
        }

        // Notify systems about shader reloads
        void notifyShaderReloaded(const std::string& filename, std::shared_ptr<ShaderModule> shader) {
            // You would implement this to notify pipeline cache or other systems
            Logger::get().info("Shader reloaded: {}", filename);
        }

        VkDevice m_device;
        std::unordered_map<std::string, std::shared_ptr<ShaderModule>> m_shaders;
        std::unordered_map<std::string, std::filesystem::file_time_type> m_shaderFileTimestamps;
    };

    // Debug utils messenger wrapper (for debug builds)
    class DebugMessengerResource {
    private:
        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_handle = VK_NULL_HANDLE;

    public:
        DebugMessengerResource() = default;

        DebugMessengerResource(VkInstance instance, VkDebugUtilsMessengerEXT handle = VK_NULL_HANDLE)
            : m_instance(instance), m_handle(handle) {
        }

        ~DebugMessengerResource() {
            cleanup();
        }

        // Disable copying
        DebugMessengerResource(const DebugMessengerResource&) = delete;
        DebugMessengerResource& operator=(const DebugMessengerResource&) = delete;

        // Enable moving
        DebugMessengerResource(DebugMessengerResource&& other) noexcept
            : m_instance(other.m_instance), m_handle(other.m_handle) {
            other.m_handle = VK_NULL_HANDLE;
        }

        DebugMessengerResource& operator=(DebugMessengerResource&& other) noexcept {
            if (this != &other) {
                cleanup();
                m_instance = other.m_instance;
                m_handle = other.m_handle;
                other.m_handle = VK_NULL_HANDLE;
            }
            return *this;
        }

        // Accessors
        VkDebugUtilsMessengerEXT& handle() { return m_handle; }
        const VkDebugUtilsMessengerEXT& handle() const { return m_handle; }
        operator VkDebugUtilsMessengerEXT() const { return m_handle; }

        // Check if valid
        operator bool() const { return m_handle != VK_NULL_HANDLE; }

    private:
        void cleanup() {
            if (m_handle != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
                // Using the function pointer directly since this is an extension function
                auto vkDestroyDebugUtilsMessengerEXT =
                    (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                        m_instance, "vkDestroyDebugUtilsMessengerEXT");

                if (vkDestroyDebugUtilsMessengerEXT) {
                    vkDestroyDebugUtilsMessengerEXT(m_instance, m_handle, nullptr);
                }

                m_handle = VK_NULL_HANDLE;
            }
        }
    };

    // Example resource manager using these RAII resources
    class VulkanResourceManager {
    private:
        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        VkPhysicalDeviceMemoryProperties m_memProperties;

        std::unordered_map<uint32_t, std::unique_ptr<VulkanTexture>> m_textures;
        std::atomic<uint32_t> m_nextTextureId{ 1 };

    public:
        VulkanResourceManager(VkDevice device, VkPhysicalDevice physicalDevice)
            : m_device(device), m_physicalDevice(physicalDevice) {

            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &m_memProperties);
        }

        // Find memory type helper
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
            for (uint32_t i = 0; i < m_memProperties.memoryTypeCount; i++) {
                if ((typeFilter & (1 << i)) &&
                    (m_memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }
            throw std::runtime_error("Failed to find suitable memory type");
        }

        // Example texture creation method
        TextureHandle createTexture(const TextureDesc& desc) {
            auto texture = std::make_unique<VulkanTexture>(m_device);
            texture->width = desc.width;
            texture->height = desc.height;
            texture->mipLevels = desc.mipLevels;
            texture->format = (desc.format);

            // Create image
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = { desc.width, desc.height, 1 };
            imageInfo.mipLevels = desc.mipLevels;
            imageInfo.arrayLayers = 1;
            imageInfo.format = convertFormat(texture->format);
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            if (vkCreateImage(m_device, &imageInfo, nullptr, &texture->image.handle()) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create image");
            }

            // Allocate memory
            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(m_device, texture->image, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(
                memRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            if (vkAllocateMemory(m_device, &allocInfo, nullptr, &texture->memory.handle()) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate image memory");
            }

            // Bind memory to image
            vkBindImageMemory(m_device, texture->image, texture->memory, 0);

            // Create image view
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = texture->image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = convertFormat(texture->format);
            viewInfo.components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY
            };
            viewInfo.subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0, desc.mipLevels,
                0, 1
            };

            if (vkCreateImageView(m_device, &viewInfo, nullptr, &texture->view.handle()) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create image view");
            }

            // Create sampler
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.mipLodBias = 0.0f;
            samplerInfo.anisotropyEnable = VK_TRUE;
            samplerInfo.maxAnisotropy = 16.0f;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = static_cast<float>(desc.mipLevels);

            if (vkCreateSampler(m_device, &samplerInfo, nullptr, &texture->sampler.handle()) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create sampler");
            }

            // Register texture and return handle
            TextureHandle handle; handle.fromId(m_nextTextureId++);
            m_textures[handle.id] = std::move(texture);
            return handle;
        }

        VulkanTexture* getTexture(TextureHandle handle) {
            auto it = m_textures.find(handle.id);
            return it != m_textures.end() ? it->second.get() : nullptr;
        }

        void destroyTexture(TextureHandle handle) {
            m_textures.erase(handle.id);
        }
    };

    class RenderPass {
    public:
        struct Attachment {
            VkFormat format = VK_FORMAT_UNDEFINED;
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
            VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        };

        struct SubpassDependency {
            uint32_t srcSubpass = VK_SUBPASS_EXTERNAL;
            uint32_t dstSubpass = 0;
            VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkAccessFlags srcAccessMask = 0;
            VkAccessFlags dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            VkDependencyFlags dependencyFlags = 0;
        };

        struct CreateInfo {
            std::vector<Attachment> attachments;
            std::vector<SubpassDependency> dependencies;
        };

        RenderPass(VkDevice device, const CreateInfo& createInfo);
        ~RenderPass() = default; // RAII handles cleanup

        // No copying
        RenderPass(const RenderPass&) = delete;
        RenderPass& operator=(const RenderPass&) = delete;

        // Moving is allowed
        RenderPass(RenderPass&&) noexcept = default;
        RenderPass& operator=(RenderPass&&) noexcept = default;

        // Getters
        VkRenderPass handle() const { return m_renderPass; }
        operator VkRenderPass() const { return m_renderPass; }

        // Begin a render pass
        void begin(VkCommandBuffer cmdBuffer, VkFramebuffer framebuffer, const VkRect2D& renderArea,
            const std::vector<VkClearValue>& clearValues);

        // End a render pass
        void end(VkCommandBuffer cmdBuffer);

    private:
        VkDevice m_device;
        RenderPassResource m_renderPass;  // RAII wrapper
    };

    class PipelineState {
    public:
        // Shader stages
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

        // Vertex input state
        VkPipelineVertexInputStateCreateInfo vertexInputState{};

        // Input assembly state
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};

        // Viewport state
        VkPipelineViewportStateCreateInfo viewportState{};

        // Rasterization state
        VkPipelineRasterizationStateCreateInfo rasterizationState{};

        // Multisample state
        VkPipelineMultisampleStateCreateInfo multisampleState{};

        // Depth stencil state
        VkPipelineDepthStencilStateCreateInfo depthStencilState{};

        // Color blend state
        VkPipelineColorBlendStateCreateInfo colorBlendState{};

        // Dynamic state
        VkPipelineDynamicStateCreateInfo dynamicState{};
    };

    class DynamicRenderer {
    public:
        struct ColorAttachment {
            VkImageView imageView = VK_NULL_HANDLE;
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            VkResolveModeFlagBits resolveMode = VK_RESOLVE_MODE_NONE;
            VkImageView resolveImageView = VK_NULL_HANDLE;
            VkImageLayout resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkClearValue clearValue{};
        };

        struct DepthStencilAttachment {
            VkImageView imageView = VK_NULL_HANDLE;
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkClearValue clearValue{};
        };

        struct RenderingInfo {
            VkRect2D renderArea{};
            uint32_t layerCount = 1;
            uint32_t viewMask = 0;
            std::vector<ColorAttachment> colorAttachments;
            std::optional<DepthStencilAttachment> depthStencilAttachment;
        };

        DynamicRenderer() = default;
        ~DynamicRenderer() = default;

        // Begin dynamic rendering
        void begin(VkCommandBuffer cmdBuffer, const RenderingInfo& renderingInfo) {
            // Setup color attachments
            std::vector<VkRenderingAttachmentInfoKHR> colorAttachmentInfos;
            colorAttachmentInfos.reserve(renderingInfo.colorAttachments.size());

            for (const auto& colorAttachment : renderingInfo.colorAttachments) {
                VkRenderingAttachmentInfoKHR attachmentInfo{};
                attachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                attachmentInfo.imageView = colorAttachment.imageView;
                attachmentInfo.imageLayout = colorAttachment.imageLayout;
                attachmentInfo.resolveMode = colorAttachment.resolveMode;
                attachmentInfo.resolveImageView = colorAttachment.resolveImageView;
                attachmentInfo.resolveImageLayout = colorAttachment.resolveImageLayout;
                attachmentInfo.loadOp = colorAttachment.loadOp;
                attachmentInfo.storeOp = colorAttachment.storeOp;
                attachmentInfo.clearValue = colorAttachment.clearValue;

                colorAttachmentInfos.push_back(attachmentInfo);
            }

            // Setup depth-stencil attachment
            VkRenderingAttachmentInfoKHR depthAttachmentInfo{};
            if (renderingInfo.depthStencilAttachment) {
                depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                depthAttachmentInfo.imageView = renderingInfo.depthStencilAttachment->imageView;
                depthAttachmentInfo.imageLayout = renderingInfo.depthStencilAttachment->imageLayout;
                depthAttachmentInfo.loadOp = renderingInfo.depthStencilAttachment->loadOp;
                depthAttachmentInfo.storeOp = renderingInfo.depthStencilAttachment->storeOp;
                depthAttachmentInfo.clearValue = renderingInfo.depthStencilAttachment->clearValue;
            }

            VkRenderingAttachmentInfoKHR stencilAttachmentInfo{};
            if (renderingInfo.depthStencilAttachment) {
                stencilAttachmentInfo = depthAttachmentInfo;
                stencilAttachmentInfo.loadOp = renderingInfo.depthStencilAttachment->stencilLoadOp;
                stencilAttachmentInfo.storeOp = renderingInfo.depthStencilAttachment->stencilStoreOp;
            }

            // Configure rendering info
            VkRenderingInfoKHR info{};
            info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
            info.renderArea = renderingInfo.renderArea;
            info.layerCount = renderingInfo.layerCount;
            info.viewMask = renderingInfo.viewMask;
            info.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentInfos.size());
            info.pColorAttachments = colorAttachmentInfos.data();
            info.pDepthAttachment = renderingInfo.depthStencilAttachment ? &depthAttachmentInfo : nullptr;
            info.pStencilAttachment = renderingInfo.depthStencilAttachment ? &stencilAttachmentInfo : nullptr;

            // Begin dynamic rendering
            vkCmdBeginRendering(cmdBuffer, &info);
        }

        // End dynamic rendering
        void end(VkCommandBuffer cmdBuffer) {
            vkCmdEndRendering(cmdBuffer);
        }
    };

    // Implementation
    RenderPass::RenderPass(VkDevice device, const CreateInfo& createInfo)
        : m_device(device), m_renderPass(device) {

        std::vector<VkAttachmentDescription> attachmentDescriptions;
        attachmentDescriptions.reserve(createInfo.attachments.size());

        for (const auto& attachment : createInfo.attachments) {
            VkAttachmentDescription desc{};
            desc.format = attachment.format;
            desc.samples = attachment.samples;
            desc.loadOp = attachment.loadOp;
            desc.storeOp = attachment.storeOp;
            desc.stencilLoadOp = attachment.stencilLoadOp;
            desc.stencilStoreOp = attachment.stencilStoreOp;
            desc.initialLayout = attachment.initialLayout;
            desc.finalLayout = attachment.finalLayout;
            attachmentDescriptions.push_back(desc);
        }

        // Setup color and depth attachment references
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;  // First attachment
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        if (attachmentDescriptions.size() > 1) {
            depthAttachmentRef.attachment = 1;  // Second attachment for depth
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        // Configure the subpass
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = (attachmentDescriptions.size() > 0) ? 1 : 0;
        subpass.pColorAttachments = (attachmentDescriptions.size() > 0) ? &colorAttachmentRef : nullptr;
        subpass.pDepthStencilAttachment = (attachmentDescriptions.size() > 1) ? &depthAttachmentRef : nullptr;

        // Configure dependencies
        std::vector<VkSubpassDependency> dependencies;
        dependencies.reserve(createInfo.dependencies.size());

        for (const auto& dependency : createInfo.dependencies) {
            VkSubpassDependency dep{};
            dep.srcSubpass = dependency.srcSubpass;
            dep.dstSubpass = dependency.dstSubpass;
            dep.srcStageMask = dependency.srcStageMask;
            dep.dstStageMask = dependency.dstStageMask;
            dep.srcAccessMask = dependency.srcAccessMask;
            dep.dstAccessMask = dependency.dstAccessMask;
            dep.dependencyFlags = dependency.dependencyFlags;
            dependencies.push_back(dep);
        }

        // Create the render pass
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
        renderPassInfo.pAttachments = attachmentDescriptions.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.empty() ? nullptr : dependencies.data();

        if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass.handle()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create render pass");
        }
    }

    void RenderPass::begin(VkCommandBuffer cmdBuffer, VkFramebuffer framebuffer,
        const VkRect2D& renderArea, const std::vector<VkClearValue>& clearValues) {
        VkRenderPassBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass = m_renderPass;
        beginInfo.framebuffer = framebuffer;
        beginInfo.renderArea = renderArea;
        beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        beginInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmdBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void RenderPass::end(VkCommandBuffer cmdBuffer) {
        vkCmdEndRenderPass(cmdBuffer);
    }

    // Framebuffer class to accompany the RenderPass
    class Framebuffer {
    public:
        struct CreateInfo {
            VkRenderPass renderPass;
            std::vector<VkImageView> attachments;
            uint32_t width;
            uint32_t height;
            uint32_t layers = 1;
        };

        Framebuffer(VkDevice device, const CreateInfo& createInfo);
        ~Framebuffer() = default;  // RAII handles cleanup

        // No copying
        Framebuffer(const Framebuffer&) = delete;
        Framebuffer& operator=(const Framebuffer&) = delete;

        // Moving is allowed
        Framebuffer(Framebuffer&&) noexcept = default;
        Framebuffer& operator=(Framebuffer&&) noexcept = default;

        // Getters
        VkFramebuffer handle() const { return m_framebuffer; }
        operator VkFramebuffer() const { return m_framebuffer; }

        uint32_t width() const { return m_width; }
        uint32_t height() const { return m_height; }

    private:
        VkDevice m_device;
        FramebufferResource m_framebuffer;  // RAII wrapper
        uint32_t m_width;
        uint32_t m_height;
        uint32_t m_layers;
    };

    // Implementation
    Framebuffer::Framebuffer(VkDevice device, const CreateInfo& createInfo)
        : m_device(device), m_framebuffer(device),
        m_width(createInfo.width), m_height(createInfo.height), m_layers(createInfo.layers) {

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = createInfo.renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(createInfo.attachments.size());
        framebufferInfo.pAttachments = createInfo.attachments.data();
        framebufferInfo.width = m_width;
        framebufferInfo.height = m_height;
        framebufferInfo.layers = m_layers;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuffer.handle()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }

    class VulkanDevice {
    public:
        // Structure for tracking physical device capabilities
        struct VulkanDeviceCapabilities {
            bool dedicatedAllocation = false;
            bool fullScreenExclusive = false;
            bool rayQuery = false;
            bool meshShaders = false;
            bool bresenhamLineRasterization = false;
            bool nonSolidFill = false;
            bool multiDrawIndirect = false;
            bool sparseBinding = false;  // For megatextures
            bool bufferDeviceAddress = false;  // For ray tracing
            bool dynamicRendering = false;  // Modern rendering without render passes
        };

        // Structure for tracking preferences in device selection
        struct DevicePreferences {
            bool preferDiscreteGPU = true;
            bool requireMeshShaders = false;
            bool requireRayQuery = true;
            bool requireSparseBinding = true;  // For megatextures
            int preferredDeviceIndex = -1;     // -1 means auto-select
        };

        // Constructor
        VulkanDevice(VkInstance instance, VkSurfaceKHR surface,
            const DevicePreferences& preferences = {});

        // Destructor - automatically cleans up Vulkan resources
        ~VulkanDevice() {
            if (m_device != VK_NULL_HANDLE) {
                vkDestroyDevice(m_device, nullptr);
                m_device = VK_NULL_HANDLE;
            }
        }

        // Delete copy operations to prevent double-free
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        // Move operations
        VulkanDevice(VulkanDevice&& other) noexcept;
        VulkanDevice& operator=(VulkanDevice&& other) noexcept;

        // Access the Vulkan handles
        VkDevice device() const { return m_device; }
        VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
        VkQueue graphicsQueue() const { return m_graphicsQueue; }
        uint32_t graphicsQueueFamily() const { return m_graphicsQueueFamily; }

        // Get device capabilities and properties
        const VulkanDeviceCapabilities& capabilities() const { return m_capabilities; }
        const VkPhysicalDeviceProperties& properties() const { return m_deviceProperties; }
        const VkPhysicalDeviceMemoryProperties& memoryProperties() const { return m_memoryProperties; }

        // Format information
        VkFormat colorFormat() const { return m_colorFormat; }
        VkFormat depthFormat() const { return m_depthFormat; }

        // Utility functions
        std::optional<uint32_t> findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

        // Setup functions for specialized features
        void setupBresenhamLineRasterization(VkPipelineRasterizationStateCreateInfo& rasterInfo) const;
        void setupFloatingOriginUniforms(VkDescriptorSetLayoutCreateInfo& layoutInfo) const;

    private:
        // Vulkan handles
        VkInstance m_instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        uint32_t m_graphicsQueueFamily = 0;

        // Device properties
        VkPhysicalDeviceProperties m_deviceProperties{};
        VkPhysicalDeviceFeatures2 m_deviceFeatures2{};
        VkPhysicalDeviceMemoryProperties m_memoryProperties{};

        // Formats
        VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

        // Capabilities
        VulkanDeviceCapabilities m_capabilities{};

        // The surface we're rendering to
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;

        // Initialization helpers
        void selectPhysicalDevice(const DevicePreferences& preferences);
        void createLogicalDevice(const DevicePreferences& preferences);
        void determineFormats();
        void logDeviceInfo() const;

        // Template for structure initialization with sType
        template<typename T>
        static T createStructure() {
            T result{};
            result.sType = getStructureType<T>();
            return result;
        }

        // Helper to get correct sType for Vulkan structures
        template<typename T>
        static VkStructureType getStructureType();
    };



    class SwapChain {
    public:
        struct CreateInfo {
            uint32_t width = 0;
            uint32_t height = 0;
            bool vsync = true;
            bool hdr = false;
            uint32_t imageCount = 2;  // Double buffering by default
            VkFormat preferredFormat = VK_FORMAT_B8G8R8A8_UNORM;
            VkColorSpaceKHR preferredColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        };


        // Constructor takes a device, surface, and creation info
        SwapChain(const VulkanDevice& device, VkSurfaceKHR surface, const CreateInfo& createInfo) : m_device(device), m_surface(surface) {
            // Get logger from device if available
            // (This assumes VulkanDevice has a getLogger method - add if needed)
            // m_logger = device.getLogger();

            createSwapChain(createInfo);
            createImageViews();

                Logger::get().info("Swap chain created: {}x{}, {} images, format: {}, {}",
                    (int)m_extent.width, (int)m_extent.height, (int)m_images.size(),
                    (int)m_imageFormat, m_vsync ? "VSync" : "No VSync");
            
        }

        // Destructor to clean up Vulkan resources
        ~SwapChain();

        // No copying
        SwapChain(const SwapChain&) = delete;
        SwapChain& operator=(const SwapChain&) = delete;

        // Moving is allowed
        SwapChain(SwapChain&& other) noexcept;
        SwapChain& operator=(SwapChain&& other) noexcept;

        // Recreate the swap chain (e.g., on window resize)
        void recreate(uint32_t width, uint32_t height);

        // Get the next image index for rendering
        VkResult acquireNextImage(uint64_t timeout, VkSemaphore signalSemaphore, VkFence fence, uint32_t& outImageIndex);

        // Present the current image
        VkResult present(uint32_t imageIndex, VkSemaphore waitSemaphore);

        // Getters
        VkSwapchainKHR handle() const { return m_swapChain; }
        VkFormat imageFormat() const { return m_imageFormat; }
        VkExtent2D extent() const { return m_extent; }
        const std::vector<VkImage>& images() const { return m_images; }
        const std::vector<ImageViewResource>& imageViews() const { return m_imageViews; }
        uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }
        bool isVSync() const { return m_vsync; }
        bool isHDR() const { return m_hdr; }

    private:
        // References to external objects
        const VulkanDevice& m_device;
        VkSurfaceKHR m_surface;

        // Swap chain objects
        SwapchainResource m_swapChain;
        std::vector<VkImage> m_images;
        std::vector<ImageViewResource> m_imageViews;

        // Properties
        VkFormat m_imageFormat;
        VkColorSpaceKHR m_colorSpace;
        VkExtent2D m_extent;
        bool m_vsync = true;
        bool m_hdr = false;

        // Internal helpers
        void createSwapChain(const CreateInfo& createInfo);
        void cleanup();
        void createImageViews();

        // Choose swap chain properties
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(
            const std::vector<VkSurfaceFormatKHR>& availableFormats,
            VkFormat preferredFormat,
            VkColorSpaceKHR preferredColorSpace);

        VkPresentModeKHR chooseSwapPresentMode(
            const std::vector<VkPresentModeKHR>& availablePresentModes,
            bool vsync);

        VkExtent2D chooseSwapExtent(
            const VkSurfaceCapabilitiesKHR& capabilities,
            uint32_t width,
            uint32_t height);
    };

    // Implementation

    SwapChain::~SwapChain() {
        cleanup();
    }
    

    SwapChain::SwapChain(SwapChain&& other) noexcept
        : m_device(other.m_device),
        m_surface(other.m_surface),
        m_swapChain(std::move(other.m_swapChain)),  // Use std::move here!
        m_images(std::move(other.m_images)),
        m_imageViews(std::move(other.m_imageViews)),
        m_imageFormat(other.m_imageFormat),
        m_colorSpace(other.m_colorSpace),
        m_extent(other.m_extent),
        m_vsync(other.m_vsync),
        m_hdr(other.m_hdr) {

        // No need to reset m_swapChain since the move constructor of VulkanResource already handles that
        // But you might still want to clear these vectors if they're not already handled
        other.m_images.clear();
    }

    SwapChain& SwapChain::operator=(SwapChain&& other) noexcept {
        if (this != &other) {
            // Clean up existing resources
            cleanup();

            // Move resources from other
            m_swapChain = std::move(other.m_swapChain);
            m_images = std::move(other.m_images);
            m_imageViews = std::move(other.m_imageViews);
            m_imageFormat = other.m_imageFormat;
            m_colorSpace = other.m_colorSpace;
            m_extent = other.m_extent;
            m_vsync = other.m_vsync;
            m_hdr = other.m_hdr;

            // Invalidate the other swap chain
            other.m_swapChain = VK_NULL_HANDLE;
            other.m_images.clear();
            other.m_imageViews.clear();
        }

        return *this;
    }

    void SwapChain::recreate(uint32_t width, uint32_t height) {
        // Save current settings
        CreateInfo createInfo{};
        createInfo.width = width;
        createInfo.height = height;
        createInfo.vsync = m_vsync;
        createInfo.hdr = m_hdr;
        createInfo.imageCount = static_cast<uint32_t>(m_images.size());
        createInfo.preferredFormat = m_imageFormat;
        createInfo.preferredColorSpace = m_colorSpace;

        // Clean up existing swap chain
        cleanup();

        // Create new swap chain
        createSwapChain(createInfo);
        createImageViews();

        Logger::get().info("Swap chain recreated: {}x{}", m_extent.width, m_extent.height);
        
    }

    VkResult SwapChain::acquireNextImage(uint64_t timeout, VkSemaphore signalSemaphore, VkFence fence, uint32_t& outImageIndex) {
        return vkAcquireNextImageKHR(m_device.device(), m_swapChain, timeout, signalSemaphore, fence, &outImageIndex);
    }

    VkResult SwapChain::present(uint32_t imageIndex, VkSemaphore waitSemaphore) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = waitSemaphore ? 1 : 0;
        presentInfo.pWaitSemaphores = waitSemaphore ? &waitSemaphore : nullptr;

        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapChain.handle();
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = nullptr;

        return vkQueuePresentKHR(m_device.graphicsQueue(), &presentInfo);
    }

    void SwapChain::createSwapChain(const CreateInfo& createInfo) {
        // Query surface capabilities
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device.physicalDevice(), m_surface, &capabilities);

        // Query supported formats
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.physicalDevice(), m_surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.physicalDevice(), m_surface, &formatCount, formats.data());

        // Query supported present modes
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.physicalDevice(), m_surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.physicalDevice(), m_surface, &presentModeCount, presentModes.data());

        // Choose format, present mode, and extent
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(formats, createInfo.preferredFormat, createInfo.preferredColorSpace);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes, createInfo.vsync);
        VkExtent2D extent = chooseSwapExtent(capabilities, createInfo.width, createInfo.height);

        // Determine image count
        uint32_t imageCount = createInfo.imageCount;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }
        if (imageCount < capabilities.minImageCount) {
            imageCount = capabilities.minImageCount;
        }

        // Create swap chain
        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = m_surface;
        swapchainInfo.minImageCount = imageCount;
        swapchainInfo.imageFormat = surfaceFormat.format;
        swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
        swapchainInfo.imageExtent = extent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        // Add transfer usage for screenshots if needed
        swapchainInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        // Handle queue families
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.queueFamilyIndexCount = 0;
        swapchainInfo.pQueueFamilyIndices = nullptr;

        swapchainInfo.preTransform = capabilities.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = presentMode;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

        // Check for HDR support if requested
        m_hdr = createInfo.hdr && (surfaceFormat.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

        // Create the swap chain
        if (vkCreateSwapchainKHR(m_device.device(), &swapchainInfo, nullptr, &m_swapChain.handle()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swap chain");
        }

        m_swapChain = SwapchainResource(m_device.device(), m_swapChain.handle());

        // Get swap chain images
        vkGetSwapchainImagesKHR(m_device.device(), m_swapChain, &imageCount, nullptr);
        m_images.resize(imageCount);
        vkGetSwapchainImagesKHR(m_device.device(), m_swapChain, &imageCount, m_images.data());

        // Store format and extent
        m_imageFormat = surfaceFormat.format;
        m_colorSpace = surfaceFormat.colorSpace;
        m_extent = extent;
        m_vsync = (presentMode == VK_PRESENT_MODE_FIFO_KHR || presentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);
    }

    void SwapChain::createImageViews() {
        m_imageViews.resize(m_images.size());

        for (size_t i = 0; i < m_images.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_images[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_imageFormat;

            // Default color mapping
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            // Only using color aspect
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(m_device.device(), &createInfo, nullptr, &m_imageViews[i].handle()) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create image view");
            }
        }
    }

    void SwapChain::cleanup() {
        // Destroy image views
        
        m_imageViews.clear();

        // Destroy swap chain
		m_swapChain.reset();

        // Images are owned by the swap chain and destroyed with it
        m_images.clear();
    }

    VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats,
        VkFormat preferredFormat,
        VkColorSpaceKHR preferredColorSpace) {

        // First check for preferred format and color space
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == preferredFormat &&
                availableFormat.colorSpace == preferredColorSpace) {
                return availableFormat;
            }
        }

        // Check for HDR formats (for HDR displays)
        if (m_hdr) {
            const std::array<VkFormat, 3> hdrFormats = {
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16
            };

            for (auto hdrFormat : hdrFormats) {
                for (const auto& availableFormat : availableFormats) {
                    if (availableFormat.format == hdrFormat &&
                        availableFormat.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                        return availableFormat;
                    }
                }
            }
        }

        // Standard 8-bit formats
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM ||
                availableFormat.format == VK_FORMAT_R8G8B8A8_UNORM) {
                return availableFormat;
            }
        }

        // Fallback to first available format
        return availableFormats[0];
    }

    VkPresentModeKHR SwapChain::chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes,
        bool vsync) {

        if (vsync) {
            // FIFO is guaranteed to be available and provides proper vsync
            return VK_PRESENT_MODE_FIFO_KHR;
        }
        else {
            // Try to find immediate mode for no vsync
            for (const auto& mode : availablePresentModes) {
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    return mode;
                }
            }

            // Try mailbox for triple buffering
            for (const auto& mode : availablePresentModes) {
                if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                    return mode;
                }
            }

            // Fallback to FIFO which is guaranteed to be available
            return VK_PRESENT_MODE_FIFO_KHR;
        }
    }

    VkExtent2D SwapChain::chooseSwapExtent(
        const VkSurfaceCapabilitiesKHR& capabilities,
        uint32_t width,
        uint32_t height) {

        if (capabilities.currentExtent.width != UINT32_MAX) {
            // Surface size is fixed
            return capabilities.currentExtent;
        }
        else {
            // Surface size can vary, clamp to allowed range
            VkExtent2D actualExtent = { width, height };

            actualExtent.width = std::clamp(actualExtent.width,
                capabilities.minImageExtent.width,
                capabilities.maxImageExtent.width);

            actualExtent.height = std::clamp(actualExtent.height,
                capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    // Vulkan-specific device selection helper
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    // Vulkan-specific swap chain information
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    // Class for managing Vulkan device and related resources
    // Template specializations for structure types
    template<> inline VkStructureType VulkanDevice::getStructureType<VkDeviceCreateInfo>() { return VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; }
    template<> inline VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceFeatures2>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; }
    template<> inline VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceVulkan12Features>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES; }
    template<> inline VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceVulkan13Features>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES; }
    template<> inline VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceProperties2>() { return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; }
    // Add more as needed...

    // Implementation

    VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface,
        const DevicePreferences& preferences)
        : m_instance(instance), m_surface(surface) {

        // Initialize Volk
        VkResult volkResult = volkInitialize();
        if (volkResult != VK_SUCCESS) {
            throw std::runtime_error("Failed to initialize Volk");
        }

        // Load Vulkan instance-level functions
        volkLoadInstance(instance);

        // Select physical device
        selectPhysicalDevice(preferences);

        // Create logical device
        createLogicalDevice(preferences);

        // Load device-level functions through Volk
        volkLoadDevice(m_device);

        // Get device formats
        determineFormats();

        // Log device information
        logDeviceInfo();
    }

    void VulkanDevice::selectPhysicalDevice(const DevicePreferences& preferences) {
        // Count available devices
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

        if (deviceCount == 0) {
            throw std::runtime_error("Failed to find any Vulkan physical devices");
        }

        // Get physical devices
        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, physicalDevices.data());

        // Check if a specific device was requested
        if (preferences.preferredDeviceIndex >= 0 &&
            preferences.preferredDeviceIndex < static_cast<int>(deviceCount)) {
            m_physicalDevice = physicalDevices[preferences.preferredDeviceIndex];
        }
        else {
            // Select the best device based on preferences
            struct DeviceRanking {
                VkPhysicalDevice device;
                int score = 0;
            };

            std::vector<DeviceRanking> rankings;

            for (const auto& device : physicalDevices) {
                DeviceRanking ranking{ device, 0 };

                // Get device properties
                VkPhysicalDeviceProperties deviceProperties;
                vkGetPhysicalDeviceProperties(device, &deviceProperties);

                // Check for discrete GPU
                if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    ranking.score += 1000;
                }

                // Check for queue families
                uint32_t queueFamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
                std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

                bool hasGraphicsQueue = false;

                for (uint32_t i = 0; i < queueFamilyCount; i++) {
                    VkBool32 presentSupport = false;
                    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

                    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
                        hasGraphicsQueue = true;
                        break;
                    }
                }

                if (!hasGraphicsQueue) {
                    continue;  // Skip devices without graphics queue
                }

                // Check for extensions
                uint32_t extensionCount;
                vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
                std::vector<VkExtensionProperties> availableExtensions(extensionCount);
                vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

                bool hasSwapchainExtension = false;
                bool hasMeshShaderExtension = false;
                bool hasRayQueryExtension = false;
                bool hasSparseBindingSupport = false;
                bool hasBresenhamLineRasterization = false;

                for (const auto& extension : availableExtensions) {
                    if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                        hasSwapchainExtension = true;
                    }
                    if (strcmp(extension.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0) {
                        hasMeshShaderExtension = true;
                        ranking.score += 100;
                    }
                    if (strcmp(extension.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0) {
                        hasRayQueryExtension = true;
                        ranking.score += 200;
                    }
                    if (strcmp(extension.extensionName, VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME) == 0) {
                        hasBresenhamLineRasterization = true;
                        ranking.score += 50;
                    }
                }

                // Check for sparse binding support
                VkPhysicalDeviceFeatures features;
                vkGetPhysicalDeviceFeatures(device, &features);
                hasSparseBindingSupport = features.sparseBinding;

                if (hasSparseBindingSupport) {
                    ranking.score += 150;  // Important for megatextures
                }

                // Mandatory extensions
                if (!hasSwapchainExtension) {
                    continue;  // Skip devices without swapchain
                }

                // Required features based on preferences
                if (preferences.requireMeshShaders && !hasMeshShaderExtension) {
                    continue;
                }

                if (preferences.requireRayQuery && !hasRayQueryExtension) {
                    continue;
                }

                if (preferences.requireSparseBinding && !hasSparseBindingSupport) {
                    continue;
                }

                // Score based on device properties
                ranking.score += deviceProperties.limits.maxImageDimension2D / 256;

                rankings.push_back(ranking);
            }

            // Sort by score
            std::sort(rankings.begin(), rankings.end(),
                [](const DeviceRanking& a, const DeviceRanking& b) {
                    return a.score > b.score;
                });

            if (rankings.empty()) {
                throw std::runtime_error("No suitable Vulkan device found");
            }

            m_physicalDevice = rankings[0].device;
        }

        // Get physical device properties
        vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProperties);
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);
    }

    void VulkanDevice::createLogicalDevice(const DevicePreferences& preferences) {
        // Find queue family with graphics and present support
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

        // Find graphics queue family
        bool found = false;
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);

            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
                m_graphicsQueueFamily = i;
                found = true;
                break;
            }
        }

        if (!found) {
            throw std::runtime_error("Could not find a queue family with both graphics and present support");
        }

        // Query device extensions
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, availableExtensions.data());

        // Check for required extensions and set capabilities
        for (const auto& extension : availableExtensions) {
            if (strcmp(extension.extensionName, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0) {
                m_capabilities.dedicatedAllocation = true;
            }
            else if (strcmp(extension.extensionName, "VK_EXT_full_screen_exclusive") == 0) {
                m_capabilities.fullScreenExclusive = true;
            }
            else if (strcmp(extension.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0) {
                m_capabilities.rayQuery = true;
            }
            else if (strcmp(extension.extensionName, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0) {
                m_capabilities.meshShaders = true;
            }
            else if (strcmp(extension.extensionName, VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME) == 0) {
                m_capabilities.bresenhamLineRasterization = true;
            }
        }

        // Set up device features
        // Vulkan 1.4 uses a chain of features structures
        auto features2 = createStructure<VkPhysicalDeviceFeatures2>();
        auto vulkan12Features = createStructure<VkPhysicalDeviceVulkan12Features>();
        auto vulkan13Features = createStructure<VkPhysicalDeviceVulkan13Features>();

        // Chain them together
        features2.pNext = &vulkan12Features;
        vulkan12Features.pNext = &vulkan13Features;

        // Get the features the device supports
        vkGetPhysicalDeviceFeatures2(m_physicalDevice, &features2);

        // Setup mesh shader features if available
        VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
        if (m_capabilities.meshShaders) {
            meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
            meshShaderFeatures.pNext = nullptr;

            // Chain it to the end
            vulkan13Features.pNext = &meshShaderFeatures;
        }

        // Setup line rasterization features if available
        VkPhysicalDeviceLineRasterizationFeaturesEXT lineRasterizationFeatures{};
        if (m_capabilities.bresenhamLineRasterization) {
            lineRasterizationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT;

            // Chain it properly
            if (m_capabilities.meshShaders) {
                lineRasterizationFeatures.pNext = nullptr;
                meshShaderFeatures.pNext = &lineRasterizationFeatures;
            }
            else {
                lineRasterizationFeatures.pNext = nullptr;
                vulkan13Features.pNext = &lineRasterizationFeatures;
            }
        }

        // Settings we want to enable
        m_capabilities.nonSolidFill = features2.features.fillModeNonSolid;
        m_capabilities.multiDrawIndirect = features2.features.multiDrawIndirect;
        m_capabilities.sparseBinding = features2.features.sparseBinding;
        m_capabilities.bufferDeviceAddress = vulkan12Features.bufferDeviceAddress;
        m_capabilities.dynamicRendering = vulkan13Features.dynamicRendering;

        // Create list of extensions we want to enable
        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        // Add optional extensions based on capabilities
        if (m_capabilities.dedicatedAllocation) {
            deviceExtensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
        }

        if (m_capabilities.fullScreenExclusive) {
            deviceExtensions.push_back("VK_EXT_full_screen_exclusive");
        }

        if (m_capabilities.rayQuery) {
            deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        }

        if (m_capabilities.meshShaders) {
            deviceExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
        }

        if (m_capabilities.bresenhamLineRasterization) {
            deviceExtensions.push_back(VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME);
        }

        // Create the logical device
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        // Create physical device features
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        deviceFeatures.fillModeNonSolid = m_capabilities.nonSolidFill ? VK_TRUE : VK_FALSE;
        deviceFeatures.multiDrawIndirect = m_capabilities.multiDrawIndirect ? VK_TRUE : VK_FALSE;
        deviceFeatures.sparseBinding = m_capabilities.sparseBinding ? VK_TRUE : VK_FALSE;

        // Enable Vulkan 1.2 features
        vulkan12Features.bufferDeviceAddress = m_capabilities.bufferDeviceAddress ? VK_TRUE : VK_FALSE;
        vulkan12Features.descriptorIndexing = VK_TRUE;  // Needed for ray tracing

        // Enable Vulkan 1.3 features
        vulkan13Features.dynamicRendering = m_capabilities.dynamicRendering ? VK_TRUE : VK_FALSE;

        // Enable mesh shader features if available
        if (m_capabilities.meshShaders) {
            meshShaderFeatures.taskShader = VK_TRUE;
            meshShaderFeatures.meshShader = VK_TRUE;
        }

        // Enable line rasterization features if available
        if (m_capabilities.bresenhamLineRasterization) {
            lineRasterizationFeatures.bresenhamLines = VK_TRUE;
            lineRasterizationFeatures.rectangularLines = VK_TRUE;
        }

        // Create the device
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &features2;  // Chain of feature structures
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        createInfo.pEnabledFeatures = nullptr;  // Using pNext chain instead

        if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create logical device");
        }

        // Get device queue
        vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    }

    void VulkanDevice::determineFormats() {
        // Find best color format
        m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;  // Default

        // Check if we can use higher bit depth
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_A2B10G10R10_UNORM_PACK32, &formatProps);

        constexpr VkFormatFeatureFlags requiredColorFeatures =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;

        if ((formatProps.optimalTilingFeatures & requiredColorFeatures) == requiredColorFeatures) {
            m_colorFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;  // Higher precision
        }

        // Find best depth format
        const std::array<VkFormat, 3> depthFormats = {
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM_S8_UINT
        };

        for (auto format : depthFormats) {
            vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &formatProps);
            if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                m_depthFormat = format;
                break;
            }
        }

        if (m_depthFormat == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("Failed to find supported depth format");
        }
    }

    void VulkanDevice::logDeviceInfo() const {
        //if (!m_logger) return;

        // Log device information
        std::string vendorName;
        switch (m_deviceProperties.vendorID) {
        case 0x1002: vendorName = "AMD"; break;
        case 0x10DE: vendorName = "NVIDIA"; break;
        case 0x8086: vendorName = "Intel"; break;
        case 0x13B5: vendorName = "ARM"; break;
        case 0x5143: vendorName = "Qualcomm"; break;
        default: vendorName = std::format("Unknown (0x{:X})", m_deviceProperties.vendorID);
        }

        // Log basic device info
        Logger::get().info("Selected GPU: {} ({})", m_deviceProperties.deviceName, vendorName);
        Logger::get().info("Driver version: {}.{}.{}",
            VK_VERSION_MAJOR(m_deviceProperties.driverVersion),
            VK_VERSION_MINOR(m_deviceProperties.driverVersion),
            VK_VERSION_PATCH(m_deviceProperties.driverVersion));

        // Log color and depth formats
        Logger::get().info("Color format: {}", m_colorFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ?
            "A2B10G10R10 (10-bit)" : "R8G8B8A8 (8-bit)");

        std::string depthFormatStr;
        switch (m_depthFormat) {
        case VK_FORMAT_D32_SFLOAT_S8_UINT: depthFormatStr = "D32_S8 (32-bit)"; break;
        case VK_FORMAT_D24_UNORM_S8_UINT: depthFormatStr = "D24_S8 (24-bit)"; break;
        case VK_FORMAT_D16_UNORM_S8_UINT: depthFormatStr = "D16_S8 (16-bit)"; break;
        default: depthFormatStr = "Unknown";
        }
        Logger::get().info("Depth format: {}", depthFormatStr);

        // Log capabilities
        Logger::get().info("Device capabilities:");
        Logger::get().info("  - Ray Query: {}", m_capabilities.rayQuery ? "Yes" : "No");
        Logger::get().info("  - Mesh Shaders: {}", m_capabilities.meshShaders ? "Yes" : "No");
        Logger::get().info("  - Bresenham Line Rasterization: {}", m_capabilities.bresenhamLineRasterization ? "Yes" : "No");
        Logger::get().info("  - Sparse Binding (MegaTextures): {}", m_capabilities.sparseBinding ? "Yes" : "No");
        Logger::get().info("  - Dynamic Rendering: {}", m_capabilities.dynamicRendering ? "Yes" : "No");
        Logger::get().info("  - Buffer Device Address: {}", m_capabilities.bufferDeviceAddress ? "Yes" : "No");
    }

    std::optional<uint32_t> VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (m_memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        return std::nullopt;
    }

    void VulkanDevice::setupBresenhamLineRasterization(VkPipelineRasterizationStateCreateInfo& rasterInfo) const {
        if (!m_capabilities.bresenhamLineRasterization) {
            return;  // Not supported
        }

        // Create line rasterization info
        VkPipelineRasterizationLineStateCreateInfoEXT lineRasterInfo{};
        lineRasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT;
        lineRasterInfo.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT;
        lineRasterInfo.stippledLineEnable = VK_FALSE;

        // Link it to the rasterization state
        rasterInfo.pNext = &lineRasterInfo;
    }

    void VulkanDevice::setupFloatingOriginUniforms(VkDescriptorSetLayoutCreateInfo& layoutInfo) const {
        // Define bindings for camera-relative rendering
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            // Camera world position as 64-bit integers
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            }
        };

        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
    }

    class VulkanBackend : public RenderBackend {
    public:

		SwapChain::CreateInfo ci{};

        std::unique_ptr<VulkanDevice> vkDevice;
        std::unique_ptr<SwapChain> vkSwapchain;

        InstanceResource instance;
        SurfaceResource surface;

        SDL_Window* w;

        std::unique_ptr<VulkanResourceManager> res;
        std::unique_ptr<Buffer> m_materialBuffer;

#if _DEBUG
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
#endif

		VulkanBackend() = default;
        ~VulkanBackend() override = default;

        struct UniformBufferObject {
            alignas(16) glm::mat4 model;
            alignas(16) glm::mat4 view;
            alignas(16) glm::mat4 proj;
            alignas(16) glm::vec3 cameraPos;
        };

        // Add this to your VulkanBackend class as a member variable
        std::unique_ptr<Buffer> m_uniformBuffer;

        // Create a method to create the UBO
        bool createUniformBuffer() {
            // Create the uniform buffer
            VkDeviceSize bufferSize = sizeof(UniformBufferObject);
            m_uniformBuffer = std::make_unique<Buffer>(
                device,
                physicalDevice,
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Initialize the UBO with identity matrices and a camera position
            updateUniformBuffer();

            return true;
        }

        // Add a method to update the UBO every frame
        void updateUniformBuffer() {
            static auto startTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

            UniformBufferObject ubo{};

            // Create a simple model matrix (rotate the quad over time)
            ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));

            // Create a view matrix (look at the quad from a slight distance)
            ubo.view = glm::lookAt(
                glm::vec3(0.0f, 0.0f, 4.0f),  // Move camera further back
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f)
            );

            // Create a projection matrix (perspective projection)
            // Get the window size for aspect ratio
            int width, height;
            SDL_GetWindowSize(w, &width, &height);
            float aspect = width / (float)height;

            ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.01f, 100.0f);

            // Flip Y coordinate for Vulkan
            ubo.proj[1][1] *= -1;

            // Set camera position (same as view matrix eye position)
            ubo.cameraPos = glm::vec3(0.0f, 0.0f, 4.0f);

            // Update the buffer data
            m_uniformBuffer->update(&ubo, sizeof(ubo));

            Logger::get().info("Model matrix:\n{} {} {} {}\n{} {} {} {}\n{} {} {} {}\n{} {} {} {}",
                ubo.model[0][0], ubo.model[0][1], ubo.model[0][2], ubo.model[0][3],
                ubo.model[1][0], ubo.model[1][1], ubo.model[1][2], ubo.model[1][3],
                ubo.model[2][0], ubo.model[2][1], ubo.model[2][2], ubo.model[2][3],
                ubo.model[3][0], ubo.model[3][1], ubo.model[3][2], ubo.model[3][3]
                ); // Similar for view and proj

            Logger::get().info("View matrix:\n{} {} {} {}\n{} {} {} {}\n{} {} {} {}\n{} {} {} {}",
                ubo.view[0][0], ubo.view[0][1], ubo.view[0][2], ubo.view[0][3],
                ubo.view[1][0], ubo.view[1][1], ubo.view[1][2], ubo.view[1][3],
                ubo.view[2][0], ubo.view[2][1], ubo.view[2][2], ubo.view[2][3],
                ubo.view[3][0], ubo.view[3][1], ubo.view[3][2], ubo.view[3][3]
            ); // Similar for proj and proj

            Logger::get().info("proj matrix:\n{} {} {} {}\n{} {} {} {}\n{} {} {} {}\n{} {} {} {}",
                ubo.proj[0][0], ubo.proj[0][1], ubo.proj[0][2], ubo.proj[0][3],
                ubo.proj[1][0], ubo.proj[1][1], ubo.proj[1][2], ubo.proj[1][3],
                ubo.proj[2][0], ubo.proj[2][1], ubo.proj[2][2], ubo.proj[2][3],
                ubo.proj[3][0], ubo.proj[3][1], ubo.proj[3][2], ubo.proj[3][3]
            ); // Similar for view and proj

        }

        // Add a method to create a light UBO as well
        struct LightUBO {
            alignas(16) glm::vec3 direction;
            alignas(16) glm::vec3 color;
        };

        std::unique_ptr<Buffer> m_lightBuffer;

        bool createLightBuffer() {
            // Create the light uniform buffer
            VkDeviceSize bufferSize = sizeof(LightUBO);
            m_lightBuffer = std::make_unique<Buffer>(
                device,
                physicalDevice,
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Initialize with default light values
            LightUBO light{};
            light.direction = glm::normalize(glm::vec3(1.0f, -3.0f, -2.0f)); // Light coming from above-right
            light.color = glm::vec3(1.0f, 1.0f, 1.0f); // White light

            m_lightBuffer->update(&light, sizeof(light));

            return true;
        }

        void createMaterialUBO() {
            // Create the uniform buffer for material properties
            VkDeviceSize uboSize = sizeof(PBRMaterialUBO);
            m_materialBuffer = std::make_unique<Buffer>(
                device,
                physicalDevice,
                uboSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Initialize the material data
            PBRMaterialUBO uboData = {};

            // Set default values
            uboData.baseColor[0] = 1.0f;  // r
            uboData.baseColor[1] = 1.0f;  // g
            uboData.baseColor[2] = 1.0f;  // b
            uboData.baseColor[3] = 1.0f;  // a

            uboData.metallic = 0.0f;
            uboData.roughness = 0.5f;
            uboData.ao = 1.0f;

            uboData.emissive[0] = 0.0f;   // r
            uboData.emissive[1] = 0.0f;   // g
            uboData.emissive[2] = 0.0f;   // b

            // Indicate which textures are available
            uboData.hasAlbedoMap = 1;  // We have an albedo texture
            uboData.hasNormalMap = 0;  // We don't have these other textures yet
            uboData.hasMetallicRoughnessMap = 0;
            uboData.hasAoMap = 0;
            uboData.hasEmissiveMap = 0;

            // Upload the data to the GPU
            m_materialBuffer->update(&uboData, uboSize);
        }

        bool createCommandPool() {
            // Get the graphics queue family index
            uint32_t queueFamilyIndex = vkDevice->graphicsQueueFamily();

            // Command pool creation info
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = queueFamilyIndex;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Allow individual command buffer reset

            // Create the command pool
            VkCommandPool commandPool = VK_NULL_HANDLE;
            if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
                Logger::get().error("Failed to create command pool");
                return false;
            }

            // Store in RAII wrapper
            m_commandPool = std::make_unique<CommandPoolResource>(device, commandPool);
            Logger::get().info("Command pool created successfully");

            return true;
        }

        bool createCommandBuffers() {
            // Make sure we have a command pool
            if (!m_commandPool || !*m_commandPool) {
                Logger::get().error("Cannot create command buffers without a valid command pool");
                return false;
            }

            // Resize the command buffer vector
            m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

            // Allocate command buffers
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = *m_commandPool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

            if (vkAllocateCommandBuffers(device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
                Logger::get().error("Failed to allocate command buffers");
                return false;
            }

            Logger::get().info("Command buffers created successfully: {}", m_commandBuffers.size());
            return true;
        }

        bool createSyncObjects() {
            // Resize sync object vectors
            m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
            m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
            m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

            // Create semaphores and fences for each frame
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled so first frame doesn't wait indefinitely

            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                // Create image available semaphore
                VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
                if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS) {
                    Logger::get().error("Failed to create image available semaphore for frame {}", i);
                    return false;
                }
                m_imageAvailableSemaphores[i] = SemaphoreResource(device, imageAvailableSemaphore);

                // Create render finished semaphore
                VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
                if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {
                    Logger::get().error("Failed to create render finished semaphore for frame {}", i);
                    return false;
                }
                m_renderFinishedSemaphores[i] = SemaphoreResource(device, renderFinishedSemaphore);

                // Create in-flight fence
                VkFence inFlightFence = VK_NULL_HANDLE;
                if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
                    Logger::get().error("Failed to create in-flight fence for frame {}", i);
                    return false;
                }
                m_inFlightFences[i] = FenceResource(device, inFlightFence);
            }

            Logger::get().info("Synchronization objects created successfully");
            return true;
        }

        class VertexBufferSimple {
        public:
            VertexBufferSimple(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, size_t vertexCount)
                : m_device(device), m_buffer(buffer), m_memory(memory), m_vertexCount(vertexCount) {
            }

            ~VertexBufferSimple() {
                if (m_buffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(m_device, m_buffer, nullptr);
                    m_buffer = VK_NULL_HANDLE;
                }

                if (m_memory != VK_NULL_HANDLE) {
                    vkFreeMemory(m_device, m_memory, nullptr);
                    m_memory = VK_NULL_HANDLE;
                }
            }

            // Bind the vertex buffer
            void bind(VkCommandBuffer cmdBuffer) const {
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &m_buffer, offsets);
            }

            // Get vertex count
            uint32_t getVertexCount() const { return static_cast<uint32_t>(m_vertexCount); }

        private:
            VkDevice m_device;
            VkBuffer m_buffer = VK_NULL_HANDLE;
            VkDeviceMemory m_memory = VK_NULL_HANDLE;
            size_t m_vertexCount = 0;
        };


        struct SimpleVertex {
    float position[3];   // XYZ position - location 0
    float texCoord[2];   // UV coordinates - location 3 (to match your existing layout)

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(SimpleVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

        // Position attribute
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(SimpleVertex, position);

        // Texture coordinate attribute
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 3;  // Keep location 3 to match your existing code
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(SimpleVertex, texCoord);

        return attributeDescriptions;
    }
};

        bool createTriangle() {
            try {
                // Create command pool for transfers if needed
                if (!m_transferCommandPool) {
                    VkCommandPoolCreateInfo poolInfo{};
                    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                    poolInfo.queueFamilyIndex = vkDevice->graphicsQueueFamily();
                    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

                    VkCommandPool cmdPool;
                    if (vkCreateCommandPool(device, &poolInfo, nullptr, &cmdPool) != VK_SUCCESS) {
                        Logger::get().error("Failed to create transfer command pool");
                        return false;
                    }
                    m_transferCommandPool = CommandPoolResource(device, cmdPool);
                    Logger::get().info("Transfer command pool created");
                }

                // Create a quad with texture coordinates instead
                std::vector<SimpleVertex> vertices = {
                    // First triangle (counter-clockwise)
                    // position              texCoord
                    {{-0.5f, -0.5f, 0.0f},  {0.0f, 0.0f}}, // Bottom left
                    {{ 0.5f,  0.5f, 0.0f},  {1.0f, 1.0f}}, // Top right
                    {{-0.5f,  0.5f, 0.0f},  {0.0f, 1.0f}}, // Top left

                    // Second triangle (counter-clockwise) 
                    {{-0.5f, -0.5f, 0.0f},  {0.0f, 0.0f}}, // Bottom left
                    {{ 0.5f, -0.5f, 0.0f},  {1.0f, 0.0f}}, // Bottom right
                    {{ 0.5f,  0.5f, 0.0f},  {1.0f, 1.0f}}, // Top right
                };


                // Create vertex buffer with HOST_VISIBLE memory
                VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();

                // Create buffer
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = vertexBufferSize;
                bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                VkBuffer vertexBufferHandle;
                if (vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBufferHandle) != VK_SUCCESS) {
                    Logger::get().error("Failed to create vertex buffer");
                    return false;
                }

                // Get memory requirements
                VkMemoryRequirements memRequirements;
                vkGetBufferMemoryRequirements(device, vertexBufferHandle, &memRequirements);

                // Find suitable memory type
                uint32_t memoryTypeIndex = res->findMemoryType(
                    memRequirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );

                // Allocate memory
                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = memoryTypeIndex;

                VkDeviceMemory vertexBufferMemory;
                if (vkAllocateMemory(device, &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) {
                    vkDestroyBuffer(device, vertexBufferHandle, nullptr);
                    Logger::get().error("Failed to allocate vertex buffer memory");
                    return false;
                }

                // Bind memory to buffer
                if (vkBindBufferMemory(device, vertexBufferHandle, vertexBufferMemory, 0) != VK_SUCCESS) {
                    vkFreeMemory(device, vertexBufferMemory, nullptr);
                    vkDestroyBuffer(device, vertexBufferHandle, nullptr);
                    Logger::get().error("Failed to bind buffer memory");
                    return false;
                }

                // Map and copy the vertex data
                void* data;
                if (vkMapMemory(device, vertexBufferMemory, 0, vertexBufferSize, 0, &data) != VK_SUCCESS) {
                    vkFreeMemory(device, vertexBufferMemory, nullptr);
                    vkDestroyBuffer(device, vertexBufferHandle, nullptr);
                    Logger::get().error("Failed to map memory");
                    return false;
                }

                memcpy(data, vertices.data(), (size_t)vertexBufferSize);
                vkUnmapMemory(device, vertexBufferMemory);

                // Store in member variables
                m_vertexBuffer = std::make_unique<VertexBufferSimple>(device, vertexBufferHandle, vertexBufferMemory, vertices.size());
                Logger::get().info("Vertex buffer created successfully with {} vertices", vertices.size());

                return true;
            }
            catch (const std::exception& e) {
                Logger::get().error("Exception in createTriangle: {}", e.what());
                return false;
            }
        }

        // Add a simple vertex buffer class
        bool createFramebuffers() {
            // Get swapchain image count
            const auto& swapchainImages = vkSwapchain.get()->imageViews();
            const auto& swapchainExtent = vkSwapchain.get()->extent();

            // Resize framebuffer container
            m_framebuffers.resize(swapchainImages.size());

            // Create a framebuffer for each swapchain image view
            for (size_t i = 0; i < swapchainImages.size(); i++) {
                // Each framebuffer needs the color and depth attachments
                std::vector<VkImageView> attachments = {
                    swapchainImages[i],        // Color attachment
                    *m_depthImageView           // Depth attachment
                };

                Framebuffer::CreateInfo framebufferInfo{};
                framebufferInfo.renderPass = *rp;
                framebufferInfo.attachments = attachments;
                framebufferInfo.width = swapchainExtent.width;
                framebufferInfo.height = swapchainExtent.height;
                framebufferInfo.layers = 1;

                try {
                    m_framebuffers[i] = std::make_unique<Framebuffer>(device, framebufferInfo);
                }
                catch (const std::exception& e) {
                    Logger::get().error("Failed to create framebuffer {}: {}", i, e.what());
                    return false;
                }
            }

            Logger::get().info("Created {} framebuffers", m_framebuffers.size());
            return true;
        }

        // Add member variable to hold framebuffers
    private:
        std::vector<std::unique_ptr<Framebuffer>> m_framebuffers;

        // Add these member variables to your VulkanBackend class
        std::unique_ptr<CommandPoolResource> m_commandPool;
        std::vector<VkCommandBuffer> m_commandBuffers;
        std::vector<SemaphoreResource> m_imageAvailableSemaphores;
        std::vector<SemaphoreResource> m_renderFinishedSemaphores;
        std::vector<FenceResource> m_inFlightFences;

        std::unique_ptr<VertexBufferSimple> m_vertexBuffer;
        std::unique_ptr<IndexBuffer> m_indexBuffer;

        // Add a command pool for transfer operations
        CommandPoolResource m_transferCommandPool;


        void createRenderPass() {
            // Create render pass configuration
            RenderPass::CreateInfo renderPassInfo{};

            // Color attachment (will be the swapchain image)
            RenderPass::Attachment colorAttachment{};
            colorAttachment.format = vkSwapchain.get()->imageFormat();  // Use swapchain format
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear on load
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // Store after use
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // No stencil for color
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // No stencil for color
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Don't care about initial layout
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // Ready for presentation
            renderPassInfo.attachments.push_back(colorAttachment);

            // Depth attachment
            RenderPass::Attachment depthAttachment{};
            depthAttachment.format = m_depthFormat;  // Use the depth format you selected
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear depth on load
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // No need to store depth
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // Depends on if stencil used
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // Depends on if stencil used
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Don't care about initial layout
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;  // Optimal for depth
            renderPassInfo.attachments.push_back(depthAttachment);

            // Add dependencies for proper synchronization

            // Dependency 1: Wait for color attachment output before writing to it
            RenderPass::SubpassDependency colorDependency{};
            colorDependency.srcSubpass = VK_SUBPASS_EXTERNAL;  // External means before/after the render pass
            colorDependency.dstSubpass = 0;  // Our only subpass
            colorDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // Wait on this stage
            colorDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // Before this stage
            colorDependency.srcAccessMask = 0;  // No access needed before
            colorDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;  // For this access
            colorDependency.dependencyFlags = 0;  // No special flags
            renderPassInfo.dependencies.push_back(colorDependency);

            // Dependency 2: Wait for early fragment tests before writing depth
            RenderPass::SubpassDependency depthDependency{};
            depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            depthDependency.dstSubpass = 0;
            depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            depthDependency.srcAccessMask = 0;
            depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depthDependency.dependencyFlags = 0;
            renderPassInfo.dependencies.push_back(depthDependency);

            // Create the render pass
            try {
                rp = std::make_unique<RenderPass>(device, renderPassInfo);
                Logger::get().info("Render pass created successfully");
            }
            catch (const std::exception& e) {
                Logger::get().error("Failed to create render pass: {}", e.what());
                throw; // Rethrow to be caught by initialize
            }
        }

        // Make sure you have a method to create depth resources
        bool createDepthResources() {
            // Find suitable depth format
            m_depthFormat = findDepthFormat();

            // Create depth image and view
            VkExtent2D extent = vkSwapchain.get()->extent();

            // Create depth image
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = extent.width;
            imageInfo.extent.height = extent.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = m_depthFormat;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            // Create the image with RAII wrapper
            m_depthImage = std::make_unique<ImageResource>(device);
            if (vkCreateImage(device, &imageInfo, nullptr, &m_depthImage->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create depth image");
                return false;
            }

            // Allocate memory
            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(device, *m_depthImage, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = res.get()->findMemoryType(memRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            m_depthImageMemory = std::make_unique<DeviceMemoryResource>(device);
            if (vkAllocateMemory(device, &allocInfo, nullptr, &m_depthImageMemory->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to allocate depth image memory");
                return false;
            }

            vkBindImageMemory(device, *m_depthImage, *m_depthImageMemory, 0);

            // Create image view
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = *m_depthImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            m_depthImageView = std::make_unique<ImageViewResource>(device);
            if (vkCreateImageView(device, &viewInfo, nullptr, &m_depthImageView->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create depth image view");
                return false;
            }

            Logger::get().info("Depth resources created successfully");
            return true;
        }

        // Helper function to find a suitable depth format
        VkFormat findDepthFormat() {
            // Try to find a supported depth format in order of preference
            std::vector<VkFormat> candidates = {
                VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D24_UNORM_S8_UINT,
                VK_FORMAT_D16_UNORM_S8_UINT,
                VK_FORMAT_D16_UNORM
            };

            for (VkFormat format : candidates) {
                VkFormatProperties props;
                vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

                // Check if optimal tiling supports depth attachment
                if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                    return format;
                }
            }

            throw std::runtime_error("Failed to find supported depth format");
        }

        void beginFrame() override {

            updateUniformBuffer();

            // Wait for previous frame to finish
            if (m_inFlightFences.size() > 0) {
                vkWaitForFences(device, 1, &m_inFlightFences[currentFrame].handle(), VK_TRUE, UINT64_MAX);
            }

            if (!vkSwapchain || !vkSwapchain.get()) {
                Logger::get().error("Swapchain is null in beginFrame()");
                return;
            }

            // Acquire next image
            uint32_t imageIndex;
            VkResult result = vkSwapchain.get()->acquireNextImage(UINT64_MAX,
                m_imageAvailableSemaphores[currentFrame],
                VK_NULL_HANDLE,
                imageIndex);

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                // Recreate swapchain and return
                int width, height;
                SDL_GetWindowSize(w, &width, &height);
                vkSwapchain.get()->recreate(width,height);
                return;
            }
            else if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to acquire swap chain image");
            }

            // Reset fence for this frame
            vkResetFences(device, 1, &m_inFlightFences[currentFrame].handle());

            // Reset the command buffer for this frame
            vkResetCommandBuffer(m_commandBuffers[currentFrame], 0);

            // Begin recording commands
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = 0;
            beginInfo.pInheritanceInfo = nullptr;

            if (vkBeginCommandBuffer(m_commandBuffers[currentFrame], &beginInfo) != VK_SUCCESS) {
                throw std::runtime_error("Failed to begin recording command buffer");
            }

            if (vkDevice.get()->capabilities().dynamicRendering) {
                // Setup rendering info for our dynamic renderer
                DynamicRenderer::RenderingInfo renderingInfo{};

                // Set render area
                renderingInfo.renderArea.offset = { 0, 0 };
                renderingInfo.renderArea.extent = vkSwapchain.get()->extent();
                renderingInfo.layerCount = 1;
                renderingInfo.viewMask = 0;  // No multiview (VR) for now

                // Configure color attachment
                DynamicRenderer::ColorAttachment colorAttachment{};
                colorAttachment.imageView = vkSwapchain.get()->imageViews()[imageIndex];
                colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.2f, 1.0f} };  // Dark blue

                // Add to the renderingInfo
                renderingInfo.colorAttachments.push_back(colorAttachment);

                // Configure depth-stencil attachment
                DynamicRenderer::DepthStencilAttachment depthStencilAttachment{};
                depthStencilAttachment.imageView = *m_depthImageView;
                depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depthStencilAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                depthStencilAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depthStencilAttachment.clearValue.depthStencil = { 1.0f, 0 };

                // Add to the renderingInfo
                renderingInfo.depthStencilAttachment = depthStencilAttachment;

                // Begin dynamic rendering
                dr.get()->begin(m_commandBuffers[currentFrame], renderingInfo);

                // Store current image index for endFrame
                m_currentImageIndex = imageIndex;
            }
            else {

                // Begin render pass
                VkRenderPassBeginInfo renderPassInfo{};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassInfo.renderPass = *rp;
                renderPassInfo.framebuffer = *m_framebuffers[imageIndex];
                renderPassInfo.renderArea.offset = { 0, 0 };
                renderPassInfo.renderArea.extent = vkSwapchain.get()->extent();

                // Clear values for each attachment
                std::array<VkClearValue, 2> clearValues{};
                clearValues[0].color = { {1.0f, 0.0f, 0.3f, 1.0f} };  // Dark blue
                clearValues[1].depthStencil = { 1.0f, 0 };

                renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
                renderPassInfo.pClearValues = clearValues.data();

                // Begin the render pass using the command buffer
                vkCmdBeginRenderPass(m_commandBuffers[currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Store the current image index for use in endFrame
                m_currentImageIndex = imageIndex;
            }

            // Bind the pipeline
            vkCmdBindPipeline(m_commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, *m_graphicsPipeline);

            // Set viewport and scissor
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;

            VkExtent2D ext = vkSwapchain.get()->extent();

            viewport.width = static_cast<float>(ext.width);
            viewport.height = static_cast<float>(ext.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(m_commandBuffers[currentFrame], 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = vkSwapchain->extent();
            vkCmdSetScissor(m_commandBuffers[currentFrame], 0, 1, &scissor);


            // Bind the descriptor set for the texture
            if (m_descriptorSet.get()) {
                try {
                    Logger::get().info("Binding descriptor set");
                    vkCmdBindDescriptorSets(
                        m_commandBuffers[currentFrame],
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        *m_pipelineLayout,
                        0, // first set
                        1, // descriptor set count
                        &m_descriptorSet->handle(),
                        0, // dynamic offset count
                        nullptr // dynamic offsets
                    );
                    Logger::get().info("Descriptor set bound successfully");
                }
                catch (const std::exception& e) {
                    Logger::get().error("Exception during descriptor set binding: {}", e.what());
                    return; // Early return to avoid crash
                }
            }
            

            // Draw triangle if buffers are available
            if (m_vertexBuffer) {
                try {
                    // Bind the vertex buffer
                    Logger::get().info("Binding vertex buffer with {} vertices", m_vertexBuffer->getVertexCount());
                    m_vertexBuffer->bind(m_commandBuffers[currentFrame]);

                    // Draw without indices
                    Logger::get().info("Drawing {} vertices", m_vertexBuffer->getVertexCount());
                    vkCmdDraw(m_commandBuffers[currentFrame], m_vertexBuffer->getVertexCount(), 1, 0, 0);
                }
                catch (const std::exception& e) {
                    Logger::get().error("Exception during triangle drawing: {}", e.what());
                }
            }
            else {
                Logger::get().warning("No vertex buffer available for drawing");
            }
        }

        void endFrame() override {
            if (vkDevice.get()->capabilities().dynamicRendering) {
                dr.get()->end(m_commandBuffers[currentFrame]);
            }
            else {
                // End the render pass
                vkCmdEndRenderPass(m_commandBuffers[currentFrame]);
            }
            // End command buffer recording
            if (vkEndCommandBuffer(m_commandBuffers[currentFrame]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to record command buffer");
            }

            // Submit command buffer
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[currentFrame] };
            VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &m_commandBuffers[currentFrame];

            VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[currentFrame] };
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;

            if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, m_inFlightFences[currentFrame].handle()) != VK_SUCCESS) {
                throw std::runtime_error("Failed to submit draw command buffer");
            }

            // Present the image
            VkResult result = vkSwapchain.get()->present(m_currentImageIndex, m_renderFinishedSemaphores[currentFrame]);

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                // Recreate swapchain
                int width, height;
                SDL_GetWindowSize(w, &width, &height);
                vkSwapchain.get()->recreate(width, height);
            }
            else if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to present swap chain image");
            }

            // Move to next frame
            currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        }

        // Add member variable to store current image index
private:
    uint32_t m_currentImageIndex = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;  // Double buffering

        // Also add member variables to hold depth resources
    private:
        std::unique_ptr<RenderPass> rp;

        // Depth resources
        std::unique_ptr<ImageResource> m_depthImage;
        std::unique_ptr<DeviceMemoryResource> m_depthImageMemory;
        std::unique_ptr<ImageViewResource> m_depthImageView;
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

        bool initialize(SDL_Window* window) override {
        

            w = window;

            createInstance();
            createDeviceAndSwapChain();

            createCommandPool();
            createCommandBuffers();

            createDepthResources();
            createUniformBuffer();
            createLightBuffer();
            if (vkDevice.get()->capabilities().dynamicRendering) {
                dr = std::make_unique<DynamicRenderer>();
                Logger::get().info("Dynamic renderer created.");
            } else {
                createRenderPass();
                createFramebuffers();
            }

            sm = std::make_unique<ShaderManager>(vkDevice.get()->device());
            createTriangle();
			createTestTexture();            
            createMaterialUBO();

			createPBRDescriptorSetLayout();
            createDescriptorSet();

            createGraphicsPipeline();
            createSyncObjects();

            return true;
        };
        void shutdown() override {};


        TextureHandle createTexture(const TextureDesc& desc) {
            // Implementation of texture creation with Vulkan
            // This would include:
            // 1. Create VkImage
            // 2. Allocate and bind memory
            // 3. Create VkImageView
            // 4. Create VkSampler if needed
            // 5. Return a handle to the texture

            // Simplified implementation for example
            VulkanTexture* texture = new VulkanTexture(device);

            // Image creation
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = desc.width;
            imageInfo.extent.height = desc.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = desc.mipLevels;
            imageInfo.arrayLayers = 1;
            imageInfo.format = convertFormat(desc.format);
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            if (vkCreateImage(device, &imageInfo, nullptr, &texture->image.handle()) != VK_SUCCESS) {
                delete texture;
                return {};
            }
        }
        BufferHandle createBuffer(const BufferDesc& desc);
        ShaderHandle createShader(const ShaderDesc& desc);

        // Vulkan-specific methods
        VkDevice getDevice() const { return device; }
        VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
        VkQueue getGraphicsQueue() const { return graphicsQueue; }

    private:
        // Vulkan instance and devices
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;

        // Queues
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;

        // Swap chain
        VkSwapchainKHR swapChain = VK_NULL_HANDLE;
        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;

        // Command submission
        VkCommandPool commandPool = VK_NULL_HANDLE;

        // Synchronization
        size_t currentFrame = 0;

        bool get_surface_capabilities_2 = false;
        bool vulkan_1_4_available = false;

        bool debug_utils = false;
        bool memory_report = false;

        bool enableValidation = false;

        uint32_t m_gfxQueueFamilyIndex = 0;

        // Format information
        VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;


        // Properties
        VkPhysicalDeviceMemoryProperties m_memoryProperties{};
        VkPhysicalDeviceProperties m_deviceProperties{};
        VkPhysicalDeviceFeatures m_deviceFeatures{};

        // Window reference
        SDL_Window* window = nullptr;

        // Validation layers
        bool enableValidationLayers = true;
        const std::vector<const char*> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };

        // Device extensions
        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        bool createInstance() {
            unsigned int sdl_extension_count;
            VkResult	 err;
            uint32_t	 i;

            err = volkInitialize();

            if (err != VK_SUCCESS) {
                return false;
            }

            VkApplicationInfo appInfo{};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = "Tremor";
            appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
            appInfo.pEngineName = "Tremor Engine";
            appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
            appInfo.apiVersion = VK_API_VERSION_1_4;  // Targeting Vulkan 1.4

            // Get SDL Vulkan extensions
            unsigned int sdlExtensionCount = 0;
            if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, nullptr)) {
                Logger::get().error( "SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
                return false;
            }

            // Allocate space for extensions (SDL + additional ones)
            auto instanceExtensions = tremor::mem::ScopedAlloc<const char*>(sdlExtensionCount + 5);
            if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, instanceExtensions.get())) {
                Logger::get().error("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
                return false;
            }

            // Track our added extensions
            uint32_t additionalExtensionCount = 0;

            // Query available extensions
            uint32_t availableExtensionCount = 0;
            err = vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, nullptr);
            if (err != VK_SUCCESS) {
                Logger::get().error("Failed to query instance extension count");
                return false;
            }

            // Check for optional extensions
            bool hasSurfaceCapabilities2 = false;
            bool hasDebugUtils = false;

            if (availableExtensionCount > 0) {
                auto extensionProps = tremor::mem::ScopedAlloc<VkExtensionProperties>(availableExtensionCount);

                err = vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, extensionProps.get());
                if (err != VK_SUCCESS) {
                    Logger::get().error("Failed to enumerate instance extensions" );
                    return false;
                }

                for (uint32_t i = 0; i < availableExtensionCount; ++i) {
                    const char* extName = extensionProps[i].extensionName;

                    if (strcmp(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, extName) == 0) {
                        hasSurfaceCapabilities2 = true;
                    }

#if _DEBUG
                    if (strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, extName) == 0) {
                        hasDebugUtils = true;
                    }
#endif
                }
            }

            // Add optional extensions
            if (hasSurfaceCapabilities2) {
                instanceExtensions[sdlExtensionCount + additionalExtensionCount++] =
                    VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
            }

#if _DEBUG
            if (hasDebugUtils) {
                instanceExtensions[sdlExtensionCount + additionalExtensionCount++] =
                    VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            }
#endif

            // Setup validation layers for debug builds
            std::vector<const char*> validationLayers;

#if _DEBUG
            // Check for the validation layer
            uint32_t layerCount;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
            std::vector<VkLayerProperties> availableLayers(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

            // Print available layers
            Logger::get().info("Available Vulkan layers:");
            for (const auto& layer : availableLayers) {
                Logger::get().info("  {}", layer.layerName);

                // Check if it's the validation layer
                if (strcmp("VK_LAYER_KHRONOS_validation", layer.layerName) == 0) {
                    enableValidation = true;
                }
            }

            if (!enableValidation) {
                Logger::get().warning("Validation layer not found. Continuing without validation.");
                Logger::get().warning("To enable validation, use vkconfig from the Vulkan SDK.");
            }
            else {
                Logger::get().info("Validation layer found and enabled.");
            }
#else
            enableValidation = false;
#endif

            // Setup debug messenger creation info
            VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
#if _DEBUG
            if (enableValidation && hasDebugUtils) {
                debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                debugCreateInfo.messageSeverity =
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                debugCreateInfo.messageType =
                    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                debugCreateInfo.pfnUserCallback = debugCallback;
                debugCreateInfo.pUserData = nullptr;
            }
#endif

            // Create the Vulkan instance
            VkInstanceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = sdlExtensionCount + additionalExtensionCount;
            createInfo.ppEnabledExtensionNames = instanceExtensions.get();

#if _DEBUG
            if (enableValidation) {
                createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
                createInfo.ppEnabledLayerNames = validationLayers.data();
                createInfo.pNext = &debugCreateInfo;
            }
            else {
                createInfo.enabledLayerCount = 0;
                createInfo.pNext = nullptr;
            }
#else
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
#endif

            // Create the instance
            VkInstance inst;
            err = vkCreateInstance(&createInfo, nullptr, &inst);
            if (err != VK_SUCCESS) {
                Logger::get().error("Failed to create Vulkan instance: {}", (int)err);
                return false;
            }

            instance.reset(inst);

            volkLoadInstance(instance);

            Logger::get().info("Vulkan instance created successfully");

            // Load instance-level functions
            volkLoadInstance(instance);

            // Setup debug messenger if enabled
#if _DEBUG
            if (enableValidation && hasDebugUtils) {
                err = vkCreateDebugUtilsMessengerEXT(
                    instance,
                    &debugCreateInfo,
                    nullptr,
                    &debugMessenger
                );

                if (err != VK_SUCCESS) {
                    Logger::get().error("Failed to set up debug messenger: {}", (int)err);
                    // Continue anyway, this is not fatal
                }
            }
#endif
            VkSurfaceKHR surf;
            if (!SDL_Vulkan_CreateSurface(w, instance, &surf)) {
                Logger::get().error("Failed to create Vulkan surface : {}", (int)err);
                return false;
            }

			surface = SurfaceResource(instance,surf);

            Logger::get().info("Vulkan surface created successfully");

            return true;
        }

#if _DEBUG
        // Debug callback function
        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData) {

            // Ignore some verbose messages
            if (strstr(pCallbackData->pMessage, "UNASSIGNED-CoreValidation-DrawState-ClearCmdBeforeDraw") != nullptr) {
                return VK_FALSE;
            }

            if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
                std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
            }

            return VK_FALSE;
        }
#endif
        

        bool createDeviceAndSwapChain() {

            VulkanDevice::DevicePreferences prefs;
            prefs.preferDiscreteGPU = true;
            prefs.requireMeshShaders = true;  // Set based on your requirements
            prefs.requireRayQuery = true;     // Set based on your requirements
            prefs.requireSparseBinding = true; // Set based on your requirements


            // Create device
            vkDevice = std::make_unique<VulkanDevice>(instance, surface, prefs);

            // Create swap chain
            SwapChain::CreateInfo swapChainInfo;
            int width, height;
            SDL_GetWindowSize(w, &width, &height);
            swapChainInfo.width = static_cast<uint32_t>(width);
            swapChainInfo.height = static_cast<uint32_t>(height);

            vkSwapchain = std::make_unique<SwapChain>(*vkDevice, surface, swapChainInfo);

            // Cache common device properties for convenience
            physicalDevice = vkDevice->physicalDevice();
            device = vkDevice->device();
            graphicsQueue = vkDevice->graphicsQueue();
            m_colorFormat = vkDevice->colorFormat();
            m_depthFormat = vkDevice->depthFormat();

            res = std::make_unique<VulkanResourceManager>(device, physicalDevice);

            return true;
        }

        std::unique_ptr<DynamicRenderer> dr;

        std::unique_ptr<ShaderManager> sm;

        bool createTestTexture() {
            try {
                // Create a simple 2x2 checkerboard texture
                const uint32_t size = 256;
                std::vector<uint8_t> pixels(size * size * 4);

                // Fill with a checkerboard pattern
                for (uint32_t y = 0; y < size; y++) {
                    for (uint32_t x = 0; x < size; x++) {
                        uint8_t color = ((x / 32 + y / 32) % 2) ? 255 : 0;
                        pixels[(y * size + x) * 4 + 0] = color;     // R
                        pixels[(y * size + x) * 4 + 1] = 0;     // G
                        pixels[(y * size + x) * 4 + 2] = color;     // B
                        pixels[(y * size + x) * 4 + 3] = 255;       // A
                    }
                }

                // Create texture image
                VkDeviceSize imageSize = size * size * 4;

                // Create staging buffer
                VkBuffer stagingBuffer;
                VkDeviceMemory stagingBufferMemory;

                // Create buffer
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = imageSize;
                bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
                    Logger::get().error("Failed to create staging buffer for texture");
                    return false;
                }

                // Get memory requirements
                VkMemoryRequirements memRequirements;
                vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);

                // Allocate memory
                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = res->findMemoryType(
                    memRequirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                );

                if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
                    vkDestroyBuffer(device, stagingBuffer, nullptr);
                    Logger::get().error("Failed to allocate staging buffer memory");
                    return false;
                }

                // Bind memory
                vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);

                // Copy data to staging buffer
                void* data;
                vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
                memcpy(data, pixels.data(), imageSize);
                vkUnmapMemory(device, stagingBufferMemory);

                // Create the texture image
                VkImageCreateInfo imageInfo{};
                imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.extent.width = size;
                imageInfo.extent.height = size;
                imageInfo.extent.depth = 1;
                imageInfo.mipLevels = 1;
                imageInfo.arrayLayers = 1;
                imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

                // Create image
                m_textureImage = std::make_unique<ImageResource>(device);
                if (vkCreateImage(device, &imageInfo, nullptr, &m_textureImage->handle()) != VK_SUCCESS) {
                    Logger::get().error("Failed to create texture image");
                    return false;
                }

                // Allocate memory for the image
                vkGetImageMemoryRequirements(device, *m_textureImage, &memRequirements);

                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = res->findMemoryType(
                    memRequirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                );

                m_textureImageMemory = std::make_unique<DeviceMemoryResource>(device);
                if (vkAllocateMemory(device, &allocInfo, nullptr, &m_textureImageMemory->handle()) != VK_SUCCESS) {
                    Logger::get().error("Failed to allocate texture image memory");
                    return false;
                }

                // Bind memory to image
                vkBindImageMemory(device, *m_textureImage, *m_textureImageMemory, 0);

                // Transition image layout and copy data from staging buffer
                VkCommandBuffer commandBuffer = beginSingleTimeCommands();

                // Transition to transfer destination layout
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = *m_textureImage;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );

                // Copy data from buffer to image
                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = { 0, 0, 0 };
                region.imageExtent = { size, size, 1 };

                vkCmdCopyBufferToImage(
                    commandBuffer,
                    stagingBuffer,
                    *m_textureImage,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &region
                );

                // Transition to shader read layout
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );

                endSingleTimeCommands(commandBuffer);

                // Create image view
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = *m_textureImage;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.baseMipLevel = 0;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = 1;

                m_missingTextureImageView = std::make_unique<ImageViewResource>(device);
                if (vkCreateImageView(device, &viewInfo, nullptr, &m_missingTextureImageView->handle()) != VK_SUCCESS) {
                    Logger::get().error("Failed to create texture image view");
                    return false;
                }

                // Create sampler
                VkSamplerCreateInfo samplerInfo{};
                samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerInfo.magFilter = VK_FILTER_LINEAR;
                samplerInfo.minFilter = VK_FILTER_LINEAR;
                samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerInfo.anisotropyEnable = VK_TRUE;
                samplerInfo.maxAnisotropy = 16.0f;
                samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
                samplerInfo.unnormalizedCoordinates = VK_FALSE;
                samplerInfo.compareEnable = VK_FALSE;
                samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
                samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                samplerInfo.mipLodBias = 0.0f;
                samplerInfo.minLod = 0.0f;
                samplerInfo.maxLod = 0.0f;

                m_textureSampler = std::make_unique<SamplerResource>(device);
                if (vkCreateSampler(device, &samplerInfo, nullptr, &m_textureSampler->handle()) != VK_SUCCESS) {
                    Logger::get().error("Failed to create texture sampler");
                    return false;
                }

                // Clean up staging buffer
                vkDestroyBuffer(device, stagingBuffer, nullptr);
                vkFreeMemory(device, stagingBufferMemory, nullptr);

                Logger::get().info("Texture created successfully");
                return true;
            }
            catch (const std::exception& e) {
                Logger::get().error("Exception in createTestTexture: {}", e.what());
                return false;
            }
        }

        // Helper command for single-time command submission
        VkCommandBuffer beginSingleTimeCommands() {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = *m_commandPool;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer;
            vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(commandBuffer, &beginInfo);

            return commandBuffer;
        }

        void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
            vkEndCommandBuffer(commandBuffer);

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(graphicsQueue);

            vkFreeCommandBuffers(device, *m_commandPool, 1, &commandBuffer);
        }

        bool createPBRDescriptorSetLayout() {
            // Just two bindings - UBO and texture
            std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
                // Binding 0: UBO for vertex shader
                VkDescriptorSetLayoutBinding{
                    0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr
                },
                // Binding 1: Texture sampler
                VkDescriptorSetLayoutBinding{
                    1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
                }
            };

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            m_descriptorSetLayout = std::make_unique<DescriptorSetLayoutResource>(device);
            if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create descriptor set layout");
                return false;
            }

            // Create the pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout->handle();

            VkPipelineLayout pipelineLayout;
            if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
                Logger::get().error("Failed to create pipeline layout");
                return false;
            }
            m_pipelineLayout = std::make_unique<PipelineLayoutResource>(device, pipelineLayout);

            return true;
        }

        bool createDescriptorSet() {
            // Create descriptor pool with just what we need
            std::array<VkDescriptorPoolSize, 2> poolSizes = {
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}
            };

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = 1;

            m_descriptorPool = std::make_unique<DescriptorPoolResource>(device);
            if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool->handle()) != VK_SUCCESS) {
                Logger::get().error("Failed to create descriptor pool");
                return false;
            }

            // Allocate descriptor set
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = *m_descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &m_descriptorSetLayout->handle();

            VkDescriptorSet descriptorSetHandle = VK_NULL_HANDLE;
            if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSetHandle) != VK_SUCCESS) {
                Logger::get().error("Failed to allocate descriptor set");
                return false;
            }

            m_descriptorSet = std::make_unique<DescriptorSetResource>(device, descriptorSetHandle);

            // Update the descriptor set
            // 1. UBO descriptor
            VkDescriptorBufferInfo uboInfo{};
            uboInfo.buffer = m_uniformBuffer->getBuffer();
            uboInfo.offset = 0;
            uboInfo.range = sizeof(UniformBufferObject);

            // 2. Texture descriptor
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = *m_missingTextureImageView;
            imageInfo.sampler = *m_textureSampler;

            // Prepare descriptor writes
            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

            // UBO at binding 0
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = *m_descriptorSet;
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &uboInfo;

            // Texture at binding 1
            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = *m_descriptorSet;
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfo;

            // Update all descriptors at once
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

            Logger::get().info("Descriptor set created and updated successfully");
            return true;
        }
        bool createGraphicsPipeline() {
            // 1. Load shaders
            // For now, let's create simple vertex and fragment shaders
            auto vertShader = sm.get()->loadShader("shaders/pbr.vert");

            auto fragShader = sm.get()->loadShader("shaders/pbr.frag");


            if (!vertShader || !fragShader) {
                Logger::get().error("Failed to load shader modules");
                return false;
            }

            // 2. Create shader stages
            std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
                vertShader->createShaderStageInfo(),
                fragShader->createShaderStageInfo()
            };


            // 3. Create pipeline state
            PipelineState pipelineState;

            // 4. Configure vertex input
            // For now, let's assume a simple vertex with position and color

            auto bindingDescription = SimpleVertex::getBindingDescription();
            auto attributeDescriptions = SimpleVertex::getAttributeDescriptions();

            pipelineState.vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            pipelineState.vertexInputState.vertexBindingDescriptionCount = 1;
            pipelineState.vertexInputState.pVertexBindingDescriptions = &bindingDescription;
            pipelineState.vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
            pipelineState.vertexInputState.pVertexAttributeDescriptions = attributeDescriptions.data();


            // 5. Input assembly
            pipelineState.inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            pipelineState.inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            pipelineState.inputAssemblyState.primitiveRestartEnable = VK_FALSE;

            // 6. Viewport and scissor - using dynamic state
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(vkSwapchain->extent().width);
            viewport.height = static_cast<float>(vkSwapchain->extent().height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = vkSwapchain->extent();

            pipelineState.viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            pipelineState.viewportState.viewportCount = 1;
            pipelineState.viewportState.pViewports = &viewport;
            pipelineState.viewportState.scissorCount = 1;
            pipelineState.viewportState.pScissors = &scissor;

            // 7. Rasterization
            pipelineState.rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            pipelineState.rasterizationState.depthClampEnable = VK_FALSE;
            pipelineState.rasterizationState.rasterizerDiscardEnable = VK_FALSE;
            pipelineState.rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
            pipelineState.rasterizationState.lineWidth = 1.0f;
            pipelineState.rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
            pipelineState.rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            pipelineState.rasterizationState.depthBiasEnable = VK_FALSE;

            // 8. Multisampling
            pipelineState.multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            pipelineState.multisampleState.sampleShadingEnable = VK_FALSE;
            pipelineState.multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            // 9. Depth/stencil testing
            pipelineState.depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            pipelineState.depthStencilState.depthTestEnable = VK_TRUE;
            pipelineState.depthStencilState.depthWriteEnable = VK_TRUE;
            pipelineState.depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
            pipelineState.depthStencilState.depthBoundsTestEnable = VK_FALSE;
            pipelineState.depthStencilState.stencilTestEnable = VK_FALSE;

            // 10. Color blending
            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;


            pipelineState.colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            pipelineState.colorBlendState.logicOpEnable = VK_FALSE;
            pipelineState.colorBlendState.attachmentCount = 1;
            pipelineState.colorBlendState.pAttachments = &colorBlendAttachment;

            // 11. Dynamic state
            std::vector<VkDynamicState> dynamicStates = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };

            pipelineState.dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            pipelineState.dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
            pipelineState.dynamicState.pDynamicStates = dynamicStates.data();

            // 12. Create pipeline layout
            // For simplicity, we'll use an empty layout for now

            // Store pipeline layout in RAII wrapper
            //m_pipelineLayout = std::make_unique<PipelineLayoutResource>(device, pipelineLayout);

            // 13. Create the actual pipeline
            if (vkDevice->capabilities().dynamicRendering) {
                // For dynamic rendering, we need to use VkPipelineRenderingCreateInfoKHR
                VkPipelineRenderingCreateInfoKHR renderingInfo{};
                renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
                renderingInfo.colorAttachmentCount = 1;
                VkFormat colorFormat = vkSwapchain->imageFormat();
                renderingInfo.pColorAttachmentFormats = &colorFormat;
                renderingInfo.depthAttachmentFormat = m_depthFormat;

                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.pNext = &renderingInfo; // Link dynamic rendering info
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = shaderStages.data();
                pipelineInfo.pVertexInputState = &pipelineState.vertexInputState;
                pipelineInfo.pInputAssemblyState = &pipelineState.inputAssemblyState;
                pipelineInfo.pViewportState = &pipelineState.viewportState;
                pipelineInfo.pRasterizationState = &pipelineState.rasterizationState;
                pipelineInfo.pMultisampleState = &pipelineState.multisampleState;
                pipelineInfo.pDepthStencilState = &pipelineState.depthStencilState;
                pipelineInfo.pColorBlendState = &pipelineState.colorBlendState;
                pipelineInfo.pDynamicState = &pipelineState.dynamicState;
                pipelineInfo.layout = *m_pipelineLayout;
                pipelineInfo.renderPass = VK_NULL_HANDLE; // Not used with dynamic rendering
                pipelineInfo.subpass = 0;
                pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

                VkPipeline graphicsPipeline;
                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
                    Logger::get().error("Failed to create graphics pipeline with dynamic rendering");
                    return false;
                }

                // Store in RAII wrapper
                m_graphicsPipeline = std::make_unique<PipelineResource>(device, graphicsPipeline);
            }
            else {
                // Traditional render pass approach
                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = shaderStages.data();
                pipelineInfo.pVertexInputState = &pipelineState.vertexInputState;
                pipelineInfo.pInputAssemblyState = &pipelineState.inputAssemblyState;
                pipelineInfo.pViewportState = &pipelineState.viewportState;
                pipelineInfo.pRasterizationState = &pipelineState.rasterizationState;
                pipelineInfo.pMultisampleState = &pipelineState.multisampleState;
                pipelineInfo.pDepthStencilState = &pipelineState.depthStencilState;
                pipelineInfo.pColorBlendState = &pipelineState.colorBlendState;
                pipelineInfo.pDynamicState = &pipelineState.dynamicState;
                pipelineInfo.layout = *m_pipelineLayout;
                pipelineInfo.renderPass = *rp; // Use the traditional render pass
                pipelineInfo.subpass = 0;
                pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

                VkPipeline graphicsPipeline;
                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
                    Logger::get().error("Failed to create graphics pipeline with render pass");
                    return false;
                }

                // Store in RAII wrapper
                m_graphicsPipeline = std::make_unique<PipelineResource>(device, graphicsPipeline);
            }

            Logger::get().info("Graphics pipeline created successfully");


            return true;
        }

        // Texture resources
        std::unique_ptr<ImageResource> m_textureImage;
        std::unique_ptr<DeviceMemoryResource> m_textureImageMemory;
        std::unique_ptr<ImageViewResource> m_missingTextureImageView;
        std::unique_ptr<SamplerResource> m_textureSampler;

        // Descriptor resources
        std::unique_ptr<DescriptorSetLayoutResource> m_descriptorSetLayout;
        std::unique_ptr<DescriptorPoolResource> m_descriptorPool;
        std::unique_ptr<DescriptorSetResource> m_descriptorSet;

        // Helper method to load shader modules
        VkShaderModule loadShader(const std::string& filename) {
            // Read the file
            std::ifstream file(filename, std::ios::ate | std::ios::binary);

            if (!file.is_open()) {
                Logger::get().error("Failed to open shader file: {}", filename);
                return VK_NULL_HANDLE;
            }

            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> shaderCode(fileSize);

            file.seekg(0);
            file.read(shaderCode.data(), fileSize);
            file.close();

            // Create shader module
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = fileSize;
            createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

            VkShaderModule shaderModule;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                Logger::get().error("Failed to create shader module for: {}", filename);
                return VK_NULL_HANDLE;
            }

            return shaderModule;
        }

        // Physical device selection
        bool isDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

        // Swap chain configuration
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

        // Submission/presentation
        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        // Add these member variables
        std::unique_ptr<PipelineLayoutResource> m_pipelineLayout;
        std::unique_ptr<PipelineResource> m_graphicsPipeline;

    };

}