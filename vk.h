#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Important for Vulkan depth range

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
#include <spirv_cross/spirv_hlsl.hpp>
#include <spirv_cross/spirv_msl.hpp>

#include <memory>

#include "gfx.h"
#include "quan.h"
#include "handle.h"
#include "taffy.h"
#include "taffy/tools.h"
#include "renderer/taffy_mesh.h"
#include "renderer/taffy_integration.h"
#include "asset.h"

// Define concepts for Vulkan types
template<typename T>
concept VulkanStructure = requires(T t) {
    { t.sType } -> std::convertible_to<VkStructureType>;
    { t.pNext } -> std::convertible_to<void*>;
};

// Type-safe structure creation - KEEP INLINE
template<VulkanStructure T>
inline T createVulkanStructure() {
    T result{};
    result.sType = getVulkanStructureType<T>();
    return result;
}

// Instead of ZEROED_STRUCT macro - KEEP INLINE
template<typename T>
inline T createStructure() {
    T result{};
    result.sType = getStructureType<T>();
    return result;
}

// Instead of CHAIN_PNEXT macro - KEEP INLINE
template<typename T>
inline void chainStructure(void** ppNext, T& structure) {
    *ppNext = &structure;
    ppNext = &structure.pNext;
}

// Helper function to copy buffer data - DECLARATION ONLY
void copyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue,
    VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

namespace tremor::gfx {

    using namespace tremor::taffy::tools;

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


    class TaffyMeshShaderPipeline {
    private:
        VkDevice device_;
        VkPhysicalDevice physical_device_;
        VkRenderPass render_pass_;

        // Shader modules
        VkShaderModule task_shader_module_ = VK_NULL_HANDLE;
        VkShaderModule mesh_shader_module_ = VK_NULL_HANDLE;
        VkShaderModule fragment_shader_module_ = VK_NULL_HANDLE;

        // Pipeline objects
        VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
        VkPipeline graphics_pipeline_ = VK_NULL_HANDLE;

        // Mesh shader parameters
        uint32_t max_vertices_ = 0;
        uint32_t max_primitives_ = 0;

    public:
        TaffyMeshShaderPipeline(VkDevice device, VkPhysicalDevice physical_device)
            : device_(device), physical_device_(physical_device) {
        }

        /**
         * Create pipeline from Taffy asset
         */
        bool create_from_taffy_asset(const Taffy::Asset& asset) {
            std::cout << "Creating mesh shader pipeline from Taffy asset..." << std::endl;

            // Extract shader data
            auto shader_data = asset.get_chunk_data(Taffy::ChunkType::SHDR);
            if (!shader_data) {
                std::cerr << "No shader data found" << std::endl;
                return false;
            }

            // Parse shader chunk header
            Taffy::ShaderChunk shader_header;
            if (shader_data->size() < sizeof(shader_header)) {
                std::cerr << "Invalid shader chunk size" << std::endl;
                return false;
            }
            std::memcpy(&shader_header, shader_data->data(), sizeof(shader_header));

            std::cout << "Found " << shader_header.shader_count << " shaders in asset" << std::endl;

            // Extract individual shaders
            size_t offset = sizeof(shader_header);
            for (uint32_t i = 0; i < shader_header.shader_count; ++i) {
                if (!extract_and_compile_shader(*shader_data, i)) {
                    std::cerr << "Failed to extract shader " << i << std::endl;
                    return false;
                }
            }

            // Create pipeline layout
            if (!create_pipeline_layout()) {
                std::cerr << "Failed to create pipeline layout" << std::endl;
                return false;
            }

            // Create graphics pipeline
            if (!create_graphics_pipeline()) {
                std::cerr << "Failed to create graphics pipeline" << std::endl;
                return false;
            }

            std::cout << "✓ Mesh shader pipeline created successfully!" << std::endl;
            return true;
        }

