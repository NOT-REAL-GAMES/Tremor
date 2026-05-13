#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Important for Vulkan depth range

#include "tremor_core.h"
#include "tremor_graphics_platform.h"
#include "RenderBackendBase.h"
#include "vk_rhi.h"
#include "vk_renderer_support.h"
#include "gfx_resource_types.h"
#include "gfx_resource_handles.h"
#include "vk_resource_wrappers.h"
#include "mem.h"
#include <shaderc/shaderc.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>

#include <memory>
#include <optional>
#include <unordered_set>

#include "gfx.h"
#include "include/quan.h"
#include "handle.h"
#include "include/taffy.h"
#include "include/tools.h"
#include "Source/Runtime/TremorRenderer/taffy_mesh.h"
#include "Source/Runtime/TremorRenderer/taffy_integration.h"
#include "include/asset.h"

namespace tremor::gfx {

    // Forward declarations
    class SDFTextRenderer;
    class UIRenderer;
    class SequencerUI;
    class VulkanBackendControls;
    class VulkanEditorBridge;
    class VulkanOverlayBridge;
    class VulkanUiBridge;

    class Buffer {
    public:
        Buffer() = default;
        Buffer(VkDevice device, VkPhysicalDevice physicalDevice,
            VkDeviceSize size, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags memoryProps);

        // Method declarations only - implementations go to vk.cpp
        void update(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

        // Simple getters can stay inline
        VkBuffer getBuffer() const { return m_buffer; }
        VkDeviceSize getSize() const { return m_size; }
        VkDeviceMemory getMemory() const { return m_memory; }

    private:
        VkDevice m_device = VK_NULL_HANDLE;
        BufferResource m_buffer;
        DeviceMemoryResource m_memory;
        VkDeviceSize m_size = 0;

        // Helper function declaration
        uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
            uint32_t typeFilter, VkMemoryPropertyFlags properties);
    };


    struct PBRMaterialUBO {
        // Basic properties
        alignas(16) float baseColor[4];   // vec4 in shader
        float metallic;        // float in shader
        float roughness;       // float in shader
        float ao;              // float in shader
        alignas(16) float emissive[3];    // vec3 in shader (padded to 16 bytes)

        // Texture availability flags
        int hasAlbedoMap;
        int hasNormalMap;
        int hasMetallicRoughnessMap;
        int hasAoMap;
        int hasEmissiveMap;
    };

    class ShaderManager;
    class RenderPass;
    class VertexBufferSimple;
    class IndexBuffer;
    class Framebuffer;
    class SwapChain;
    class VulkanResourceManager;
    class DynamicRenderer;
    class CommandPoolResource;
    class DescriptorBuilder;
    class DescriptorLayoutCache;
    class DescriptorAllocator;
    class DescriptorWriter;




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
            uint32_t colorAttachmentIndex = 0;
            std::optional<uint32_t> depthAttachmentIndex = 1;
            std::optional<uint32_t> resolveAttachmentIndex = std::nullopt;
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

        VkBuffer getBuffer() const { return m_buffer; }

