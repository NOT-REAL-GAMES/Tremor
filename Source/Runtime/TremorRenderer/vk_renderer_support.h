#pragma once

#include <array>

#include "../../../tremor_core.h"
#include "../../../tremor_graphics_platform.h"
#include "../TremorRHI/vk_rhi.h"

#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>

#include "include/quan.h"
#include "include/taffy.h"
#include "include/tools.h"
#include "include/asset.h"

namespace tremor::gfx {

using namespace tremor::taffy::tools;

class TaffyMeshShaderPipeline {
private:
    VkDevice device_;
    VkPhysicalDevice physical_device_;
    VkRenderPass render_pass_;

    VkShaderModule task_shader_module_ = VK_NULL_HANDLE;
    VkShaderModule mesh_shader_module_ = VK_NULL_HANDLE;
    VkShaderModule fragment_shader_module_ = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline_ = VK_NULL_HANDLE;

    uint32_t max_vertices_ = 0;
    uint32_t max_primitives_ = 0;

public:
    TaffyMeshShaderPipeline(VkDevice device, VkPhysicalDevice physical_device)
        : device_(device), physical_device_(physical_device) {
    }

    bool create_from_taffy_asset(const Taffy::Asset& asset) {
        std::cout << "Creating mesh shader pipeline from Taffy asset..." << std::endl;

        auto shader_data = asset.get_chunk_data(Taffy::ChunkType::SHDR);
        if (!shader_data) {
            std::cerr << "No shader data found" << std::endl;
            return false;
        }

        Taffy::ShaderChunk shader_header;
        if (shader_data->size() < sizeof(shader_header)) {
            std::cerr << "Invalid shader chunk size" << std::endl;
            return false;
        }
        std::memcpy(&shader_header, shader_data->data(), sizeof(shader_header));

        std::cout << "Found " << shader_header.shader_count << " shaders in asset" << std::endl;

        size_t offset = sizeof(shader_header);
        for (uint32_t i = 0; i < shader_header.shader_count; ++i) {
            if (!extract_and_compile_shader(*shader_data, i)) {
                std::cerr << "Failed to extract shader " << i << std::endl;
                return false;
            }
        }

        if (!create_pipeline_layout()) {
            std::cerr << "Failed to create pipeline layout" << std::endl;
            return false;
        }

        if (!create_graphics_pipeline()) {
            std::cerr << "Failed to create graphics pipeline" << std::endl;
            return false;
        }

        std::cout << "✓ Mesh shader pipeline created successfully!" << std::endl;
        return true;
    }

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
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);
        vkCmdDrawMeshTasksEXT(command_buffer, 1, 1, 1);
        std::cout << "✓ Mesh shader draw call submitted!" << std::endl;
    }