        /**
         * Render using mesh shader pipeline
         */
        void render(VkCommandBuffer command_buffer, PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT = VK_NULL_HANDLE) {

            if (!graphics_pipeline_) {
                std::cerr << "Pipeline not created!" << std::endl;
                return;
            }

            if (!vkCmdDrawMeshTasksEXT) {
                vkCmdDrawMeshTasksEXT = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(
                    vkGetDeviceProcAddr(device_, "vkCmdDrawMeshTasksEXT"));

                if (!vkCmdDrawMeshTasksEXT) {
                    std::cerr << "Mesh shader draw function not available!" << std::endl;
                    return;
                }
            }

            std::cout << "🎮 Rendering with mesh shaders!" << std::endl;

            // Bind mesh shader pipeline
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);

            // Draw using mesh tasks instead of traditional vertex buffers!
            // This is the magic - no vertex data needed, mesh shader generates geometry!
            vkCmdDrawMeshTasksEXT(command_buffer, 1, 1, 1);

            std::cout << "✓ Mesh shader draw call submitted!" << std::endl;
        }

    private:
        bool extract_and_compile_shader(const std::vector<uint8_t>& shader_data, uint32_t shader_index) {
            std::cout << "🔍 EXTRACTING SHADER " << shader_index << ":" << std::endl;

            const uint8_t* chunk_ptr = shader_data.data();
            size_t chunk_size = shader_data.size();

            // Parse header first
            Taffy::ShaderChunk header;
            std::memcpy(&header, chunk_ptr, sizeof(header));
            std::cout << "  Total shaders in chunk: " << header.shader_count << std::endl;

            if (shader_index >= header.shader_count) {
                std::cerr << "  ❌ Shader index out of range!" << std::endl;
                return false;
            }

            // Calculate offset to this shader's info
            size_t shader_info_offset = sizeof(Taffy::ShaderChunk) + shader_index * sizeof(Taffy::ShaderChunk::Shader);
            std::cout << "  Shader info offset: " << shader_info_offset << std::endl;

            if (shader_info_offset + sizeof(Taffy::ShaderChunk::Shader) > chunk_size) {
                std::cerr << "  ❌ Shader info extends beyond chunk!" << std::endl;
                return false;
            }

            // Read shader info
            Taffy::ShaderChunk::Shader shader_info;
            std::memcpy(&shader_info, chunk_ptr + shader_info_offset, sizeof(shader_info));

            std::cout << "  Name hash: 0x" << std::hex << shader_info.name_hash << std::dec << std::endl;
            std::cout << "  Stage: " << static_cast<uint32_t>(shader_info.stage) << std::endl;
            std::cout << "  SPIR-V size: " << shader_info.spirv_size << " bytes" << std::endl;

            // Calculate SPIR-V offset for THIS specific shader
            size_t spirv_data_start = sizeof(Taffy::ShaderChunk) + header.shader_count * sizeof(Taffy::ShaderChunk::Shader);
            size_t spirv_offset = spirv_data_start;

            // Skip SPIR-V data for previous shaders
            for (uint32_t i = 0; i < shader_index; ++i) {
                size_t prev_shader_info_offset = sizeof(Taffy::ShaderChunk) + i * sizeof(Taffy::ShaderChunk::Shader);
                Taffy::ShaderChunk::Shader prev_shader;
                std::memcpy(&prev_shader, chunk_ptr + prev_shader_info_offset, sizeof(prev_shader));
                spirv_offset += prev_shader.spirv_size;
                std::cout << "  Skipping shader " << i << " SPIR-V: " << prev_shader.spirv_size << " bytes" << std::endl;
            }

            std::cout << "  This shader's SPIR-V offset: " << spirv_offset << std::endl;

            // Validate SPIR-V bounds
            if (spirv_offset + shader_info.spirv_size > chunk_size) {
                std::cerr << "  ❌ SPIR-V data extends beyond chunk!" << std::endl;
                std::cerr << "    SPIR-V end: " << (spirv_offset + shader_info.spirv_size) << std::endl;
                std::cerr << "    Chunk size: " << chunk_size << std::endl;
                return false;
            }

            // Validate SPIR-V magic
            if (shader_info.spirv_size >= 4) {
                uint32_t magic;
                std::memcpy(&magic, chunk_ptr + spirv_offset, sizeof(magic));
                std::cout << "  SPIR-V magic: 0x" << std::hex << magic << std::dec;

                if (magic == 0x07230203) {
                    std::cout << " ✅ VALID" << std::endl;
                }
                else {
                    std::cout << " ❌ INVALID! Expected 0x07230203" << std::endl;
                    return false;
                }
            }

            // Create Vulkan shader module
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = shader_info.spirv_size;
            createInfo.pCode = reinterpret_cast<const uint32_t*>(chunk_ptr + spirv_offset);

            VkShaderModule shaderModule;
            VkResult result = vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule);