    private:
        VkDevice m_device;
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_memory = VK_NULL_HANDLE;
        size_t m_vertexCount = 0;
    };


    class MeshRegistry {
    public:
        struct alignas(16) MeshData {
            uint32_t vertexCount;
            uint32_t indexCount;
            VkBuffer vertexBuffer;
            VkBuffer indexBuffer;
            VkIndexType indexType;
            AABBF bounds;
        };

        // Register a mesh and get its ID
        uint32_t registerMesh(const MeshData& mesh, const std::string& name = "") {
            uint32_t id = static_cast<uint32_t>(m_meshes.size());
            m_meshes.push_back(mesh);

            if (!name.empty()) {
                m_meshNames[name] = id;
            }

            return id;
        }

        // Register your existing vertex buffer
        uint32_t registerMesh(VertexBufferSimple* vertexBuffer, const std::string& name = "") {
            MeshData mesh;
            mesh.vertexCount = vertexBuffer->getVertexCount();
            mesh.indexCount = 0; // No indices for this simple mesh
            mesh.vertexBuffer = vertexBuffer->getBuffer(); // Assuming you add a getter
            mesh.indexBuffer = VK_NULL_HANDLE;
            mesh.indexType = VK_INDEX_TYPE_UINT32; // Default

            // Calculate bounds from vertices or use fixed bounds for the cube
            mesh.bounds = {
                glm::vec3(-0.5f, -0.5f, -0.5f),
                glm::vec3(0.5f, 0.5f, 0.5f)
            };

            return registerMesh(mesh, name);
        }

        // Get a mesh by ID
        const MeshData* getMesh(uint32_t id) const {
            if (id < m_meshes.size()) {
                return &m_meshes[id];
            }
            return nullptr;
        }

        // Get a mesh by name
        const MeshData* getMesh(const std::string& name) const {
            auto it = m_meshNames.find(name);
            if (it != m_meshNames.end()) {
                return &m_meshes[it->second];
            }
            return nullptr;
        }

        // Get a mesh ID by name
        uint32_t getMeshID(const std::string& name) const {
            auto it = m_meshNames.find(name);
            if (it != m_meshNames.end()) {
                return it->second;
            }
            return UINT32_MAX; // Invalid ID
        }

    private:
        std::vector<MeshData> m_meshes;
        std::unordered_map<std::string, uint32_t> m_meshNames;
    };


    // Inline helper for attribute descriptions
    inline std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributes(4);

        attributes[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position) };
        attributes[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal) };
        attributes[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, color) };
        attributes[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, texCoord) };
        //attributes[3] = { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, tangent) };

        return attributes;
    }

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



    // Helper function - keep inline
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
        return ShaderType::Vertex;
    }

    class ShaderCompiler {
    public:
        struct CompileOptions {
            bool optimize = true;
            bool generateDebugInfo = false;
            std::vector<std::string> includePaths;
            std::unordered_map<std::string, std::string> macros;
        };

        ShaderCompiler();

        // Method declarations - implementations go to vk.cpp
        std::vector<uint32_t> compileToSpv(const std::string& source, ShaderType type,
            const std::string& filename, int flags = 0);
        std::vector<uint32_t> compileFileToSpv(const std::string& filename, ShaderType type,
            const CompileOptions& options);

    private:


        std::unique_ptr<shaderc::Compiler> m_compiler;
        std::unique_ptr<shaderc::CompileOptions> m_options;

        shaderc_shader_kind getShaderKind(ShaderType type);
    };

    enum class ShaderStageType {
        Vertex,
        Fragment,
        Compute,
        Geometry,
        TessControl,
        TessEvaluation,
        Task,
        Mesh
        // Add others as needed
    };

    class ShaderReflection {
    public:
        enum class ShaderStageType {
            Vertex, Fragment, Compute, Geometry, TessControl, TessEvaluation, Task, Mesh
        };

        struct ResourceBinding {
            uint32_t set;
            uint32_t binding;
            VkDescriptorType descriptorType;
            uint32_t count;
            VkShaderStageFlags stageFlags;
            std::string name;
        };

        struct UBOMember {
            std::string name;
            uint32_t offset;
            uint32_t size;
            struct TypeInfo {
                spirv_cross::SPIRType::BaseType baseType;
                uint32_t vecSize = 1;
                uint32_t columns = 1;
                std::vector<uint32_t> arrayDims;
            } typeInfo;
        };

        struct UniformBuffer {
            uint32_t set;
            uint32_t binding;
            uint32_t size;
            VkShaderStageFlags stageFlags;
            std::string name;
            uint32_t typeId;
            uint32_t baseTypeId;
            std::vector<UBOMember> members;
        };

        struct PushConstantRange {
            VkShaderStageFlags stageFlags;
            uint32_t offset;
            uint32_t size;
        };

        struct VertexAttribute {
            uint32_t location;
            std::string name;
            VkFormat format;
        };


        ShaderReflection() = default;

        // Method declarations - implementations go to vk.cpp
        void reflect(const std::vector<uint32_t>& spirvCode, VkShaderStageFlags stageFlags);
        void merge(const ShaderReflection& other);
        std::vector<UBOMember> getUBOMembers(const UniformBuffer& ubo) const;
        std::unique_ptr<DescriptorSetLayoutResource> createDescriptorSetLayout(VkDevice device, uint32_t setNumber) const;
        std::unique_ptr<PipelineLayoutResource> createPipelineLayout(VkDevice device) const;
        std::unique_ptr<DescriptorPoolResource> createDescriptorPool(VkDevice device, uint32_t maxSets = 100) const;
        VkPipelineVertexInputStateCreateInfo createVertexInputState() const;

        // Getters - can stay inline
        const std::vector<ResourceBinding>& getResourceBindings() const { return m_resourceBindings; }
        const std::vector<UniformBuffer>& getUniformBuffers() const { return m_uniformBuffers; }
        const std::vector<PushConstantRange>& getPushConstantRanges() const { return m_pushConstantRanges; }
        const std::vector<VertexAttribute>& getVertexAttributes() const { return m_vertexAttributes; }
        ShaderStageType getStageType(VkShaderStageFlags flags);

    private:
        std::unordered_map<VkShaderStageFlags, std::vector<uint32_t>> m_spirvCode;
        std::vector<ResourceBinding> m_resourceBindings;
        std::vector<UniformBuffer> m_uniformBuffers;
        std::vector<PushConstantRange> m_pushConstantRanges;
        std::vector<VertexAttribute> m_vertexAttributes;

        // Mutable for vertex input state
        mutable VkVertexInputBindingDescription m_bindingDescription;
        mutable std::vector<VkVertexInputAttributeDescription> m_attributeDescriptions;
        mutable VkPipelineVertexInputStateCreateInfo m_vertexInputState;

        // Helper methods
        VkFormat getFormatFromType(const spirv_cross::SPIRType& type) const;
        uint32_t getFormatSize(VkFormat format) const;
    };

    class ShaderModule {
    public:
        ShaderModule() = default;
        ShaderModule(VkDevice device, VkShaderModule rawModule, ShaderType type = ShaderType::Vertex);
        ~ShaderModule() = default;

        // Static factory methods - declarations only
        static std::unique_ptr<ShaderModule> loadFromFile(VkDevice device, const std::string& filename,
            ShaderType type = ShaderType::Vertex, const std::string& entryPoint = "main");
        static std::unique_ptr<ShaderModule> compileFromSource(VkDevice device, const std::string& source,
            ShaderType type, const std::string& filename = "unnamed_shader",
            const std::string& entryPoint = "main", const ShaderCompiler::CompileOptions& options = ShaderCompiler::CompileOptions{});
        static std::unique_ptr<ShaderModule> compileFromFile(VkDevice device, const std::string& filename,
            const std::string& entryPoint = "main", int flags = 0);

        // Method declarations
        VkPipelineShaderStageCreateInfo createShaderStageInfo() const;
        bool isValid() const;
        explicit operator bool() const;

        // Simple getters - can stay inline
        VkShaderModule getHandle() const {
            return m_module ? m_module->handle() : VK_NULL_HANDLE;
        }
        ShaderType getType() const { return m_type; }
        const std::string& getEntryPoint() const { return m_entryPoint; }
        const std::string& getFilename() const { return m_filename; }
        const std::vector<uint32_t>& getSPIRVCode() const { return m_spirvCode; }
        const ShaderReflection* getReflection() const { return m_reflection.get(); }
        ShaderReflection* getReflection() { return m_reflection.get(); }

    private:
        VkDevice m_device = VK_NULL_HANDLE;
        std::unique_ptr<ShaderModuleResource> m_module;
        ShaderType m_type = ShaderType::Vertex;
        std::string m_entryPoint = "main";
        std::string m_filename;
        std::vector<uint32_t> m_spirvCode;
        std::unique_ptr<ShaderReflection> m_reflection;

        VkShaderStageFlagBits getShaderStageFlagBits() const;
    };


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


    // Forward declarations
    class Camera;
    class RenderContext;

    // UBO for enhanced cluster rendering
    struct alignas(16) EnhancedClusterUBO {
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
        glm::mat4 invViewMatrix;
        glm::mat4 invProjMatrix;
        glm::vec4 cameraPos;
        glm::uvec4 clusterDimensions;
        glm::vec4 zPlanes;
        glm::vec4 screenSize;
        uint32_t numLights;
        uint32_t numObjects;
        uint32_t numClusters;
        uint32_t frameNumber;
        float time;
        float deltaTime;
        uint32_t flags;
        uint32_t _padding;
    };

    class VulkanBackend : public RenderBackend {
    public:
        // Constructor/Destructor
        VulkanBackend();
        ~VulkanBackend() override;

        // RenderBackend interface implementation
        bool initialize(SDL_Window* window) override;
        void shutdown() override;
        void beginFrame() override;
        void endFrame() override;
        
        // Vulkan-specific getters
        VkDevice getDevice() const { return device; }
        VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
        VkQueue getGraphicsQueue() const { return graphicsQueue; }
        VkCommandBuffer getCurrentCommandBuffer() const { return m_commandBuffers[currentFrame]; }
        VkExtent2D getSwapchainExtent() const { return vkSwapchain ? vkSwapchain->extent() : VkExtent2D{1920, 1080}; }
        bool isFrameReady() const { return m_frameReady; }
        UIRenderer* getUIRenderer() const { return m_uiRenderer.get(); }
        VulkanClusteredRenderer* getClusteredRenderer() const { return m_clusteredRenderer.get(); }
        TaffyOverlayManager* getOverlayManager() const { return m_overlayManager.get(); }
        void enqueueUiMessage(const std::string& text, float durationSeconds = 4.0f, uint32_t color = 0xFFD060FF);


    private:
        friend class VulkanBackendControls;
        friend class VulkanOverlayBridge;
        friend class VulkanUiBridge;

        // === CORE VULKAN OBJECTS ===

        // Instance and devices
        InstanceResource instance;
        SurfaceResource surface;
        std::unique_ptr<VulkanDevice> vkDevice;
        std::unique_ptr<SwapChain> vkSwapchain;
        std::unique_ptr<VulkanResourceManager> res;

        // Raw Vulkan handles (cached from device)
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;

        // Window reference
        SDL_Window* w = nullptr;

        // === RENDERING SUBSYSTEMS ===

        // Renderer systems
        std::unique_ptr<VulkanClusteredRenderer> m_clusteredRenderer;
        std::unique_ptr<DynamicRenderer> dr;
        std::unique_ptr<ShaderManager> sm;
        std::unique_ptr<RenderPass> rp;
        std::unique_ptr<SDFTextRenderer> m_textRenderer;
        std::unique_ptr<UIRenderer> m_uiRenderer;
        std::unique_ptr<SequencerUI> m_sequencerUI;
        std::function<void(int)> m_sequencerCallback;
        std::unique_ptr<VulkanEditorBridge> m_editorIntegration;
        
        // Main menu button IDs for visibility control
        uint32_t m_toggleOverlayButtonId = 0;
        uint32_t m_modelEditorButtonId = 0;
        uint32_t m_exitButtonId = 0;
        uint32_t m_meshShaderStatusLabelId = 0;
        std::vector<uint32_t> m_uiMessageLabelIds;
        std::vector<uint32_t> m_profilerLabelIds;
        bool m_profilerOverlayVisible = true;

        // Camera
        Camera cam;

        // === PIPELINES AND SHADERS ===

        // Mesh shader pipeline
        std::unique_ptr<PipelineResource> m_meshShaderPipeline;
        std::unique_ptr<PipelineLayoutResource> m_meshShaderPipelineLayout;

        // Graphics pipeline
        std::unique_ptr<PipelineLayoutResource> m_pipelineLayout;
        std::unique_ptr<PipelineResource> m_graphicsPipeline;

        // Shader management
        std::vector<std::shared_ptr<ShaderModule>> m_pipelineShaders;
        ShaderReflection m_combinedReflection;

        // === DESCRIPTORS ===

        std::vector<std::unique_ptr<DescriptorSetLayoutResource>> m_descriptorSetLayouts;
        std::vector<std::unique_ptr<DescriptorSetResource>> m_descriptorSets;
        std::unique_ptr<DescriptorSetLayoutResource> m_descriptorSetLayout;
        std::unique_ptr<DescriptorPoolResource> m_descriptorPool;
        std::unique_ptr<DescriptorSetResource> m_descriptorSet;

        // === UNIFORM BUFFERS ===

        struct UniformBufferObject {
            alignas(16) glm::mat4 model;
            alignas(16) glm::mat4 view;
            alignas(16) glm::mat4 proj;
            alignas(16) glm::vec3 cameraPos;
        };

        struct alignas(16) LightUBO {
            alignas(16) glm::vec3 position;
            alignas(16) glm::vec3 color;
            float ambientStrength;
            float diffuseStrength;
            float specularStrength;
            float shininess;
        };

        struct alignas(16) MaterialUBO {
            alignas(16) glm::vec4 baseColor;
            float metallic;
            float roughness;
            float ao;
            float emissiveFactor;
            alignas(16) glm::vec3 emissiveColor;
            float padding;

            // Texture availability flags
            int hasAlbedoMap;
            int hasNormalMap;
            int hasMetallicRoughnessMap;
            int hasEmissiveMap;
            int hasOcclusionMap;
        };

        std::unique_ptr<Buffer> m_uniformBuffer;
        std::unique_ptr<Buffer> m_lightBuffer;
        std::unique_ptr<Buffer> m_materialBuffer;

        // === VERTEX DATA ===

        struct BlinnPhongVertex {
            float position[3];  // XYZ position - location 0
            float normal[3];    // Normal vector - location 1
            float texCoord[2];  // UV coordinates - location 2

            static VkVertexInputBindingDescription getBindingDescription();
            static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
        };

        std::unique_ptr<VertexBufferSimple> m_vertexBuffer;
        std::unique_ptr<IndexBuffer> m_indexBuffer;

        // === TEXTURES ===

        std::unique_ptr<ImageResource> m_textureImage;
        std::unique_ptr<DeviceMemoryResource> m_textureImageMemory;
        std::unique_ptr<ImageViewResource> m_missingTextureImageView;
        std::unique_ptr<SamplerResource> m_textureSampler;

        // === DEPTH BUFFER ===

        std::unique_ptr<ImageResource> m_depthImage;
        std::unique_ptr<DeviceMemoryResource> m_depthImageMemory;
        std::unique_ptr<ImageViewResource> m_depthImageView;
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

        // === MULTISAMPLING (MSAA) ===
        
        VkSampleCountFlagBits m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        std::unique_ptr<ImageResource> m_colorImage;
        std::unique_ptr<DeviceMemoryResource> m_colorImageMemory;
        std::unique_ptr<ImageViewResource> m_colorImageView;

        // === COMMAND BUFFERS AND SYNC ===

        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

        std::unique_ptr<CommandPoolResource> m_commandPool;
        CommandPoolResource m_transferCommandPool;
        std::vector<VkCommandBuffer> m_commandBuffers;

        // Synchronization objects
        std::vector<SemaphoreResource> m_imageAvailableSemaphores;
        std::vector<SemaphoreResource> m_renderFinishedSemaphores;
        std::vector<FenceResource> m_inFlightFences;

        // Frame tracking
        size_t currentFrame = 0;
        uint32_t m_currentImageIndex = 0;
        bool m_frameReady = false;
        bool m_framebufferResized = false;

        // === FRAMEBUFFERS ===

        std::vector<std::unique_ptr<Framebuffer>> m_framebuffers;

        // === SCENE MANAGEMENT ===

        // Spatial partitioning
        Octree<RenderableObject> m_sceneOctree;

        // Asset management
        MeshRegistry m_meshRegistry;
        std::vector<uint32_t> m_materialIDs;
        uint32_t m_cubeMeshID;

        // Taffy asset system
        std::unique_ptr<Tremor::TaffyAssetLoader> taffy_loader_;
        std::vector<std::unique_ptr<Tremor::TaffyAssetLoader::LoadedAsset>> loaded_assets_;
        std::unique_ptr<TaffyOverlayManager> m_overlayManager;
        std::unique_ptr<TaffyMeshShaderManager> m_taffyMeshShaderManager;
        PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT_ = nullptr;

        // Development/debug overlay state
        bool hot_pink_enabled = true;
        bool reload_assets_requested = false;
        std::chrono::steady_clock::time_point last_overlay_check_;
        const std::chrono::milliseconds overlay_check_interval_{ 1000 };

        // === DEBUG AND VALIDATION ===

#if _DEBUG
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData);
#endif

        // === INITIALIZATION METHODS ===

        bool createInstance();
        bool createDeviceAndSwapChain();
        bool createCommandPool();
        bool createCommandBuffers();
        bool createSyncObjects();
        bool createDepthResources();
        bool createRenderPass();
        bool createFramebuffers();
        bool initializeCoreVulkan(SDL_Window* window);
        bool initializeRenderResources();
        bool initializeRenderSystems();
        bool initializeEditorAndUi();
        void initializeOverlayAndDevAssets();
        bool recreateSwapchainResources();
        bool refreshSwapchainDependents();
        
        // === MULTISAMPLING METHODS ===
        
        VkSampleCountFlagBits getMaxUsableSampleCount();
        bool createColorResources();

        // === RESOURCE CREATION METHODS ===

        bool createUniformBuffer();
        bool createLightBuffer();
        bool createMaterialBuffer();
        bool createCubeMesh() { return true; }
        bool createTestTexture();
        bool createDescriptorSetLayouts();
        bool createAndUpdateDescriptorSets() { return true; }
        bool createMinimalMeshShaderPipeline();
        bool createGraphicsPipeline() { return true; }

        // === VERTEX DATA CREATION ===

        std::vector<BlinnPhongVertex> createCube();
        void createCubeRenderableObject();

        // === TAFFY ASSET METHODS ===

        // === UPDATE METHODS ===

        void updateUniformBuffer();
        bool updateLight();

        // === RENDERING METHODS ===

        void renderWithMeshShader(VkCommandBuffer cmdBuffer);

        // === UTILITY METHODS ===

        VkFormat findDepthFormat();
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);

        // Device selection helpers
        bool isDeviceSuitable(VkPhysicalDevice device);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);

        // Swap chain helpers
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

        // === MEMBER VARIABLES FOR LEGACY SUPPORT ===

        // Format information
        VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
        uint32_t m_gfxQueueFamilyIndex = 0;

        // Properties
        VkPhysicalDeviceMemoryProperties m_memoryProperties{};
        VkPhysicalDeviceProperties m_deviceProperties{};
        VkPhysicalDeviceFeatures m_deviceFeatures{};

        // Feature flags
        bool get_surface_capabilities_2 = false;
        bool vulkan_1_4_available = false;
        bool debug_utils = false;
        bool memory_report = false;
        bool enableValidation = false;

        // Validation layers and device extensions
        const std::vector<const char*> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };

        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        // Reflection data storage
        std::vector<tremor::gfx::ShaderReflection::UniformBuffer> m_uniformBuffers;
        std::vector<tremor::gfx::ShaderReflection::ResourceBinding> m_resourceBindings;
    };

    // Vulkan-specific command buffer wrapper
    class VulkanRenderCommandBuffer : public RenderCommandBuffer {
    public:
        VulkanRenderCommandBuffer(VkCommandBuffer cmdBuffer) : m_cmdBuffer(cmdBuffer) {}
        VkCommandBuffer getHandle() const { return m_cmdBuffer; }

    private:
        VkCommandBuffer m_cmdBuffer;
    };

    class VulkanClusteredRenderer : public ClusteredRenderer {
    public:

        std::unique_ptr<TaffyMeshShaderManager> mesh_shader_manager_;

        VulkanClusteredRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
            VkQueue graphicsQueue, uint32_t graphicsQueueFamily,
            VkCommandPool commandPool, const ClusterConfig& config,
            VkRenderPass renderPass = VK_NULL_HANDLE,
            bool useDynamicRendering = true,
            VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);
        ~VulkanClusteredRenderer() override;

        // Method declarations - implementations go to vk.cpp
        void createClusterGrid() override;
        bool initialize(Format colorFormat, Format depthFormat) override;
        bool onSwapchainRecreated(VkRenderPass renderPass, Format colorFormat, Format depthFormat,
                                  VkSampleCountFlagBits sampleCount);
        void shutdown() override;
        uint32_t loadMesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices, const std::string& name = "") override;
        uint32_t createMaterial(const PBRMaterial& material) override;
        void render(RenderCommandBuffer* cmdBuffer, Camera* camera) override{}
        void updateGPUBuffers() override;
        void buildClusters(Camera* camera, Octree<RenderableObject>& octree);
        void updateLights(const std::vector<ClusterLight>& lights) override;
        void render(VkCommandBuffer cmdBuffer, Camera* camera);
        bool isMeshShaderPathActive() const { return m_lastRenderUsedMeshShaderPath; }

        // Simple getter - can stay inline
        VkDevice getDevice() const { return m_device; }

        // Public members that need to be accessible
        Taffy::Asset test_asset_;
        Tremor::TaffyMesh test_mesh_;

    protected:
        //void onClustersUpdated() override;
        //void onLightsUpdated() override;
        //void onMeshDataUpdated() override;

    private:
        // Vulkan-specific members
        VkDevice m_device;
        ClusterConfig m_config;
        VkPhysicalDevice m_physicalDevice;
        VkCommandPool m_commandPool;
        VkQueue m_graphicsQueue;
        uint32_t m_graphicsQueueFamily;
        uint32_t m_totalClusters;
        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        bool m_useDynamicRendering = true;
        VkSampleCountFlagBits m_sampleCount = VK_SAMPLE_COUNT_1_BIT;
        bool m_meshShaderSupported = false;
        bool m_lastRenderUsedMeshShaderPath = false;

        // Rendering configuration
        VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

        // Shader modules
        std::unique_ptr<ShaderModule> m_taskShader;
        std::unique_ptr<ShaderModule> m_meshShader;
        std::unique_ptr<ShaderModule> m_fragmentShader;
        std::unique_ptr<ShaderModule> m_debugTaskShader;
        std::unique_ptr<ShaderModule> m_debugMeshShader;
        std::unique_ptr<ShaderModule> m_fallbackVertexShader;
        std::unique_ptr<ShaderModule> m_fallbackFragmentShader;

        // Pipeline resources
        std::unique_ptr<PipelineLayoutResource> m_pipelineLayout;
        std::unique_ptr<PipelineResource> m_pipeline;
        std::unique_ptr<PipelineResource> m_wireframePipeline;
        std::unique_ptr<PipelineResource> m_debugPipeline;
        std::unique_ptr<PipelineResource> m_testBufferPipeline;
        std::unique_ptr<PipelineResource> m_workingMeshPipeline;
        std::unique_ptr<PipelineLayoutResource> m_workingMeshPipelineLayout;
        std::unique_ptr<PipelineResource> m_fallbackPipeline;
        std::unique_ptr<PipelineLayoutResource> m_fallbackPipelineLayout;

        // Descriptor resources
        std::unique_ptr<DescriptorSetLayoutResource> m_descriptorSetLayout;
        std::unique_ptr<DescriptorPoolResource> m_descriptorPool;
        std::unique_ptr<DescriptorSetResource> m_descriptorSet;

        // GPU buffers
        std::unique_ptr<Buffer> m_clusterBuffer;
        std::unique_ptr<Buffer> m_objectBuffer;
        std::unique_ptr<Buffer> m_lightBuffer;
        std::unique_ptr<Buffer> m_indexBuffer;
        std::unique_ptr<Buffer> m_uniformBuffer;
        std::unique_ptr<Buffer> m_vertexBuffer;
        std::unique_ptr<Buffer> m_fallbackVertexBuffer;
        std::unique_ptr<Buffer> m_meshIndexBuffer;
        std::unique_ptr<Buffer> m_meshInfoBuffer;
        std::unique_ptr<Buffer> m_materialBuffer;

        // Default textures
        std::unique_ptr<ImageResource> m_defaultAlbedoTexture;
        std::unique_ptr<ImageViewResource> m_defaultAlbedoView;
        std::unique_ptr<ImageResource> m_defaultNormalTexture;
        std::unique_ptr<ImageViewResource> m_defaultNormalView;
        std::unique_ptr<SamplerResource> m_defaultSampler;

        // Private method declarations - implementations go to vk.cpp
        bool createMeshBuffers();
        bool createFallbackPipeline();
        bool createDefaultTextures();
        bool supportsMeshShaderPath() const;
        void renderFallback(VkCommandBuffer cmdBuffer, Camera* camera);
        void updateUniformBuffers(Camera* camera);
        void updateMeshBuffers();
        void updateMaterialBuffer();
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkFormat convertFormat(Format format);
    };

    // More declarations for other classes...
    // (VertexBuffer, IndexBuffer, ShaderManager, etc. - just declarations)

    // Inline helper functions
    inline const char* getDescriptorTypeName(VkDescriptorType type) {
        switch (type) {
        case VK_DESCRIPTOR_TYPE_SAMPLER: return "SAMPLER";
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return "COMBINED_IMAGE_SAMPLER";
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return "SAMPLED_IMAGE";
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "STORAGE_IMAGE";
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: return "UNIFORM_TEXEL_BUFFER";
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: return "STORAGE_TEXEL_BUFFER";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return "UNIFORM_BUFFER";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return "STORAGE_BUFFER";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: return "UNIFORM_BUFFER_DYNAMIC";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return "STORAGE_BUFFER_DYNAMIC";
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return "INPUT_ATTACHMENT";
        default: return "UNKNOWN";
        }
    }

    class ShaderManager {
    public:
        ShaderManager(VkDevice device) : m_device(device) {}

        std::shared_ptr<ShaderModule> loadShader(
            const std::string & filename,
            const std::string & entryPoint = "main",
            const ShaderCompiler::CompileOptions & options = ShaderCompiler::CompileOptions{});

        void checkForChanges();

        std::filesystem::file_time_type getFileTimestamp(const std::string& filename);

        // Load a shader with automatic compilation
        // Check for shader file changes and reload if needed
        void notifyShaderReloaded(const std::string& filename, std::shared_ptr<ShaderModule> shader);
    private:
        // Get file modification timestamp

        VkDevice m_device;
        std::unordered_map<std::string, std::shared_ptr<ShaderModule>> m_shaders;
        std::unordered_map<std::string, std::filesystem::file_time_type> m_shaderFileTimestamps;
    };
    
    using namespace Taffy;



} // namespace tremor::gfx