private:
    bool extract_and_compile_shader(const std::vector<uint8_t>& shader_data, uint32_t shader_index) {
        std::cout << "🔍 EXTRACTING SHADER " << shader_index << ":" << std::endl;

        const uint8_t* chunk_ptr = shader_data.data();
        size_t chunk_size = shader_data.size();

        Taffy::ShaderChunk header;
        std::memcpy(&header, chunk_ptr, sizeof(header));
        std::cout << "  Total shaders in chunk: " << header.shader_count << std::endl;

        if (shader_index >= header.shader_count) {
            std::cerr << "  ❌ Shader index out of range!" << std::endl;
            return false;
        }

        size_t shader_info_offset = sizeof(Taffy::ShaderChunk) + shader_index * sizeof(Taffy::ShaderChunk::Shader);
        std::cout << "  Shader info offset: " << shader_info_offset << std::endl;

        if (shader_info_offset + sizeof(Taffy::ShaderChunk::Shader) > chunk_size) {
            std::cerr << "  ❌ Shader info extends beyond chunk!" << std::endl;
            return false;
        }

        Taffy::ShaderChunk::Shader shader_info;
        std::memcpy(&shader_info, chunk_ptr + shader_info_offset, sizeof(shader_info));

        std::cout << "  Name hash: 0x" << std::hex << shader_info.name_hash << std::dec << std::endl;
        std::cout << "  Stage: " << static_cast<uint32_t>(shader_info.stage) << std::endl;
        std::cout << "  SPIR-V size: " << shader_info.spirv_size << " bytes" << std::endl;

        size_t spirv_data_start = sizeof(Taffy::ShaderChunk) + header.shader_count * sizeof(Taffy::ShaderChunk::Shader);
        size_t spirv_offset = spirv_data_start;

        for (uint32_t i = 0; i < shader_index; ++i) {
            size_t prev_shader_info_offset = sizeof(Taffy::ShaderChunk) + i * sizeof(Taffy::ShaderChunk::Shader);
            Taffy::ShaderChunk::Shader prev_shader;
            std::memcpy(&prev_shader, chunk_ptr + prev_shader_info_offset, sizeof(prev_shader));
            spirv_offset += prev_shader.spirv_size;
            std::cout << "  Skipping shader " << i << " SPIR-V: " << prev_shader.spirv_size << " bytes" << std::endl;
        }

        std::cout << "  This shader's SPIR-V offset: " << spirv_offset << std::endl;

        if (spirv_offset + shader_info.spirv_size > chunk_size) {
            std::cerr << "  ❌ SPIR-V data extends beyond chunk!" << std::endl;
            std::cerr << "    SPIR-V end: " << (spirv_offset + shader_info.spirv_size) << std::endl;
            std::cerr << "    Chunk size: " << chunk_size << std::endl;
            return false;
        }

        if (shader_info.spirv_size >= 4) {
            uint32_t magic;
            std::memcpy(&magic, chunk_ptr + spirv_offset, sizeof(magic));
            std::cout << "  SPIR-V magic: 0x" << std::hex << magic << std::dec;

            if (magic == 0x07230203) {
                std::cout << " ✅ VALID" << std::endl;
            } else {
                std::cout << " ❌ INVALID! Expected 0x07230203" << std::endl;
                return false;
            }
        }

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

        if (shader_info.stage == Taffy::ShaderChunk::Shader::ShaderStage::MeshShader) {
            mesh_shader_module_ = shaderModule;
            std::cout << "    → Stored as mesh shader module" << std::endl;
        } else if (shader_info.stage == Taffy::ShaderChunk::Shader::ShaderStage::Fragment) {
            fragment_shader_module_ = shaderModule;
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

        if (task_shader_module_ != VK_NULL_HANDLE) {
            VkPipelineShaderStageCreateInfo task_stage{};
            task_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            task_stage.stage = VK_SHADER_STAGE_TASK_BIT_NV;
            task_stage.module = task_shader_module_;
            task_stage.pName = "main";
            shader_stages.push_back(task_stage);
        }

        if (mesh_shader_module_ != VK_NULL_HANDLE) {
            VkPipelineShaderStageCreateInfo mesh_stage{};
            mesh_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            mesh_stage.stage = VK_SHADER_STAGE_MESH_BIT_NV;
            mesh_stage.module = mesh_shader_module_;
            mesh_stage.pName = "main";
            shader_stages.push_back(mesh_stage);
        }

        if (fragment_shader_module_ != VK_NULL_HANDLE) {
            VkPipelineShaderStageCreateInfo frag_stage{};
            frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            frag_stage.module = fragment_shader_module_;
            frag_stage.pName = "main";
            shader_stages.push_back(frag_stage);
        }

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.width = 800.0f;
        viewport.height = 600.0f;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = {};

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.attachmentCount = 1;
        color_blending.pAttachments = &color_blend_attachment;

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
        } else {
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
    std::unordered_map<std::string, std::unique_ptr<TaffyMeshShaderPipeline>> pipelines_;

public:
    TaffyMeshShaderManager(VkDevice device, VkPhysicalDevice physical_device)
        : device_(device), physical_device_(physical_device) {
        vkCmdDrawMeshTasksEXT_ = (PFN_vkCmdDrawMeshTasksEXT)
            vkGetDeviceProcAddr(device, "vkCmdDrawMeshTasksEXT");

        if (vkCmdDrawMeshTasksEXT_) {
            std::cout << "✓ Mesh shader manager initialized!" << std::endl;
        } else {
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

    bool load_taffy_asset(const std::string& filepath) {
        std::cout << "🔥 Loading Taffy mesh shader asset: " << filepath << std::endl;

        Taffy::Asset asset;
        if (!asset.load_from_file_safe(filepath)) {
            std::cerr << "Failed to load Taffy asset: " << filepath << std::endl;
            return false;
        }

        if (!asset.has_feature(Taffy::FeatureFlags::MeshShaders)) {
            std::cout << "Asset doesn't contain mesh shaders, using fallback" << std::endl;
            return false;
        }

        if (!asset.has_chunk(Taffy::ChunkType::SHDR)) {
            std::cout << "No shader chunk found in asset" << std::endl;
            return false;
        }

        auto pipeline = std::make_unique<TaffyMeshShaderPipeline>(device_, physical_device_);

        if (!pipeline->create_from_taffy_asset(asset)) {
            std::cerr << "Failed to create mesh shader pipeline from asset" << std::endl;
            return false;
        }

        pipelines_[filepath] = std::move(pipeline);

        std::cout << "🚀 Mesh shader pipeline created successfully for: " << filepath << std::endl;
        return true;
    }

    void render_asset(const std::string& filepath, VkCommandBuffer command_buffer) {
        auto it = pipelines_.find(filepath);
        if (it == pipelines_.end()) {
            std::cerr << "Pipeline not found for asset: " << filepath << std::endl;
            return;
        }

        it->second->render(command_buffer, vkCmdDrawMeshTasksEXT_);
    }

    const std::unordered_map<std::string, std::unique_ptr<TaffyMeshShaderPipeline>>& get_pipelines() const {
        return pipelines_;
    }
};

VkDescriptorSetLayout createMeshShaderDescriptorSetLayout(VkDevice device);

struct MeshShaderPushConstants {
    glm::mat4 mvp;
    uint32_t vertex_count;
    uint32_t primitive_count;
    uint32_t vertex_stride_floats;
    uint32_t index_offset_bytes;
    uint32_t overlay_flags;
    uint32_t overlay_data_offset;
    uint32_t meshlet_count;
    uint32_t meshlet_desc_offset_bytes;
    uint32_t meshlet_vertex_index_offset_bytes;
    uint32_t meshlet_primitive_index_offset_bytes;
    uint32_t instance_count;
    uint32_t instance_data_offset_bytes;
};

class TaffyOverlayManager {
public:
    static constexpr uint32_t OverlayFlag_DebugMeshlets = 1u << 0;
    static constexpr uint32_t FramesInFlight = 2;

    struct MeshAssetGPUData {
        VkBuffer vertexStorageBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexStorageMemory = VK_NULL_HANDLE;
        std::array<VkBuffer, FramesInFlight> instanceStorageBuffers{};
        std::array<VkDeviceMemory, FramesInFlight> instanceStorageMemories{};
        std::array<VkDescriptorSet, FramesInFlight> descriptorSets{};
        uint32_t vertexCount = 0;
        uint32_t primitiveCount = 0;
        uint32_t vertexStrideFloats = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t meshletCount = 0;
        uint32_t meshletDescOffset = 0;
        uint32_t meshletVertexIndexOffset = 0;
        uint32_t meshletPrimitiveIndexOffset = 0;
        std::array<uint32_t, FramesInFlight> instanceCapacities{};
        bool usesMeshShader = false;
    };

    TaffyOverlayManager(VkDevice device, VkPhysicalDevice physicalDevice,
        VkRenderPass renderPass, VkExtent2D swapchainExtent,
        VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB,
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT,
        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);

    ~TaffyOverlayManager();

    void setMeshletDebugColorsEnabled(bool enabled) {
        if (enabled) {
            meshShaderOverlayFlags_ |= OverlayFlag_DebugMeshlets;
        } else {
            meshShaderOverlayFlags_ &= ~OverlayFlag_DebugMeshlets;
        }
    }

    bool areMeshletDebugColorsEnabled() const {
        return (meshShaderOverlayFlags_ & OverlayFlag_DebugMeshlets) != 0;
    }

    void renderMeshAsset(const std::string& asset_path, VkCommandBuffer cmd, const glm::mat4& viewProj,
        const Vec3Q& renderOrigin = Vec3Q(), const Vec3Q& objectPosition = Vec3Q());
    void renderMeshAssetBatch(const std::string& asset_path, VkCommandBuffer cmd,
        const glm::mat4& viewProj, const std::vector<glm::mat4>& models);

    void load_master_asset(const std::string& master_path);
    void loadAssetWithOverlay(const std::string& master_path, const std::string& overlay_path);
    void clear_overlays(const std::string& master_path);
    void reloadAsset(const std::string& asset_path);
    void checkForPipelineUpdates();

    void updateSwapchainExtent(VkExtent2D newExtent) { swapchain_extent_ = newExtent; }
    void onSwapchainRecreated(VkRenderPass renderPass, VkExtent2D newExtent,
        VkFormat swapchainFormat, VkFormat depthFormat,
        VkSampleCountFlagBits sampleCount);

    void invalidatePipeline(const std::string& asset_path);

    VkDescriptorPool getDescriptorPool() const { return descriptorPool; }
    VkDescriptorSetLayout getMeshShaderDescriptorSetLayout() const { return meshShaderDescSetLayout; }
    VkExtent2D getSwapchainExtent() const { return swapchain_extent_; }
    void setActiveFrameIndex(uint32_t frameIndex) { activeFrameIndex_ = frameIndex % FramesInFlight; }

private:
    struct RenderStateCache {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceSize indexOffset = 0;
        VkExtent2D viewportExtent{ 0, 0 };
        bool viewportBound = false;
        bool scissorBound = false;
    };

    struct PipelineInfo {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkShaderModule taskShader = VK_NULL_HANDLE;
        VkShaderModule vertexShader = VK_NULL_HANDLE;
        VkShaderModule meshShader = VK_NULL_HANDLE;
        VkShaderModule fragmentShader = VK_NULL_HANDLE;
        std::string vertexShaderHash;
        std::string fragmentShaderHash;
    };

    VkDevice device_;
    VkPhysicalDevice physical_device_;
    VkRenderPass render_pass_;
    VkExtent2D swapchain_extent_;
    VkFormat swapchain_format_;
    VkFormat depth_format_;
    VkSampleCountFlagBits sample_count_;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout meshShaderDescSetLayout = VK_NULL_HANDLE;
    uint32_t activeFrameIndex_ = 0;

    std::unordered_map<std::string, std::unique_ptr<Taffy::Asset>> loaded_assets_;
    std::unordered_map<std::string, MeshAssetGPUData> gpu_data_cache_;
    std::unordered_map<std::string, PipelineInfo> pipeline_cache_;
    std::unordered_map<std::string, bool> pipeline_rebuild_flags_;
    std::unordered_set<std::string> failed_asset_loads_;
    std::unordered_map<std::string, std::string> applied_overlays_;
    uint32_t meshShaderOverlayFlags_ = 0;
    RenderStateCache renderStateCache_{};

    bool ensureAssetLoaded(const std::string& asset_path);
    PipelineInfo* getOrCreatePipeline(const std::string& asset_path);
    PipelineInfo* createPipelineForAsset(const std::string& asset_path);
    VkPipeline createMeshShaderPipeline(const PipelineInfo& pipelineInfo);
    VkPipeline createTraditionalPipeline(const PipelineInfo& pipelineInfo);
    void rebuildPipeline(const std::string& asset_path);
    void cleanupShaderModules(const PipelineInfo& pipelineInfo);
    void renderMeshAssetInternal(VkCommandBuffer cmd, VkPipeline meshPipeline,
        VkPipelineLayout pipelineLayout, const MeshAssetGPUData& gpuData, const glm::mat4& viewProj,
        const glm::mat4& model, uint32_t instanceCount = 0);
    MeshAssetGPUData uploadTaffyAsset(const Taffy::Asset& asset);
    bool ensureInstanceBufferCapacity(MeshAssetGPUData& gpuData, uint32_t instanceCount);
    void cleanupMeshAssetGPUData(MeshAssetGPUData& gpuData);
    bool extractShadersFromAsset(const Taffy::Asset& asset,
        VkShaderModule& vertexShaderModule,
        VkShaderModule& meshShaderModule,
        VkShaderModule& fragmentShaderModule);
    void initializeDescriptorResources();
    void createDescriptorPool(size_t maxSets);
    void recreateDescriptorPool();
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool extractAndCompileShader(const std::vector<uint8_t>& shader_data,
        uint32_t shader_index,
        VkShaderModule& vertexShaderModule,
        VkShaderModule& meshShaderModule,
        VkShaderModule& fragmentShaderModule);
};

class TaffyShaderTranspiler {
public:
    enum class TargetAPI {
        Vulkan_SPIRV,
        Vulkan_GLSL,
        OpenGL_GLSL,
        DirectX_HLSL,
        Metal_MSL,
        WebGL_GLSL
    };

    static std::string transpile_shader(const std::vector<uint32_t>& spirv,
        TargetAPI target,
        Taffy::ShaderChunk::Shader::ShaderStage stage) {
        try {
            switch (target) {
            case TargetAPI::Vulkan_SPIRV:
                return "";

            case TargetAPI::Vulkan_GLSL:
            case TargetAPI::OpenGL_GLSL: {
                spirv_cross::CompilerGLSL glsl_compiler(spirv);

                spirv_cross::CompilerGLSL::Options glsl_options;
                if (target == TargetAPI::Vulkan_GLSL) {
                    glsl_options.version = 460;
                    glsl_options.vulkan_semantics = true;
                } else {
                    glsl_options.version = 460;
                    glsl_options.vulkan_semantics = false;
                }

                if (stage == Taffy::ShaderChunk::Shader::ShaderStage::MeshShader ||
                    stage == Taffy::ShaderChunk::Shader::ShaderStage::TaskShader) {
                    glsl_options.version = 460;
                }

                glsl_compiler.set_common_options(glsl_options);
                return glsl_compiler.compile();
            }

            case TargetAPI::DirectX_HLSL: {
                spirv_cross::CompilerHLSL hlsl_compiler(spirv);
                spirv_cross::CompilerGLSL::Options basic_options;
                basic_options.version = 450;
                hlsl_compiler.set_common_options(basic_options);
                return hlsl_compiler.compile();
            }

            case TargetAPI::Metal_MSL: {
                spirv_cross::CompilerMSL msl_compiler(spirv);
                spirv_cross::CompilerGLSL::Options basic_options;
                basic_options.version = 450;
                msl_compiler.set_common_options(basic_options);
                return msl_compiler.compile();
            }

            case TargetAPI::WebGL_GLSL: {
                spirv_cross::CompilerGLSL webgl_compiler(spirv);
                spirv_cross::CompilerGLSL::Options webgl_options;
                webgl_options.version = 300;
                webgl_options.es = true;
                webgl_options.vulkan_semantics = false;
                webgl_compiler.set_common_options(webgl_options);
                return webgl_compiler.compile();
            }
            }
        } catch (const spirv_cross::CompilerError& e) {
            std::cerr << "SPIR-V Cross compilation failed: " << e.what() << std::endl;
        }

        return "";
    }

    static TargetAPI get_preferred_target() {
#ifdef TREMOR_VULKAN
        return TargetAPI::Vulkan_SPIRV;
#elif defined(TREMOR_DIRECTX12)
        return TargetAPI::DirectX_HLSL;
#elif defined(TREMOR_METAL)
        return TargetAPI::Metal_MSL;
#elif defined(TREMOR_OPENGL)
        return TargetAPI::OpenGL_GLSL;
#elif defined(TREMOR_WEBGL)
        return TargetAPI::WebGL_GLSL;
#else
        return TargetAPI::Vulkan_GLSL;
#endif
    }
};

} // namespace tremor::gfx