            if (result != VK_SUCCESS) {
                std::cerr << "  ❌ Failed to create Vulkan shader module! VkResult: " << result << std::endl;
                return false;
            }

            std::cout << "  ✅ Shader " << shader_index << " extracted and compiled successfully!" << std::endl;

            // Store the shader module based on stage
            if (shader_info.stage == Taffy::ShaderChunk::Shader::ShaderStage::MeshShader) {
                mesh_shader_module_ = shaderModule;  // Store mesh shader
                std::cout << "    → Stored as mesh shader module" << std::endl;
            }
            else if (shader_info.stage == Taffy::ShaderChunk::Shader::ShaderStage::Fragment) {
                fragment_shader_module_ = shaderModule;  // Store fragment shader
                std::cout << "    → Stored as fragment shader module" << std::endl;
            }

            return true;
        }
        bool create_pipeline_layout() {
            VkPipelineLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.setLayoutCount = 0;
            layout_info.pSetLayouts = nullptr;
            layout_info.pushConstantRangeCount = 0;
            layout_info.pPushConstantRanges = nullptr;

            VkResult result = vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_);
            return result == VK_SUCCESS;
        }

        bool create_graphics_pipeline() {
            std::vector<VkPipelineShaderStageCreateInfo> shader_stages;

            // Task shader stage (optional)
            if (task_shader_module_ != VK_NULL_HANDLE) {
                VkPipelineShaderStageCreateInfo task_stage{};
                task_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                task_stage.stage = VK_SHADER_STAGE_TASK_BIT_NV;
                task_stage.module = task_shader_module_;
                task_stage.pName = "main";
                shader_stages.push_back(task_stage);
            }

            // Mesh shader stage (required)
            if (mesh_shader_module_ != VK_NULL_HANDLE) {
                VkPipelineShaderStageCreateInfo mesh_stage{};
                mesh_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                mesh_stage.stage = VK_SHADER_STAGE_MESH_BIT_NV;
                mesh_stage.module = mesh_shader_module_;
                mesh_stage.pName = "main";
                shader_stages.push_back(mesh_stage);
            }

            // Fragment shader stage
            if (fragment_shader_module_ != VK_NULL_HANDLE) {
                VkPipelineShaderStageCreateInfo frag_stage{};
                frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                frag_stage.module = fragment_shader_module_;
                frag_stage.pName = "main";
                shader_stages.push_back(frag_stage);
            }

            // For mesh shaders, we don't need vertex input state!
            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input.vertexBindingDescriptionCount = 0;
            vertex_input.pVertexBindingDescriptions = nullptr;
            vertex_input.vertexAttributeDescriptionCount = 0;
            vertex_input.pVertexAttributeDescriptions = nullptr;

            // Input assembly (not used for mesh shaders but required)
            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            input_assembly.primitiveRestartEnable = VK_FALSE;

            // Use your existing viewport, rasterization, multisampling, color blend states
            // For demo, using basic states:

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = 800.0f;  // Use your actual swapchain extent
            viewport.height = 600.0f;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = { 800, 600 };  // Use your actual swapchain extent

            VkPipelineViewportStateCreateInfo viewport_state{};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.pViewports = &viewport;
            viewport_state.scissorCount = 1;
            viewport_state.pScissors = &scissor;

            // Basic rasterization state
            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
            rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            // Basic multisampling
            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            // Basic color blending
            VkPipelineColorBlendAttachmentState color_blend_attachment{};
            color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            color_blend_attachment.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo color_blending{};
            color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blending.logicOpEnable = VK_FALSE;
            color_blending.attachmentCount = 1;
            color_blending.pAttachments = &color_blend_attachment;

            // Create graphics pipeline
            VkGraphicsPipelineCreateInfo pipeline_info{};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
            pipeline_info.pStages = shader_stages.data();
            pipeline_info.pVertexInputState = &vertex_input;
            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterizer;
            pipeline_info.pMultisampleState = &multisampling;
            pipeline_info.pColorBlendState = &color_blending;
            pipeline_info.layout = pipeline_layout_;
            pipeline_info.renderPass = render_pass_;
            pipeline_info.subpass = 0;

            VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline_);

            if (result == VK_SUCCESS) {
                std::cout << "✓ Graphics pipeline created successfully!" << std::endl;
            }
            else {
                std::cerr << "Failed to create graphics pipeline: " << result << std::endl;
            }

            return result == VK_SUCCESS;
        }

    public:
        ~TaffyMeshShaderPipeline() {
        }
    };


    class TaffyMeshShaderManager {
    private:
        VkDevice device_;
        VkPhysicalDevice physical_device_;
        PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT_ = nullptr;

        // Pipeline cache for loaded mesh shader assets
        std::unordered_map<std::string, std::unique_ptr<TaffyMeshShaderPipeline>> pipelines_;

    public:
        TaffyMeshShaderManager(VkDevice device, VkPhysicalDevice physical_device)
            : device_(device), physical_device_(physical_device) {

            // Get mesh shader function pointer
            vkCmdDrawMeshTasksEXT_ = (PFN_vkCmdDrawMeshTasksEXT)
                vkGetDeviceProcAddr(device, "vkCmdDrawMeshTasksEXT");

            if (vkCmdDrawMeshTasksEXT_) {
                std::cout << "✓ Mesh shader manager initialized!" << std::endl;
            }
            else {
                std::cout << "✗ Failed to get mesh shader function pointer!" << std::endl;
            }
        }

        void debug_print_pipelines() {
            std::cout << "🔍 Registered pipelines:" << std::endl;
            for (const auto& [key, pipeline] : pipelines_) {
                std::cout << "    \"" << key << "\" -> " << pipeline.get() << std::endl;
            }
            if (pipelines_.empty()) {
                std::cout << "    (No pipelines registered!)" << std::endl;
            }
        }

        /**
         * STEP 3: Load Taffy asset and create mesh shader pipeline
         */
        bool load_taffy_asset(const std::string& filepath) {
            std::cout << "🔥 Loading Taffy mesh shader asset: " << filepath << std::endl;

            // Load Taffy asset
            Taffy::Asset asset;
            if (!asset.load_from_file_safe(filepath)) {
                std::cerr << "Failed to load Taffy asset: " << filepath << std::endl;
                return false;
            }

            // Check for mesh shader support
            if (!asset.has_feature(Taffy::FeatureFlags::MeshShaders)) {
                std::cout << "Asset doesn't contain mesh shaders, using fallback" << std::endl;
                return false;
            }

            if (!asset.has_chunk(Taffy::ChunkType::SHDR)) {
                std::cout << "No shader chunk found in asset" << std::endl;
                return false;
            }

            // Create mesh shader pipeline
            auto pipeline = std::make_unique<TaffyMeshShaderPipeline>(device_, physical_device_);

            if (!pipeline->create_from_taffy_asset(asset)) {
                std::cerr << "Failed to create mesh shader pipeline from asset" << std::endl;
                return false;
            }

            // Store pipeline for rendering
            pipelines_[filepath] = std::move(pipeline);

            std::cout << "🚀 Mesh shader pipeline created successfully for: " << filepath << std::endl;
            return true;
        }

        /**
         * STEP 4: Render mesh shader asset
         */
        void render_asset(const std::string& filepath, VkCommandBuffer command_buffer) {
            auto it = pipelines_.find(filepath);
            if (it == pipelines_.end()) {
                std::cerr << "Pipeline not found for asset: " << filepath << std::endl;
                return;
            }

            // Render using mesh shader pipeline
            it->second->render(command_buffer, vkCmdDrawMeshTasksEXT_);
        }

        /**
         * Get all loaded pipelines
         */
        const std::unordered_map<std::string, std::unique_ptr<TaffyMeshShaderPipeline>>& get_pipelines() const {
            return pipelines_;
        }
    };


    VkDescriptorSetLayout createMeshShaderDescriptorSetLayout(VkDevice device);

    struct MeshShaderPushConstants {
        uint32_t vertex_count;
        uint32_t primitive_count;
        uint32_t vertex_stride_floats;
        uint32_t reserved;
    };

    /**
     * @brief Unified manager for Taffy assets with overlay support and mesh shader rendering
     *
     * This class combines the functionality of the original TaffyOverlayManager and TaffyAssetRenderer,
     * providing a single interface for loading, managing, and rendering Taffy assets with overlay support.
     * It also manages pipeline creation and caching for simplified rendering.
     */
    class TaffyOverlayManager {
    public:
        /**
         * @brief GPU data for mesh shader assets
         */
        struct MeshAssetGPUData {
            VkBuffer vertexStorageBuffer = VK_NULL_HANDLE;
            VkDeviceMemory vertexStorageMemory = VK_NULL_HANDLE;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
            uint32_t vertexCount = 0;
            uint32_t primitiveCount = 0;
            uint32_t vertexStrideFloats = 0;
            bool usesMeshShader = false;
        };


        /**
         * @brief Constructor
         * @param device Vulkan device handle
         * @param physicalDevice Vulkan physical device handle
         * @param renderPass Render pass for pipeline creation (can be VK_NULL_HANDLE for dynamic rendering)
         * @param swapchainExtent Current swapchain extent for viewport setup
         */
        TaffyOverlayManager(VkDevice device, VkPhysicalDevice physicalDevice,
            VkRenderPass renderPass, VkExtent2D swapchainExtent);

        /**
         * @brief Destructor - cleans up all Vulkan resources including pipelines
         */
        ~TaffyOverlayManager();

        // Simplified rendering interface
        /**
         * @brief Render a Taffy asset with automatic pipeline management
         * @param asset_path Path to the .taf file
         * @param cmd Command buffer to record rendering commands
         *
         * This method handles:
         * - Loading the asset if not already loaded
         * - Creating/caching the pipeline for the asset
         * - Uploading geometry to GPU
         * - Binding pipeline and resources
         * - Issuing draw commands
         */
        void renderMeshAsset(const std::string& asset_path, VkCommandBuffer cmd);

        // Asset loading and overlay management
        /**
         * @brief Load a master asset
         * @param master_path Path to the master .taf file
         */
        void load_master_asset(const std::string& master_path);

        /**
         * @brief Load an asset with overlay applied
         * @param master_path Path to the master .taf file
         * @param overlay_path Path to the overlay .tafo file
         */
        void loadAssetWithOverlay(const std::string& master_path, const std::string& overlay_path);

        /**
         * @brief Clear all overlays for a master asset
         * @param master_path Path to the master .taf file
         */
        void clear_overlays(const std::string& master_path);

        /**
         * @brief Check if any pipelines need rebuilding and rebuild them
         * Call this periodically or after overlay changes
         */
        void checkForPipelineUpdates();

        /**
         * @brief Update the swapchain extent (needed after window resize)
         * @param newExtent New swapchain extent
         */
        void updateSwapchainExtent(VkExtent2D newExtent) { swapchain_extent_ = newExtent; }

        /**
         * @brief Mark a pipeline for rebuild (e.g., after shader hot-reload)
         * @param asset_path Path to the asset whose pipeline needs rebuilding
         */
        void invalidatePipeline(const std::string& asset_path);

        // Accessors
        VkDescriptorPool getDescriptorPool() const { return descriptorPool; }
        VkDescriptorSetLayout getMeshShaderDescriptorSetLayout() const { return meshShaderDescSetLayout; }

    private:
        struct PipelineInfo {
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout layout = VK_NULL_HANDLE;
            VkShaderModule taskShader = VK_NULL_HANDLE;
            VkShaderModule meshShader = VK_NULL_HANDLE;
            VkShaderModule fragmentShader = VK_NULL_HANDLE;
            std::string vertexShaderHash;
            std::string fragmentShaderHash;
        };

        VkDevice device_;
        VkPhysicalDevice physical_device_;
        VkRenderPass render_pass_;
        VkExtent2D swapchain_extent_;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout meshShaderDescSetLayout = VK_NULL_HANDLE;

        // Storage for loaded assets and their resources
        std::unordered_map<std::string, std::unique_ptr<Taffy::Asset>> loaded_assets_;
        std::unordered_map<std::string, MeshAssetGPUData> gpu_data_cache_;
        std::unordered_map<std::string, PipelineInfo> pipeline_cache_;
        std::unordered_map<std::string, bool> pipeline_rebuild_flags_;

        /**
         * @brief Ensure an asset is loaded and ready for rendering
         * @param asset_path Path to the asset
         * @return true if asset is loaded successfully
         */
        bool ensureAssetLoaded(const std::string& asset_path);

        /**
         * @brief Get or create a pipeline for the given asset
         * @param asset_path Path to the asset
         * @return Pointer to pipeline info, or nullptr on failure
         */
        PipelineInfo* getOrCreatePipeline(const std::string& asset_path);

        /**
         * @brief Create a new pipeline for an asset
         * @param asset_path Path to the asset
         * @return Pointer to pipeline info in cache, or nullptr on failure
         */
        PipelineInfo* createPipelineForAsset(const std::string& asset_path);

        /**
         * @brief Create a mesh shader pipeline
         * @param pipelineInfo Pipeline info with shader modules
         * @return Created pipeline handle
         */
        VkPipeline createMeshShaderPipeline(const PipelineInfo& pipelineInfo);

        /**
         * @brief Rebuild a pipeline (e.g., after overlay changes)
         * @param asset_path Path to the asset
         */
        void rebuildPipeline(const std::string& asset_path);

        /**
         * @brief Clean up shader modules
         * @param pipelineInfo Pipeline info containing shader modules
         */
        void cleanupShaderModules(const PipelineInfo& pipelineInfo);

        /**
         * @brief Internal render method with explicit pipeline and GPU data
         */
        void renderMeshAssetInternal(VkCommandBuffer cmd, VkPipeline meshPipeline,
            VkPipelineLayout pipelineLayout, const MeshAssetGPUData& gpuData);

        /**
         * @brief Upload a Taffy asset to GPU memory
         * @param asset The Taffy asset to upload
         * @return GPU data structure
         */
        MeshAssetGPUData uploadTaffyAsset(const Taffy::Asset& asset);

        /**
         * @brief Extract and create shader modules from a Taffy asset
         * @param asset The Taffy asset containing shader data
         * @param meshShaderModule Output mesh shader module
         * @param fragmentShaderModule Output fragment shader module
         * @return true if successful
         */
        bool extractShadersFromAsset(const Taffy::Asset& asset,
            VkShaderModule& meshShaderModule,
            VkShaderModule& fragmentShaderModule);

        /**
         * @brief Initialize descriptor pool and layouts
         */
        void initializeDescriptorResources();

        /**
         * @brief Find suitable memory type for allocation
         * @param typeFilter Memory type filter
         * @param properties Required memory properties
         * @return Memory type index
         */
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

        /**
         * @brief Extract and compile a single shader from shader data
         * @param shader_data Raw shader chunk data
         * @param shader_index Index of the shader to extract
         * @param meshShaderModule Output mesh shader module
         * @param fragmentShaderModule Output fragment shader module
         * @return true if successful
         */
        bool extractAndCompileShader(const std::vector<uint8_t>& shader_data,
            uint32_t shader_index,
            VkShaderModule& meshShaderModule,
            VkShaderModule& fragmentShaderModule);
    };
    // =============================================================================
    // ENHANCED TAFFY MESH SHADER MANAGER WITH OVERLAY SUPPORT
    // =============================================================================


    class TaffyShaderTranspiler {
    public:
        enum class TargetAPI {
            Vulkan_SPIRV,    // Native SPIR-V for Vulkan
            Vulkan_GLSL,     // GLSL for Vulkan
            OpenGL_GLSL,     // GLSL for OpenGL
            DirectX_HLSL,    // HLSL for DirectX 12
            Metal_MSL,       // MSL for Metal
            WebGL_GLSL       // WebGL-compatible GLSL
        };

        /**
         * Transpile SPIR-V to target shader language
         */
        static std::string transpile_shader(const std::vector<uint32_t>& spirv,
            TargetAPI target,
            Taffy::ShaderChunk::Shader::ShaderStage stage) {
            try {
                switch (target) {
                case TargetAPI::Vulkan_SPIRV:
                    return ""; // Return empty - use raw SPIR-V

                case TargetAPI::Vulkan_GLSL:
                case TargetAPI::OpenGL_GLSL: {
                    spirv_cross::CompilerGLSL glsl_compiler(spirv);

                    // Configure GLSL options using set_common_options
                    spirv_cross::CompilerGLSL::Options glsl_options;
                    if (target == TargetAPI::Vulkan_GLSL) {
                        glsl_options.version = 460;
                        glsl_options.vulkan_semantics = true;
                    }
                    else {
                        glsl_options.version = 460;
                        glsl_options.vulkan_semantics = false;
                    }

                    // Enable mesh shader extensions if needed
                    if (stage == Taffy::ShaderChunk::Shader::ShaderStage::MeshShader ||
                        stage == Taffy::ShaderChunk::Shader::ShaderStage::TaskShader) {
                        glsl_options.version = 460; // Mesh shaders need GLSL 4.6+
                    }

                    glsl_compiler.set_common_options(glsl_options);
                    return glsl_compiler.compile();
                }

                case TargetAPI::DirectX_HLSL: {
                    spirv_cross::CompilerHLSL hlsl_compiler(spirv);

                    // For HLSL, use default options or set_common_options with basic GLSL options
                    spirv_cross::CompilerGLSL::Options basic_options;
                    basic_options.version = 450;
                    hlsl_compiler.set_common_options(basic_options);

                    return hlsl_compiler.compile();
                }

                case TargetAPI::Metal_MSL: {
                    spirv_cross::CompilerMSL msl_compiler(spirv);

                    // For MSL, use default options or set_common_options with basic GLSL options  
                    spirv_cross::CompilerGLSL::Options basic_options;
                    basic_options.version = 450;
                    msl_compiler.set_common_options(basic_options);

                    return msl_compiler.compile();
                }

                case TargetAPI::WebGL_GLSL: {
                    spirv_cross::CompilerGLSL webgl_compiler(spirv);

                    // Configure WebGL options
                    spirv_cross::CompilerGLSL::Options webgl_options;
                    webgl_options.version = 300;
                    webgl_options.es = true;
                    webgl_options.vulkan_semantics = false;
                    webgl_compiler.set_common_options(webgl_options);

                    return webgl_compiler.compile();
                }
                }
            }
            catch (const spirv_cross::CompilerError& e) {
                std::cerr << "SPIR-V Cross compilation failed: " << e.what() << std::endl;
            }

            return "";
        }

        /**
 * Get the best target API for current platform/engine
 */
        static TargetAPI get_preferred_target() {
#ifdef TREMOR_VULKAN
            return TargetAPI::Vulkan_SPIRV; // Use native SPIR-V for best performance
#elif defined(TREMOR_DIRECTX12)
            return TargetAPI::DirectX_HLSL;
#elif defined(TREMOR_METAL)
            return TargetAPI::Metal_MSL;
#elif defined(TREMOR_OPENGL)
            return TargetAPI::OpenGL_GLSL;
#elif defined(TREMOR_WEBGL)
            return TargetAPI::WebGL_GLSL;
#else
            return TargetAPI::Vulkan_GLSL; // Default fallback
#endif
        }
    };

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

    class TaffyMeshShaderPipeline;
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


    class VulkanResourceManager {
    private:
        VkDevice m_device;
        VkPhysicalDevice m_physicalDevice;
        VkPhysicalDeviceMemoryProperties m_memProperties;

        std::unordered_map<uint32_t, std::unique_ptr<VulkanTexture>> m_textures;
        std::atomic<uint32_t> m_nextTextureId{ 1 };

    public:
		VkDevice device() const { return m_device; }
		VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }

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

        VulkanDevice& m_device;

        // Constructor takes a device, surface, and creation info
        SwapChain(VulkanDevice& device, VkSurfaceKHR surface, const CreateInfo& createInfo) : m_device(device), m_surface(surface) {
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
            const CompileOptions& options = {});

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
            const std::string& entryPoint = "main", const ShaderCompiler::CompileOptions& options = {});
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
        ~VulkanBackend() override = default;

        bool hot_pink_enabled = true;

        // RenderBackend interface implementation
        bool initialize(SDL_Window* window) override;
        void shutdown() override;
        void beginFrame() override;
        void endFrame() override;

        // Resource creation interface
        uint32_t loadMeshFromFile(const std::string& filename);
        uint32_t createMaterialFromDesc(const MaterialDesc& desc);
        void addObjectToScene(uint32_t meshID, uint32_t materialID, const glm::mat4& transform);

        TextureHandle createTexture(const TextureDesc& desc);
        BufferHandle createBuffer(const BufferDesc& desc);
        ShaderHandle createShader(const ShaderDesc& desc);

        // Vulkan-specific getters
        VkDevice getDevice() const { return device; }
        VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
        VkQueue getGraphicsQueue() const { return graphicsQueue; }

        std::unique_ptr<TaffyOverlayManager> m_overlayManager;

        // Scene management
        void createEnhancedScene();
        void createTaffyScene();
        void createSceneLighting();
        void simpleColorCyclingTest();

        void initializeOverlayWorkflow();

        VkShaderModule loadShader(const std::string& filename);

        std::unique_ptr<TaffyMeshShaderManager> m_taffyMeshShaderManager;
        PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT_ = nullptr;

        std::chrono::steady_clock::time_point last_overlay_check_;
        const std::chrono::milliseconds overlay_check_interval_{ 1000 };

        // ADD THESE NEW METHODS FOR OVERLAY SUPPORT:
        void initializeOverlaySystem();
        void createDevelopmentOverlays();
        void loadTestAssetWithOverlays();
        void updateOverlaySystem();
        void createTestMasterAssetFromGLSL();
        void renderWithOverlays(VkCommandBuffer cmdBuffer);
        void demonstrateOverlayControls();


    private:
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

        // === RESOURCE CREATION METHODS ===

        bool createUniformBuffer();
        bool createLightBuffer();
        bool createMaterialBuffer();
        bool createCubeMesh() { return true; }
        bool createTestTexture();
        bool createDescriptorSetLayouts();
        bool createAndUpdateDescriptorSets(){}
        bool createMinimalMeshShaderPipeline();
        bool createGraphicsPipeline() { return true; }

        // === VERTEX DATA CREATION ===

        std::vector<BlinnPhongVertex> createCube();
        void createCubeRenderableObject();

        // === TAFFY ASSET METHODS ===

        void createTaffyMeshes();

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
            VkCommandPool commandPool, const ClusterConfig& config);
        ~VulkanClusteredRenderer() override;

        // Method declarations - implementations go to vk.cpp
        void createClusterGrid() override;
        bool initialize(Format colorFormat, Format depthFormat) override;
        void shutdown() override;
        uint32_t loadMesh(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices, const std::string& name = "") override;
        uint32_t createMaterial(const PBRMaterial& material) override;
        void render(RenderCommandBuffer* cmdBuffer, Camera* camera) override{}
        void updateGPUBuffers() override;
        void buildClusters(Camera* camera, Octree<RenderableObject>& octree);
        void updateLights(const std::vector<ClusterLight>& lights) override;
        void render(VkCommandBuffer cmdBuffer, Camera* camera);

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

        // Rendering configuration
        VkFormat* m_colorFormat;
        VkFormat* m_depthFormat;

        // Shader modules
        std::unique_ptr<ShaderModule> m_taskShader;
        std::unique_ptr<ShaderModule> m_meshShader;
        std::unique_ptr<ShaderModule> m_fragmentShader;
        std::unique_ptr<ShaderModule> m_debugTaskShader;
        std::unique_ptr<ShaderModule> m_debugMeshShader;

        // Pipeline resources
        std::unique_ptr<PipelineLayoutResource> m_pipelineLayout;
        std::unique_ptr<PipelineResource> m_pipeline;
        std::unique_ptr<PipelineResource> m_wireframePipeline;
        std::unique_ptr<PipelineResource> m_debugPipeline;
        std::unique_ptr<PipelineResource> m_testBufferPipeline;
        std::unique_ptr<PipelineResource> m_workingMeshPipeline;
        std::unique_ptr<PipelineLayoutResource> m_workingMeshPipelineLayout;

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
        bool createDefaultTextures();
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
            const ShaderCompiler::CompileOptions & options = {});

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