#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Important for Vulkan depth range

#include "vk.h"
#include "quan.h"
#include "renderer/taffy_integration.h"
#include "renderer/sdf_text_renderer.h"
#include "renderer/ui_renderer.h"
#include "renderer/sequencer_ui.h"
#include "tools.h"
#include "asset.h"
#include "overlay.h"
#include "taffy_font_tools.h"
#include "taffy_audio_tools.h"
#include <iomanip>

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
            //Logger::get().error("Failed to allocate transfer command buffer: {}", (int)result);
            return;
        }

        // Begin recording
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
        if (result != VK_SUCCESS) {
            //Logger::get().error("Failed to begin command buffer: {}", (int)result);
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
            //Logger::get().error("Failed to end command buffer: {}", (int)result);
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
            //Logger::get().error("Failed to submit transfer command buffer: {}", (int)result);
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            return;
        }

        // Wait for the transfer to complete
        result = vkQueueWaitIdle(queue);
        if (result != VK_SUCCESS) {
            //Logger::get().error("Failed to wait for queue idle: {}", (int)result);
        }

        // Free the temporary command buffer
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

        //Logger::get().info("Buffer copy completed successfully: {} bytes", size);
    }
    catch (const std::exception& e) {
        //Logger::get().error("Exception during buffer copy: {}", e.what());
    }
}


namespace tremor::gfx {

    VulkanBackend::VulkanBackend() {
        static int instanceCount = 0;
        instanceCount++;
        //Logger::get().critical("VulkanBackend CONSTRUCTOR called!");
        //Logger::get().critical("  This is instance #{}", instanceCount);
        //Logger::get().critical("  VulkanBackend this pointer: {}", (void*)this);
        
        if (instanceCount > 1) {
            //Logger::get().critical("WARNING: Multiple VulkanBackend instances created!");
            //Logger::get().critical("Stack trace would be helpful here...");
        }
    }

    VulkanBackend::~VulkanBackend() {
        // Destructor implementation - needed because of forward declaration of SDFTextRenderer
        // The default destructor is fine, but it needs to be in the .cpp file
    }

    // Helper function to create descriptor set layout for mesh shaders
    VkDescriptorSetLayout createMeshShaderDescriptorSetLayout(VkDevice device) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        // Binding 0: Vertex/Geometry data as storage buffer
        VkDescriptorSetLayoutBinding vertexStorageBinding{};
        vertexStorageBinding.binding = 0;
        vertexStorageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vertexStorageBinding.descriptorCount = 1;
        vertexStorageBinding.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;  // Both mesh and fragment shaders need this
        vertexStorageBinding.pImmutableSamplers = nullptr;
        bindings.push_back(vertexStorageBinding);

        // Add other bindings if needed in the future (textures, uniforms, etc.)
        // For example:
        // Binding 1: Material data
        // VkDescriptorSetLayoutBinding materialBinding{};
        // materialBinding.binding = 1;
        // materialBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        // materialBinding.descriptorCount = 1;
        // materialBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        // materialBinding.pImmutableSamplers = nullptr;
        // bindings.push_back(materialBinding);

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout descriptorSetLayout;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create mesh shader descriptor set layout!");
        }

        return descriptorSetLayout;
    }

    // TaffyOverlayManager with merged TaffyAssetRenderer functionality and pipeline management
    
        TaffyOverlayManager::TaffyOverlayManager(VkDevice device, VkPhysicalDevice physicalDevice,
            VkRenderPass renderPass, VkExtent2D swapchainExtent,
            VkFormat swapchainFormat, VkFormat depthFormat, VkSampleCountFlagBits sampleCount)
            : device_(device), physical_device_(physicalDevice),
            render_pass_(renderPass), swapchain_extent_(swapchainExtent),
            swapchain_format_(swapchainFormat), depth_format_(depthFormat), sample_count_(sampleCount) {

            // Initialize descriptor pool and layouts for mesh shader rendering
            initializeDescriptorResources();
        }

        TaffyOverlayManager::~TaffyOverlayManager() {
            // Clean up all pipelines
            
        }

        // Simplified render method - just pass asset path and command buffer
        void TaffyOverlayManager::renderMeshAsset(const std::string& asset_path, VkCommandBuffer cmd, const glm::mat4& viewProj) {
            // Extract camera position from view matrix (inverse of last column)
            glm::vec3 camPos = -glm::vec3(viewProj[3]);
            
            //std::cout << "\n=== renderMeshAsset called for: " << asset_path << " ===" << std::endl;
            //std::cout << "Camera position from viewProj: (" << camPos.x << ", " << camPos.y << ", " << camPos.z << ")" << std::endl;
            
            // Ensure asset is loaded
            if (!ensureAssetLoaded(asset_path)) {
                std::cerr << "Failed to load asset: " << asset_path << std::endl;
                return;
            }

            // Get or create pipeline for this asset
            PipelineInfo* pipeline = getOrCreatePipeline(asset_path);
            if (!pipeline) {
                std::cerr << "Failed to create pipeline for: " << asset_path << std::endl;
                return;
            }

            // Get GPU data for this asset
            auto gpuDataIt = gpu_data_cache_.find(asset_path);
            if (gpuDataIt == gpu_data_cache_.end()) {
                std::cerr << "No GPU data for asset: " << asset_path << std::endl;
                return;
            }

            const MeshAssetGPUData& gpuData = gpuDataIt->second;

            // Now render using the cached pipeline and GPU data
            renderMeshAssetInternal(cmd, pipeline->pipeline, pipeline->layout, gpuData, viewProj);
        }

        // Asset loading and management
        void TaffyOverlayManager::load_master_asset(const std::string& master_path) {
            ensureAssetLoaded(master_path);
        }

        void TaffyOverlayManager::loadAssetWithOverlay(const std::string& master_path, const std::string& overlay_path) {
            // Check if this exact overlay is already applied to this asset
            auto overlayIt = applied_overlays_.find(master_path);
            if (overlayIt != applied_overlays_.end() && overlayIt->second == overlay_path) {
                //Logger::get().info("Overlay {} is already applied to {}, skipping redundant load", overlay_path, master_path);
                return;
            }
            
            // First ensure the master asset is loaded
            if (!ensureAssetLoaded(master_path)){
                //Logger::get().critical("Couldn't load asset: {}",master_path);
                return;
            }
            
            // IMPORTANT: Create a working copy to ensure we never modify the cached master
            // This allows us to apply different overlays without accumulating changes
            auto working_copy = std::make_unique<Taffy::Asset>(*loaded_assets_[master_path]);
            
            // Load the overlay
            Taffy::Overlay overlay;
            if (!overlay.load_from_file(overlay_path)) {
                std::cerr << "Failed to load overlay: " << overlay_path << std::endl;
                return;
            }
            
            // Apply the overlay to the WORKING COPY, not the master
            if (!overlay.apply_to_asset(*working_copy)) {
                std::cerr << "Failed to apply overlay to asset" << std::endl;
                return;
            }
            
            //Logger::get().info("Overlay applied to working copy of {}", master_path);
            //Logger::get().info("Original asset remains unchanged");
            
            // IMPORTANT: Replace the loaded asset with the working copy so pipeline creation uses it
            loaded_assets_[master_path] = std::move(working_copy);
            //Logger::get().info("Working copy is now the active asset for {}", master_path);
            
            // Re-upload the modified asset to GPU
            MeshAssetGPUData gpuData = uploadTaffyAsset(*loaded_assets_[master_path]);
            if (!gpuData.vertexStorageBuffer) {
                std::cerr << "Failed to re-upload asset with overlay to GPU" << std::endl;
                return;
            }
            
            // Wait for GPU to finish before cleaning up resources
            vkDeviceWaitIdle(device_);
            
            // Clean up old GPU data before replacing
            auto oldDataIt = gpu_data_cache_.find(master_path);
            if (oldDataIt != gpu_data_cache_.end()) {
                // Clean up old buffers
                if (oldDataIt->second.vertexStorageBuffer) {
                    vkDestroyBuffer(device_, oldDataIt->second.vertexStorageBuffer, nullptr);
                }
                if (oldDataIt->second.vertexStorageMemory) {
                    vkFreeMemory(device_, oldDataIt->second.vertexStorageMemory, nullptr);
                }
                // Note: Descriptor sets are automatically returned to pool when freed
                // The new uploadTaffyAsset call will allocate a fresh descriptor set
            }
            
            // Update the GPU data cache with the new data
            gpu_data_cache_[master_path] = gpuData;
            
            // Mark pipeline for rebuild to use new vertex data
            invalidatePipeline(master_path);
            
            // Track that this overlay is now applied
            applied_overlays_[master_path] = overlay_path;
            
            std::cout << "Successfully applied overlay " << overlay_path << " to " << master_path << std::endl;
        }

        void TaffyOverlayManager::reloadAsset(const std::string& asset_path) {
            //Logger::get().info("Reloading asset: {}", asset_path);
            
            // Clear any overlay tracking since we're reloading from disk
            applied_overlays_.erase(asset_path);
            
            // Wait for GPU to finish
            vkDeviceWaitIdle(device_);
            
            // Clean up old GPU data
            auto gpuDataIt = gpu_data_cache_.find(asset_path);
            if (gpuDataIt != gpu_data_cache_.end()) {
                if (gpuDataIt->second.vertexStorageBuffer) {
                    vkDestroyBuffer(device_, gpuDataIt->second.vertexStorageBuffer, nullptr);
                }
                if (gpuDataIt->second.vertexStorageMemory) {
                    vkFreeMemory(device_, gpuDataIt->second.vertexStorageMemory, nullptr);
                }
                gpu_data_cache_.erase(gpuDataIt);
            }
            
            // Remove from loaded assets cache
            loaded_assets_.erase(asset_path);
            
            // Invalidate pipeline
            invalidatePipeline(asset_path);
            
            // Reload the asset
            if (!ensureAssetLoaded(asset_path)) {
                //Logger::get().error("Failed to reload asset: {}", asset_path);
                return;
            }
            
            //Logger::get().info("Asset reloaded successfully");
        }

        void TaffyOverlayManager::clear_overlays(const std::string& master_path) {
            if(applied_overlays_.empty()){
                //Logger::get().info("Asset {} didn't have any overlays! Skipping clear...",master_path);
                return;
            }

            //Logger::get().info("Clearing overlays for: {}", master_path);
            

            // Clear the overlay tracking
            applied_overlays_.erase(master_path);
            
            // Reload the original asset from disk
            //Logger::get().info("Reloading original asset from disk: {}", master_path);
            
            // Remove current (possibly modified) asset
            auto assetIt = loaded_assets_.find(master_path);
            if (assetIt != loaded_assets_.end()) {
                loaded_assets_.erase(assetIt);
            }
            
            // Force reload from disk
            if (!ensureAssetLoaded(master_path)) {
                //Logger::get().error("Failed to reload original asset: {}", master_path);
                return;
            }
            
            // Re-upload the fresh asset to GPU
            MeshAssetGPUData gpuData = uploadTaffyAsset(*loaded_assets_[master_path]);
            if (!gpuData.vertexStorageBuffer) {
                //Logger::get().error("Failed to re-upload original asset to GPU: {}", master_path);
                return;
            }
            
            // Clean up the old overlay GPU data
            auto oldDataIt = gpu_data_cache_.find(master_path);
            if (oldDataIt != gpu_data_cache_.end()) {
                // Wait for GPU to finish before cleaning up
                vkDeviceWaitIdle(device_);
                
                // Clean up old buffers
                if (oldDataIt->second.vertexStorageBuffer) {
                    vkDestroyBuffer(device_, oldDataIt->second.vertexStorageBuffer, nullptr);
                }
                if (oldDataIt->second.vertexStorageMemory) {
                    vkFreeMemory(device_, oldDataIt->second.vertexStorageMemory, nullptr);
                }
                // Note: We're reusing the same descriptor set to avoid pool exhaustion
            }
            
            // Update the GPU data cache with the fresh upload of the original
            gpu_data_cache_[master_path] = gpuData;
            
            //Logger::get().info("Overlays cleared - original asset restored");
            invalidatePipeline(master_path);

            return;
            
        }

        // Check if pipeline needs rebuild (e.g., after overlay changes)
        void TaffyOverlayManager::checkForPipelineUpdates() {
            for (auto& [path, needsRebuild] : pipeline_rebuild_flags_) {
                if (needsRebuild) {
                    rebuildPipeline(path);
                    needsRebuild = false;
                }
            }
        }

        

        
        // Storage for loaded assets and their resources

        bool TaffyOverlayManager::ensureAssetLoaded(const std::string& asset_path) {
            // Check if already loaded
            if (loaded_assets_.find(asset_path) != loaded_assets_.end()) {
                //Logger::get().info("Asset already loaded: {}", asset_path);
                //Logger::get().info("  TaffyOverlayManager instance: {}", (void*)this);
                return true;
            }
            
            //Logger::get().info("Loading new asset: {}", asset_path);
            //Logger::get().info("  TaffyOverlayManager instance: {}", (void*)this);

            // Load the asset
            auto asset = std::make_unique<Taffy::Asset>();
            if (!asset->load_from_file_safe(asset_path)) {
                std::cerr << "Failed to load Taffy asset: " << asset_path << std::endl;
                return false;
            }

            // Upload to GPU
            MeshAssetGPUData gpuData = uploadTaffyAsset(*asset);
            if (!gpuData.vertexStorageBuffer) {
                std::cerr << "Failed to upload asset to GPU: " << asset_path << std::endl;
                return false;
            }

            // Store the loaded asset and GPU data
            // IMPORTANT: This is the MASTER copy - never modify it directly!
            // Always create working copies when applying overlays
            loaded_assets_[asset_path] = std::move(asset);
            gpu_data_cache_[asset_path] = gpuData;
            
            // std::cout << "Asset loaded successfully: " << asset_path << std::endl;
            //Logger::get().info("  Loaded assets count: {}", loaded_assets_.size());
            //Logger::get().info("  GPU data cache count: {}", gpu_data_cache_.size());
            //Logger::get().info("  Vertex storage buffer: {}", (void*)gpuData.vertexStorageBuffer);

            return true;
        }

        TaffyOverlayManager::PipelineInfo* TaffyOverlayManager::getOrCreatePipeline(const std::string& asset_path) {
            // Check if pipeline exists
            auto it = pipeline_cache_.find(asset_path);
            if (it != pipeline_cache_.end()) {
                //Logger::get().info("Reusing cached pipeline for: {}", asset_path);
                //Logger::get().info("  Cached pipeline: {}", (void*)it->second.pipeline);
                //Logger::get().info("  Cached layout: {}", (void*)it->second.layout);
                //Logger::get().info("  Pipeline cache size: {}", pipeline_cache_.size());
                return &it->second;
            }
            //Logger::get().info("Creating new pipeline for: {}", asset_path);
            //Logger::get().info("  TaffyOverlayManager instance: {}", (void*)this);
            //Logger::get().info("  Pipeline cache size before: {}", pipeline_cache_.size());

            // Create new pipeline for this asset
            return createPipelineForAsset(asset_path);
        }

        TaffyOverlayManager::PipelineInfo* TaffyOverlayManager::createPipelineForAsset(const std::string& asset_path) {
            auto assetIt = loaded_assets_.find(asset_path);
            if (assetIt == loaded_assets_.end()) {
                return nullptr;
            }

            const Taffy::Asset& asset = *assetIt->second;
            PipelineInfo pipelineInfo;

            // Extract shaders from asset
            if (!extractShadersFromAsset(asset, pipelineInfo.meshShader, pipelineInfo.fragmentShader)) {
                std::cerr << "Failed to extract shaders from asset: " << asset_path << std::endl;
                return nullptr;
            }

            // Create pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &meshShaderDescSetLayout;

            // Push constants for mesh and fragment shaders
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pushConstantRange.offset = 0;
            // Data-driven mesh shader now expects MVP matrix + metadata (80 bytes total)
            pushConstantRange.size = sizeof(MeshShaderPushConstants); // MVP (64) + 4 uint32_t (16) = 80 bytes
            pipelineLayoutInfo.pushConstantRangeCount = 1;
            pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

            if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineInfo.layout) != VK_SUCCESS) {
                std::cerr << "Failed to create pipeline layout" << std::endl;
                cleanupShaderModules(pipelineInfo);
                return nullptr;
            }

            // Create pipeline
            pipelineInfo.pipeline = createMeshShaderPipeline(pipelineInfo);
            if (!pipelineInfo.pipeline) {
                vkDestroyPipelineLayout(device_, pipelineInfo.layout, nullptr);
                cleanupShaderModules(pipelineInfo);
                return nullptr;
            }

            // Store in cache
            pipeline_cache_[asset_path] = pipelineInfo;
            return &pipeline_cache_[asset_path];
        }

        VkPipeline TaffyOverlayManager::createMeshShaderPipeline(const PipelineInfo& pipelineInfo) {
            // Shader stages
            std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

            // Task shader (optional)
            if (pipelineInfo.taskShader) {
                VkPipelineShaderStageCreateInfo taskStageInfo{};
                taskStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                taskStageInfo.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
                taskStageInfo.module = pipelineInfo.taskShader;
                taskStageInfo.pName = "main";
                shaderStages.push_back(taskStageInfo);
            }

            // Mesh shader
            VkPipelineShaderStageCreateInfo meshStageInfo{};
            meshStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            meshStageInfo.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
            meshStageInfo.module = pipelineInfo.meshShader;
            meshStageInfo.pName = "main";
            shaderStages.push_back(meshStageInfo);

            // Fragment shader
            VkPipelineShaderStageCreateInfo fragStageInfo{};
            fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragStageInfo.module = pipelineInfo.fragmentShader;
            fragStageInfo.pName = "main";
            shaderStages.push_back(fragStageInfo);

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
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; // Disable culling for debugging
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            // Multisampling
            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = sample_count_;

            // Depth testing
            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE; // Disable depth test for debugging
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            depthStencil.depthBoundsTestEnable = VK_FALSE;
            depthStencil.stencilTestEnable = VK_FALSE;

            // Color blending
            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

            // Dynamic state
            std::vector<VkDynamicState> dynamicStates = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };

            VkPipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
            dynamicState.pDynamicStates = dynamicStates.data();

            // Setup for dynamic rendering if render pass is null
            VkPipelineRenderingCreateInfo renderingInfo{};
            
            if (render_pass_ == VK_NULL_HANDLE) {
                //Logger::get().info("Creating mesh shader pipeline for dynamic rendering");
                //Logger::get().info("  Color format: {}", (int)swapchain_format_);
                //Logger::get().info("  Depth format: {}", (int)depth_format_);
                renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &swapchain_format_;
                renderingInfo.depthAttachmentFormat = depth_format_;
                renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
            } else {
                //Logger::get().info("Creating mesh shader pipeline for traditional render pass");
            }

            // Create pipeline
            VkGraphicsPipelineCreateInfo ppInfo{};
            ppInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            if (render_pass_ == VK_NULL_HANDLE) {
                ppInfo.pNext = &renderingInfo;  // Chain the rendering info for dynamic rendering
            }
            ppInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
            ppInfo.pStages = shaderStages.data();
            ppInfo.pVertexInputState = nullptr;  // Not used for mesh shaders
            ppInfo.pInputAssemblyState = nullptr; // Not used for mesh shaders
            ppInfo.pViewportState = &viewportState;
            ppInfo.pRasterizationState = &rasterizer;
            ppInfo.pMultisampleState = &multisampling;
            ppInfo.pDepthStencilState = &depthStencil;
            ppInfo.pColorBlendState = &colorBlending;
            ppInfo.pDynamicState = &dynamicState;
            ppInfo.layout = pipelineInfo.layout;
            ppInfo.renderPass = render_pass_;
            ppInfo.subpass = 0;
            ppInfo.basePipelineHandle = VK_NULL_HANDLE;

            VkPipeline pipeline;
            VkResult pipelineResult = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ppInfo, nullptr, &pipeline);
            if (pipelineResult != VK_SUCCESS) {
                std::cerr << "Failed to create graphics pipeline! Error: " << pipelineResult << std::endl;
                return VK_NULL_HANDLE;
            }

            //Logger::get().info("Mesh shader pipeline created successfully!");
            return pipeline;
        }

        void TaffyOverlayManager::invalidatePipeline(const std::string& asset_path) {
            pipeline_rebuild_flags_[asset_path] = true;
        }

        void TaffyOverlayManager::rebuildPipeline(const std::string& asset_path) {
            // Clean up old pipeline
            auto it = pipeline_cache_.find(asset_path);
            if (it != pipeline_cache_.end()) {
                vkDestroyPipeline(device_, it->second.pipeline, nullptr);
                vkDestroyPipelineLayout(device_, it->second.layout, nullptr);
                cleanupShaderModules(it->second);
                pipeline_cache_.erase(it);
            }

            // Create new pipeline
            createPipelineForAsset(asset_path);
        }

        void TaffyOverlayManager::cleanupShaderModules(const PipelineInfo& pipelineInfo) {
            if (pipelineInfo.taskShader) {
                vkDestroyShaderModule(device_, pipelineInfo.taskShader, nullptr);
            }
            if (pipelineInfo.meshShader) {
                vkDestroyShaderModule(device_, pipelineInfo.meshShader, nullptr);
            }
            if (pipelineInfo.fragmentShader) {
                vkDestroyShaderModule(device_, pipelineInfo.fragmentShader, nullptr);
            }
        }

        void TaffyOverlayManager::renderMeshAssetInternal(VkCommandBuffer cmd, VkPipeline meshPipeline,
            VkPipelineLayout pipelineLayout, const MeshAssetGPUData& gpuData, const glm::mat4& viewProj) {
            if (!gpuData.usesMeshShader) {
                std::cerr << "⚠️  Asset doesn't use mesh shaders!" << std::endl;
                return;
            }

            // Bind mesh shader pipeline
            //Logger::get().info("Binding mesh shader pipeline");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

            // Set viewport
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(swapchain_extent_.width);
            viewport.height = static_cast<float>(swapchain_extent_.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            //Logger::get().info("Viewport: {}x{} at ({}, {})", viewport.width, viewport.height, viewport.x, viewport.y);
            //Logger::get().info("Depth range: {} to {}", viewport.minDepth, viewport.maxDepth);
            //Logger::get().info("Swapchain extent: {}x{}", swapchain_extent_.width, swapchain_extent_.height);
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            // Set scissor
            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = swapchain_extent_;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // Bind descriptor set with vertex storage buffer
            if (gpuData.descriptorSet == VK_NULL_HANDLE) {
                //Logger::get().error("Descriptor set is null!");
                return;
            }
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout, 0, 1, &gpuData.descriptorSet, 0, nullptr);

            // Set push constants
            MeshShaderPushConstants pushConstants{};
            pushConstants.mvp = viewProj;  // Use the view-projection matrix directly
            pushConstants.vertex_count = gpuData.vertexCount;
            pushConstants.primitive_count = gpuData.primitiveCount;
            pushConstants.vertex_stride_floats = gpuData.vertexStrideFloats;
            pushConstants.index_offset_bytes = gpuData.indexOffset;

            //Logger::get().info("Push constants: vertices={}, primitives={}, stride={}",
            //    pushConstants.vertex_count,
            //    pushConstants.primitive_count,
            //    pushConstants.vertex_stride_floats);

            // Push the full structure with MVP matrix
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_MESH_BIT_EXT,
                0, sizeof(pushConstants), &pushConstants);

            // Draw with mesh shader
            //Logger::get().info("Drawing mesh shader with 1x1x1 workgroups");
            
            // Ensure vkCmdDrawMeshTasksEXT is available
            if (!vkCmdDrawMeshTasksEXT) {
                //Logger::get().error("vkCmdDrawMeshTasksEXT is not available! Mesh shader extension not loaded properly.");
                return;
            }
            
            // Set push constants for mesh shader
            struct MeshPushConstantsData {
                glm::mat4 mvp;
                uint32_t vertex_count;
                uint32_t primitive_count;
                uint32_t vertex_stride_floats;
                uint32_t index_offset_bytes;
                uint32_t overlay_flags;
                uint32_t overlay_data_offset;
            } meshPushData;
            
            meshPushData.mvp = viewProj;
            meshPushData.vertex_count = gpuData.vertexCount;
            meshPushData.primitive_count = gpuData.primitiveCount;
            meshPushData.vertex_stride_floats = gpuData.vertexStrideFloats;
            meshPushData.index_offset_bytes = gpuData.indexOffset;
            meshPushData.overlay_flags = 0; // Overlays will be handled by shader replacement
            meshPushData.overlay_data_offset = 0;
                        
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_MESH_BIT_EXT,
                0, sizeof(meshPushData), &meshPushData);
            
            vkCmdDrawMeshTasksEXT(cmd, 3, 1, 1);  // 1x1x1 workgroups
        }

        TaffyOverlayManager::MeshAssetGPUData TaffyOverlayManager::uploadTaffyAsset(const Taffy::Asset& asset) {
            std::cout << "🚀 Starting uploadTaffyAsset..." << std::endl;
            MeshAssetGPUData gpuData{};

            // Get geometry chunk data
            auto geomData = asset.get_chunk_data(Taffy::ChunkType::GEOM);
            if (!geomData) {
                std::cerr << "❌ No geometry chunk found!" << std::endl;
                return gpuData;
            }
            std::cout << "✅ Found GEOM chunk, size: " << geomData->size() << " bytes" << std::endl;

            // Parse geometry header
            Taffy::GeometryChunk geomHeader;
            std::memcpy(&geomHeader, geomData->data(), sizeof(geomHeader));

            std::cout << "📊 Geometry info:" << std::endl;
            std::cout << "  Vertex count: " << geomHeader.vertex_count << std::endl;
            std::cout << "  Vertex stride: " << geomHeader.vertex_stride << " bytes" << std::endl;
            std::cout << "  Vertex format: 0x" << std::hex << static_cast<uint32_t>(geomHeader.vertex_format) << std::dec << std::endl;
            std::cout << "  Render mode value: " << geomHeader.render_mode << std::endl;
            std::cout << "  Render mode: " << (geomHeader.render_mode == Taffy::GeometryChunk::MeshShader ? "Mesh Shader" : "Traditional") << std::endl;
            std::cout << "  MeshShader enum value: " << Taffy::GeometryChunk::MeshShader << std::endl;
            
            // Check if this is Vec3Q format
            bool isVec3Q = asset.has_feature(Taffy::FeatureFlags::QuantizedCoords); // Check Vec3Q flag
            std::cout << "  Uses Vec3Q: " << (isVec3Q ? "Yes" : "No") << std::endl;

            // Check if this uses mesh shaders
            // TEMPORARY: Force mesh shader usage for assets with "data-driven mesh shader" in description
            if (asset.get_description().find("data-driven mesh shader") != std::string::npos) {
                gpuData.usesMeshShader = true;
                std::cout << "🔧 FORCING mesh shader mode for data-driven mesh shader asset" << std::endl;
            } else {
                gpuData.usesMeshShader = (geomHeader.render_mode == Taffy::GeometryChunk::MeshShader);
            }
            
            std::cout << "🎨 DEBUG: Render mode check - header value: " << geomHeader.render_mode 
                      << ", MeshShader enum: " << Taffy::GeometryChunk::MeshShader 
                      << ", uses mesh shader: " << gpuData.usesMeshShader << std::endl;

            if (gpuData.usesMeshShader) {
                std::cout << "🔧 Creating storage buffer for mesh shader..." << std::endl;

                // Calculate vertex data size and offset
                size_t vertexDataOffset = sizeof(Taffy::GeometryChunk);
                size_t vertexDataSize = geomHeader.vertex_count * geomHeader.vertex_stride;
                
                // Calculate index data size and offset
                size_t indexDataSize = geomHeader.index_count * sizeof(uint32_t);
                size_t indexDataOffset = vertexDataOffset + vertexDataSize;
                size_t totalBufferSize = vertexDataSize + indexDataSize;

                //std::cout << "  GeometryChunk size: " << sizeof(Taffy::GeometryChunk) << " bytes" << std::endl;
                //std::cout << "  Vertex data offset: " << vertexDataOffset << " bytes" << std::endl;
                //std::cout << "  Vertex data size: " << vertexDataSize << " bytes" << std::endl;
                //std::cout << "  Index data offset: " << indexDataOffset << " bytes" << std::endl;
                //std::cout << "  Index data size: " << indexDataSize << " bytes" << std::endl;
                //std::cout << "  Total buffer size: " << totalBufferSize << " bytes" << std::endl;
                //std::cout << "  Total chunk size: " << geomData->size() << " bytes" << std::endl;

                // Validate data bounds
                if (vertexDataOffset + vertexDataSize > geomData->size()) {
                    std::cerr << "❌ Vertex data extends beyond chunk!" << std::endl;
                    return gpuData;
                }
                
                // Validate index data bounds if indices are present
                if (geomHeader.index_count > 0 && indexDataOffset + indexDataSize > geomData->size()) {
                    std::cerr << "❌ Index data extends beyond chunk!" << std::endl;
                    return gpuData;
                }

                const uint8_t* vertexData = geomData->data() + vertexDataOffset;
                /*
                // Debug: print vertex data
                if (geomHeader.vertex_count > 0) {
                    std::cout << "🔍 Vertex stride: " << geomHeader.vertex_stride << " bytes" << std::endl;
                    
                    // Print all three vertices
                    for (int v = 0; v < 3 && v < geomHeader.vertex_count; v++) {
                        std::cout << "📍 Vertex " << v << " raw bytes (full vertex):" << std::endl;
                        const uint8_t* vertexStart = vertexData + (v * geomHeader.vertex_stride);
                        
                        for (int i = 0; i < geomHeader.vertex_stride; i++) {
                            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                                      << static_cast<int>(vertexStart[i]) << " ";
                            if ((i + 1) % 8 == 0) std::cout << std::endl;
                        }
                        std::cout << std::dec << std::endl;
                        
                        // The position should be the first 24 bytes (3 x int64_t for Vec3Q)
                        const int64_t* quantPos = reinterpret_cast<const int64_t*>(vertexStart);
                        std::cout << "  Position (as int64): X=" << quantPos[0] 
                                  << ", Y=" << quantPos[1] 
                                  << ", Z=" << quantPos[2] << std::endl;
                        
                        // Convert to millimeters (quantization is 1/128mm)
                        float x_mm = quantPos[0] / 128.0f;
                        float y_mm = quantPos[1] / 128.0f;
                        float z_mm = quantPos[2] / 128.0f;
                        std::cout << "  Position (mm): X=" << x_mm 
                                  << ", Y=" << y_mm 
                                  << ", Z=" << z_mm << std::endl;
                                  
                    }
                }
                */
                // Create buffer with STORAGE_BUFFER usage
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = totalBufferSize;
                bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                if (vkCreateBuffer(device_, &bufferInfo, nullptr, &gpuData.vertexStorageBuffer) != VK_SUCCESS) {
                    std::cerr << "❌ Failed to create storage buffer!" << std::endl;
                    return gpuData;
                }

                // Allocate memory for buffer
                VkMemoryRequirements memRequirements;
                vkGetBufferMemoryRequirements(device_, gpuData.vertexStorageBuffer, &memRequirements);

                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                if (vkAllocateMemory(device_, &allocInfo, nullptr, &gpuData.vertexStorageMemory) != VK_SUCCESS) {
                    std::cerr << "❌ Failed to allocate buffer memory!" << std::endl;
                    vkDestroyBuffer(device_, gpuData.vertexStorageBuffer, nullptr);
                    return gpuData;
                }

                vkBindBufferMemory(device_, gpuData.vertexStorageBuffer, gpuData.vertexStorageMemory, 0);

                // Copy vertex and index data to buffer
                void* mappedData;
                vkMapMemory(device_, gpuData.vertexStorageMemory, 0, totalBufferSize, 0, &mappedData);
                
                // Copy vertex data first
                std::memcpy(mappedData, vertexData, vertexDataSize);
                
                // Copy index data after vertices if present
                if (geomHeader.index_count > 0) {
                    const uint8_t* indexData = geomData->data() + indexDataOffset;
                    std::memcpy((uint8_t*)mappedData + vertexDataSize, indexData, indexDataSize);
                    //std::cout << "✅ Copied " << geomHeader.index_count << " indices to storage buffer" << std::endl;
                }
                
                vkUnmapMemory(device_, gpuData.vertexStorageMemory);

                //std::cout << "✅ Storage buffer created with " << totalBufferSize << " bytes" << std::endl;

                // Allocate descriptor set
                VkDescriptorSetAllocateInfo descAllocInfo{};
                descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                descAllocInfo.descriptorPool = descriptorPool;
                descAllocInfo.descriptorSetCount = 1;
                descAllocInfo.pSetLayouts = &meshShaderDescSetLayout;

                std::cout << "🔍 Descriptor allocation debug:" << std::endl;
                std::cout << "  Descriptor pool: " << (void*)descriptorPool << std::endl;
                std::cout << "  Descriptor set layout: " << (void*)meshShaderDescSetLayout << std::endl;

                VkResult allocResult = vkAllocateDescriptorSets(device_, &descAllocInfo, &gpuData.descriptorSet);
                if (allocResult == VK_ERROR_OUT_OF_POOL_MEMORY || allocResult == VK_ERROR_FRAGMENTED_POOL) {
                    std::cout << "⚠️  Descriptor pool exhausted, recreating..." << std::endl;
                    Logger::get().error("DESCRIPTOR POOL EXHAUSTED! This is likely causing the hang.");
                    
                    // Recreate the descriptor pool with more capacity
                    recreateDescriptorPool();
                    
                    // Update the allocation info with new pool
                    descAllocInfo.descriptorPool = descriptorPool;
                    
                    // Try allocation again
                    allocResult = vkAllocateDescriptorSets(device_, &descAllocInfo, &gpuData.descriptorSet);
                }
                
                if (allocResult != VK_SUCCESS) {
                    std::cerr << "❌ Failed to allocate descriptor set! Result: " << allocResult << std::endl;
                    vkDestroyBuffer(device_, gpuData.vertexStorageBuffer, nullptr);
                    vkFreeMemory(device_, gpuData.vertexStorageMemory, nullptr);
                    gpuData.vertexStorageBuffer = VK_NULL_HANDLE;  // Clear the handle
                    gpuData.vertexStorageMemory = VK_NULL_HANDLE;
                    return gpuData;
                }

                // Update descriptor set to point to storage buffer
                VkDescriptorBufferInfo bufferDescInfo{};
                bufferDescInfo.buffer = gpuData.vertexStorageBuffer;
                bufferDescInfo.offset = 0;
                bufferDescInfo.range = totalBufferSize;

                VkWriteDescriptorSet descriptorWrite{};
                descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.dstSet = gpuData.descriptorSet;
                descriptorWrite.dstBinding = 0;
                descriptorWrite.dstArrayElement = 0;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pBufferInfo = &bufferDescInfo;

                vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);

                std::cout << "✅ Descriptor set updated" << std::endl;

                // Store mesh shader info
                gpuData.vertexCount = geomHeader.vertex_count;
                // Calculate actual primitive count from indices or vertices
                // For triangles: either index_count/3 or vertex_count/3 (if non-indexed)
                if (geomHeader.index_count > 0) {
                    gpuData.primitiveCount = geomHeader.index_count / 3;
                } else {
                    gpuData.primitiveCount = geomHeader.vertex_count / 3;
                }
                
                // IMPORTANT: For Vec3Q support, the stride needs to account for the fact that
                // Vec3Q is 24 bytes (3 x int64) but the shader reads it as uint32 pairs
                // The stride should be in uint32 units, not float units
                gpuData.vertexStrideFloats = geomHeader.vertex_stride / sizeof(uint32_t);
                
                // Calculate index offset (indices come after vertices in the buffer)
                gpuData.indexOffset = vertexDataSize;
                gpuData.indexCount = geomHeader.index_count;

                //std::cout << "📊 Mesh shader parameters:" << std::endl;
                //std::cout << "  Vertex count: " << gpuData.vertexCount << std::endl;
                //std::cout << "  Index count: " << geomHeader.index_count << std::endl;
                //std::cout << "  Index offset: " << gpuData.indexOffset << " bytes" << std::endl;
                //std::cout << "  Primitive count: " << gpuData.primitiveCount << std::endl;
                //std::cout << "  Vertex stride (bytes): " << geomHeader.vertex_stride << std::endl;
                //std::cout << "  Vertex stride (uint32s): " << gpuData.vertexStrideFloats << std::endl;
                //std::cout << "✅ Successfully created storage buffer: " << gpuData.vertexStorageBuffer << std::endl;
                //std::cout << "✅ Successfully allocated descriptor set: " << gpuData.descriptorSet << std::endl;
            }
            else {
                std::cout << "📐 Using traditional vertex buffer setup..." << std::endl;
                
                // Calculate vertex data size and offset
                size_t vertexDataOffset = sizeof(Taffy::GeometryChunk);
                size_t vertexDataSize = geomHeader.vertex_count * geomHeader.vertex_stride;
                
                std::cout << "  Vertex data size: " << vertexDataSize << " bytes" << std::endl;
                
                // Validate data bounds
                if (vertexDataOffset + vertexDataSize > geomData->size()) {
                    std::cerr << "❌ Vertex data extends beyond chunk!" << std::endl;
                    return gpuData;
                }
                
                const uint8_t* vertexData = geomData->data() + vertexDataOffset;
                
                // Create vertex buffer (using storage buffer for now, but with vertex buffer usage)
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = vertexDataSize;
                bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                
                if (vkCreateBuffer(device_, &bufferInfo, nullptr, &gpuData.vertexStorageBuffer) != VK_SUCCESS) {
                    std::cerr << "❌ Failed to create vertex buffer!" << std::endl;
                    return gpuData;
                }
                
                // Allocate memory for buffer
                VkMemoryRequirements memRequirements;
                vkGetBufferMemoryRequirements(device_, gpuData.vertexStorageBuffer, &memRequirements);
                
                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                
                if (vkAllocateMemory(device_, &allocInfo, nullptr, &gpuData.vertexStorageMemory) != VK_SUCCESS) {
                    std::cerr << "❌ Failed to allocate buffer memory!" << std::endl;
                    vkDestroyBuffer(device_, gpuData.vertexStorageBuffer, nullptr);
                    return gpuData;
                }
                
                vkBindBufferMemory(device_, gpuData.vertexStorageBuffer, gpuData.vertexStorageMemory, 0);
                
                // Copy vertex data to buffer
                void* mappedData;
                vkMapMemory(device_, gpuData.vertexStorageMemory, 0, vertexDataSize, 0, &mappedData);
                std::memcpy(mappedData, vertexData, vertexDataSize);
                vkUnmapMemory(device_, gpuData.vertexStorageMemory);
                
                std::cout << "✅ Vertex buffer created with " << vertexDataSize << " bytes" << std::endl;
                
                // Store info for traditional rendering
                gpuData.vertexCount = geomHeader.vertex_count;
                gpuData.vertexStrideFloats = geomHeader.vertex_stride / sizeof(float);
                gpuData.indexOffset = vertexDataSize;  // Indices start after vertices
                gpuData.indexCount = geomHeader.index_count;
                gpuData.usesMeshShader = false;
                
                // For traditional rendering, we might not need a descriptor set
                // but let's allocate one anyway for consistency
                VkDescriptorSetAllocateInfo descAllocInfo{};
                descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                descAllocInfo.descriptorPool = descriptorPool;
                descAllocInfo.descriptorSetCount = 1;
                descAllocInfo.pSetLayouts = &meshShaderDescSetLayout;
                
                VkResult allocResult = vkAllocateDescriptorSets(device_, &descAllocInfo, &gpuData.descriptorSet);
                if (allocResult != VK_SUCCESS) {
                    std::cerr << "⚠️  Warning: Failed to allocate descriptor set for traditional rendering" << std::endl;
                    // This is not critical for traditional rendering, so we continue
                }
            }

            std::cout << "🏁 uploadTaffyAsset complete, returning gpuData with buffer: " 
                      << gpuData.vertexStorageBuffer << std::endl;
            return gpuData;
        }

        // Extract and create shader modules from Taffy asset
        bool TaffyOverlayManager::extractShadersFromAsset(const Taffy::Asset& asset,
            VkShaderModule& meshShaderModule,
            VkShaderModule& fragmentShaderModule) {
            std::cout << "🔍 Extracting shaders from Taffy asset..." << std::endl;

            auto shaderData = asset.get_chunk_data(Taffy::ChunkType::SHDR);
            if (!shaderData) {
                std::cerr << "❌ No shader chunk found!" << std::endl;
                return false;
            }

            const uint8_t* chunk_ptr = shaderData->data();
            size_t chunk_size = shaderData->size();

            // Parse header
            Taffy::ShaderChunk header;
            if (chunk_size < sizeof(header)) {
                std::cerr << "❌ Shader chunk too small for header!" << std::endl;
                return false;
            }

            std::memcpy(&header, chunk_ptr, sizeof(header));
            std::cout << "  Total shaders in chunk: " << header.shader_count << std::endl;

            // Process each shader
            for (uint32_t i = 0; i < header.shader_count; ++i) {
                if (!extractAndCompileShader(*shaderData, i, meshShaderModule, fragmentShaderModule)) {
                    std::cerr << "❌ Failed to extract shader " << i << std::endl;
                    return false;
                }
            }

            std::cout << "✅ All shaders extracted successfully!" << std::endl;
            return true;
        }

        void TaffyOverlayManager::initializeDescriptorResources() {
            createDescriptorPool(1000); // Initial size

            // Create mesh shader descriptor set layout
            meshShaderDescSetLayout = createMeshShaderDescriptorSetLayout(device_);
        }

        void TaffyOverlayManager::createDescriptorPool(size_t maxSets) {
            // If there's an existing pool, destroy it
            if (descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device_, descriptorPool, nullptr);
                descriptorPool = VK_NULL_HANDLE;
            }

            // Configure pool sizes for mesh shader resources
            std::array<VkDescriptorPoolSize, 1> poolSizes{};
            poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            poolSizes[0].descriptorCount = static_cast<uint32_t>(maxSets);

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = static_cast<uint32_t>(maxSets);
            poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

            VkResult result = vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool);
            if (result != VK_SUCCESS) {
                std::cerr << "❌ Failed to create descriptor pool! Result: " << result << std::endl;
                throw std::runtime_error("Failed to create descriptor pool");
            }

            std::cout << "✅ Created descriptor pool with capacity for " << maxSets << " sets" << std::endl;
        }

        void TaffyOverlayManager::recreateDescriptorPool() {
            // Get current pool statistics if possible
            // For now, we'll just double the capacity
            static size_t currentCapacity = 1000;
            currentCapacity *= 2;

            std::cout << "♻️  Recreating descriptor pool with new capacity: " << currentCapacity << std::endl;

            // Create new pool with increased capacity
            createDescriptorPool(currentCapacity);
        }

        uint32_t TaffyOverlayManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
            VkPhysicalDeviceMemoryProperties memProperties;
            vkGetPhysicalDeviceMemoryProperties(physical_device_, &memProperties);

            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
                if ((typeFilter & (1 << i)) &&
                    (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }

            throw std::runtime_error("Failed to find suitable memory type!");
        }

        bool TaffyOverlayManager::extractAndCompileShader(const std::vector<uint8_t>& shader_data,
            uint32_t shader_index,
            VkShaderModule& meshShaderModule,
            VkShaderModule& fragmentShaderModule) {
            std::cout << "  🔍 Extracting shader " << shader_index << ":" << std::endl;

            const uint8_t* chunk_ptr = shader_data.data();
            size_t chunk_size = shader_data.size();

            // Parse header
            Taffy::ShaderChunk header;
            std::memcpy(&header, chunk_ptr, sizeof(header));

            if (shader_index >= header.shader_count) {
                std::cerr << "    ❌ Shader index out of range!" << std::endl;
                return false;
            }

            // Calculate offset to this shader's info
            size_t shader_info_offset = sizeof(Taffy::ShaderChunk) +
                shader_index * sizeof(Taffy::ShaderChunk::Shader);

            if (shader_info_offset + sizeof(Taffy::ShaderChunk::Shader) > chunk_size) {
                std::cerr << "    ❌ Shader info extends beyond chunk!" << std::endl;
                return false;
            }

            // Read shader info
            Taffy::ShaderChunk::Shader shader_info;
            std::memcpy(&shader_info, chunk_ptr + shader_info_offset, sizeof(shader_info));

            std::cout << "    Name hash: 0x" << std::hex << shader_info.name_hash << std::dec << std::endl;
            std::cout << "    Stage: " << static_cast<uint32_t>(shader_info.stage) << std::endl;
            std::cout << "    SPIR-V size: " << shader_info.spirv_size << " bytes" << std::endl;

            // Calculate SPIR-V offset
            size_t spirv_data_start = sizeof(Taffy::ShaderChunk) +
                header.shader_count * sizeof(Taffy::ShaderChunk::Shader);
            size_t spirv_offset = spirv_data_start;

            // Skip SPIR-V data for previous shaders
            for (uint32_t i = 0; i < shader_index; ++i) {
                size_t prev_shader_info_offset = sizeof(Taffy::ShaderChunk) +
                    i * sizeof(Taffy::ShaderChunk::Shader);
                Taffy::ShaderChunk::Shader prev_shader;
                std::memcpy(&prev_shader, chunk_ptr + prev_shader_info_offset, sizeof(prev_shader));
                spirv_offset += prev_shader.spirv_size;
            }

            // Validate SPIR-V bounds
            if (spirv_offset + shader_info.spirv_size > chunk_size) {
                std::cerr << "    ❌ SPIR-V data extends beyond chunk!" << std::endl;
                return false;
            }

            // Create Vulkan shader module
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = shader_info.spirv_size;
            createInfo.pCode = reinterpret_cast<const uint32_t*>(chunk_ptr + spirv_offset);

            VkShaderModule shaderModule;
            VkResult result = vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule);

            if (result != VK_SUCCESS) {
                std::cerr << "    ❌ Failed to create shader module! VkResult: " << result << std::endl;
                return false;
            }

            std::cout << "    ✅ Shader extracted and compiled successfully!" << std::endl;

            // Store the shader module based on stage
            if (shader_info.stage == Taffy::ShaderChunk::Shader::ShaderStage::MeshShader) {
                meshShaderModule = shaderModule;
                std::cout << "      → Stored as mesh shader module" << std::endl;
            }
            else if (shader_info.stage == Taffy::ShaderChunk::Shader::ShaderStage::Fragment) {
                fragmentShaderModule = shaderModule;
                std::cout << "      → Stored as fragment shader module" << std::endl;
            }

            return true;
        }

    shaderc_shader_kind ShaderCompiler::getShaderKind(ShaderType type) {
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


   
        Buffer::Buffer(VkDevice device, VkPhysicalDevice physicalDevice,
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
        void Buffer::update(const void* data, VkDeviceSize size, VkDeviceSize offset) {
            if (!m_memory) {
                //Logger::get().error("Attempting to update buffer with invalid memory");
                return;
            }

            if (size > m_size) {
                //Logger::get().error("Buffer update size ({}) exceeds buffer size ({})", size, m_size);
                return;
            }

            void* mappedData = nullptr;
            VkResult result = vkMapMemory(m_device, m_memory, offset, size, 0, &mappedData);

            if (result != VK_SUCCESS) {
                //Logger::get().error("Failed to map buffer memory: {}", (int)result);
                return;
            }

            memcpy(mappedData, data, static_cast<size_t>(size));

            VkMappedMemoryRange range = {
                VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                nullptr,
                m_memory,
                offset,
                size
            };
            vkFlushMappedMemoryRanges(m_device, 1, &range);


            vkUnmapMemory(m_device, m_memory);
        }


        
        // Helper function to find memory type
        uint32_t Buffer::findMemoryType(VkPhysicalDevice physicalDevice,
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

    
        // Initialize the compiler
        ShaderCompiler::ShaderCompiler() {
            m_compiler = std::make_unique<shaderc::Compiler>();
            m_options = std::make_unique<shaderc::CompileOptions>();


            // Set proper SPIR-V version (1.6 is good for Vulkan 1.4)
            m_options->SetTargetSpirv(shaderc_spirv_version_1_6);

            m_options->SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

            // Set target environment

            // Suppress warnings that might clutter your output
            //m_options->SetSuppressWarnings(); // Set to true if too verbose
            //m_options->SetWarningsAsErrors();  // Good for development
        }

        // Compile GLSL or HLSL source to SPIR-V
        std::vector<uint32_t> ShaderCompiler::compileToSpv(
            const std::string& source,
            ShaderType type,
            const std::string& filename,
            int flags) {

            // Create completely fresh options for this compilation
            shaderc::CompileOptions options;
            options.SetTargetSpirv(shaderc_spirv_version_1_6);
            options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

            std::cout << "=== COMPILING: " << filename << " ===" << std::endl;
            std::cout << "Source preview: " << source.substr(0, 100) << std::endl;

            shaderc_shader_kind kind = getShaderKind(type);

            // Use the fresh options, not m_options
            shaderc::SpvCompilationResult result = m_compiler->CompileGlslToSpv(
                source, kind, filename.c_str(), options);  // Use 'options', not '*m_options'

            if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
                std::cout << "FULL ERROR MESSAGE:" << std::endl;
                std::cout << result.GetErrorMessage() << std::endl;
                return {};
            }

            std::cout << "SUCCESS!" << std::endl;
            return std::vector<uint32_t>(result.cbegin(), result.cend());
        }

        // Compile a shader file to SPIR-V
        std::vector<uint32_t> ShaderCompiler::compileFileToSpv(
            const std::string& filename,
            ShaderType type,
            const CompileOptions& options) {

            // Read file content
            std::ifstream file(filename);
            if (!file.is_open()) {
                //Logger::get().error("Failed to open shader file: {}", filename);
                return {};
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            file.close();

            // Compile the source
            return compileToSpv(buffer.str(), type, filename);

            // Compile the source
            //return compileToSpv(sourceCode, type, filename);
        }

        // Convert from VkShaderStageFlags to ShaderStageType
        ShaderReflection::ShaderStageType ShaderReflection::getStageType(VkShaderStageFlags flags) {
            if (flags & VK_SHADER_STAGE_VERTEX_BIT) return ShaderStageType::Vertex;
            if (flags & VK_SHADER_STAGE_FRAGMENT_BIT) return ShaderStageType::Fragment;
            if (flags & VK_SHADER_STAGE_COMPUTE_BIT) return ShaderStageType::Compute;
            if (flags & VK_SHADER_STAGE_GEOMETRY_BIT) return ShaderStageType::Geometry;
            if (flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) return ShaderStageType::TessControl;
            if (flags & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) return ShaderStageType::TessEvaluation;
            if (flags & VK_SHADER_STAGE_TASK_BIT_EXT) return ShaderStageType::Task;         // Add this
            if (flags & VK_SHADER_STAGE_MESH_BIT_EXT) return ShaderStageType::Mesh;         // Add this

            return ShaderStageType::Vertex; // Default
        }

        // Change the map to use the enum
        std::unordered_map<VkShaderStageFlags, std::vector<uint32_t>> m_spirvCode;


        // Then update the getUBOMembers method
        // Then modify getUBOMembers to create a compiler on demand
        std::vector<ShaderReflection::UBOMember> ShaderReflection::getUBOMembers(const UniformBuffer& ubo) const {
            // Try to find SPIRV code for any stage that this UBO uses
            const VkShaderStageFlags stagesToTry[] = {
                VK_SHADER_STAGE_VERTEX_BIT,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                VK_SHADER_STAGE_COMPUTE_BIT
                // Add others as needed
            };

            for (VkShaderStageFlags stage : stagesToTry) {
                if (ubo.stageFlags & stage) {
                    auto it = m_spirvCode.find(stage);
                    if (it != m_spirvCode.end()) {
                        // Create a compiler for this stage's SPIRV code
                        spirv_cross::CompilerGLSL compiler(it->second);

                        // Extract UBO members
                        std::vector<UBOMember> members;
                        const spirv_cross::SPIRType& type = compiler.get_type(ubo.baseTypeId);

                        for (uint32_t i = 0; i < type.member_types.size(); i++) {
                            UBOMember member;
                            member.name = compiler.get_member_name(ubo.baseTypeId, i);
                            member.offset = compiler.type_struct_member_offset(type, i);
                            member.size = compiler.get_declared_struct_member_size(type, i);

                            // Get member type information
                            const spirv_cross::SPIRType& memberType = compiler.get_type(type.member_types[i]);
                            member.typeInfo.baseType = memberType.basetype;
                            member.typeInfo.vecSize = memberType.vecsize;
                            member.typeInfo.columns = memberType.columns;
                            member.typeInfo.arrayDims = std::vector<uint32_t>(memberType.array.begin(), memberType.array.end());

                            members.push_back(member);
                        }

                        return members;
                    }
                }
            }

            return {}; // No compiler found for any relevant stage
        }

        void ShaderReflection::reflect(const std::vector<uint32_t>& spirvCode, VkShaderStageFlags stageFlags) {
            // Convert flags to enum
            // Store the SPIRV code for this stage
            m_spirvCode[stageFlags] = spirvCode;

            // Create a local compiler for reflection (not stored)
            spirv_cross::CompilerGLSL compiler(spirvCode);
            spirv_cross::ShaderResources resources = compiler.get_shader_resources();
            //Logger::get().info("Shader reflection found: {} uniform buffers, {} sampled images",
            //    resources.uniform_buffers.size(), resources.sampled_images.size());

            // REMOVE THIS ENTIRE SECTION - This is the first occurrence that's causing duplication
            // ----------------------------------------------------------------------------------
            // // Print all resources by name
            // for (const auto& resource : resources.sampled_images) {
            //     ResourceBinding binding;
            //     binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            //     binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            //     binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            //
            //     // Check if this is an array
            //     spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
            //     binding.count = type.array.empty() ? 1 : type.array[0];
            //
            //     // Ensure stage flags are not zero
            //     binding.stageFlags = stageFlags;
            //     binding.name = resource.name;
            //
            //     Logger::get().info("Resource: {} (set {}, binding {})", binding.name, binding.set, binding.binding);
            //
            //     m_resourceBindings.push_back(binding);
            // }
            // ----------------------------------------------------------------------------------

            // Process uniform buffers
            for (const auto& resource : resources.uniform_buffers) {
                UniformBuffer ubo;
                // Set basic properties
                ubo.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                ubo.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                ubo.name = resource.name;
                ubo.stageFlags = stageFlags;
                ubo.typeId = resource.type_id;
                ubo.baseTypeId = resource.base_type_id;

                // Get UBO size
                const spirv_cross::SPIRType& type = compiler.get_type(resource.base_type_id);
                ubo.size = compiler.get_declared_struct_size(type);

                // Extract members immediately
                for (uint32_t i = 0; i < type.member_types.size(); i++) {
                    UBOMember member;
                    member.name = compiler.get_member_name(resource.base_type_id, i);
                    member.offset = compiler.type_struct_member_offset(type, i);
                    member.size = compiler.get_declared_struct_member_size(type, i);

                    // Get member type info
                    const spirv_cross::SPIRType& memberType = compiler.get_type(type.member_types[i]);
                    member.typeInfo.baseType = memberType.basetype;
                    member.typeInfo.vecSize = memberType.vecsize;
                    member.typeInfo.columns = memberType.columns;
                    member.typeInfo.arrayDims = std::vector<uint32_t>(memberType.array.begin(), memberType.array.end());

                    ubo.members.push_back(member);
                }

                // Store UBO
                m_uniformBuffers.push_back(ubo);

                // Also add to resource bindings
                ResourceBinding binding;
                binding.set = ubo.set;
                binding.binding = ubo.binding;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                binding.count = 1;
                binding.stageFlags = stageFlags;
                binding.name = ubo.name;
                m_resourceBindings.push_back(binding);
            }

            // Process storage buffers
            for (const auto& resource : resources.storage_buffers) {
                ResourceBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                binding.count = 1;
                binding.stageFlags = stageFlags;
                binding.name = resource.name;
                m_resourceBindings.push_back(binding);
            }

            // Process combined image samplers - THIS IS THE CORRECT ONE TO KEEP
            for (const auto& resource : resources.sampled_images) {
                ResourceBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

                // Check if this is an array
                spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
                binding.count = type.array.empty() ? 1 : type.array[0];

                binding.stageFlags = stageFlags;
                binding.name = resource.name;

                // If you want to keep the logging, move it here
                //Logger::get().info("Resource: {} (set {}, binding {})", binding.name, binding.set, binding.binding);

                m_resourceBindings.push_back(binding);
            }

            // Process separate images
            for (const auto& resource : resources.separate_images) {
                ResourceBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

                spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
                binding.count = type.array.empty() ? 1 : type.array[0];

                binding.stageFlags = stageFlags;
                binding.name = resource.name;
                m_resourceBindings.push_back(binding);
            }

            // Process separate samplers
            for (const auto& resource : resources.separate_samplers) {
                ResourceBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;

                spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
                binding.count = type.array.empty() ? 1 : type.array[0];

                binding.stageFlags = stageFlags;
                binding.name = resource.name;
                m_resourceBindings.push_back(binding);
            }

            // Process storage images
            for (const auto& resource : resources.storage_images) {
                ResourceBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

                spirv_cross::SPIRType type = compiler.get_type(resource.type_id);
                binding.count = type.array.empty() ? 1 : type.array[0];

                binding.stageFlags = stageFlags;
                binding.name = resource.name;
                m_resourceBindings.push_back(binding);
            }

            // Process push constants
            for (const auto& resource : resources.push_constant_buffers) {
                const spirv_cross::SPIRType& type = compiler.get_type(resource.base_type_id);
                uint32_t size = compiler.get_declared_struct_size(type);

                PushConstantRange range;
                range.stageFlags = stageFlags;
                range.offset = 0; // Will be calculated when merging with other stages
                range.size = size;
                m_pushConstantRanges.push_back(range);
            }

            // Process vertex input attributes (for vertex shaders only)
            if (stageFlags & VK_SHADER_STAGE_VERTEX_BIT) {
                for (const auto& resource : resources.stage_inputs) {
                    const uint32_t location = compiler.get_decoration(resource.id, spv::DecorationLocation);
                    const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);

                    VertexAttribute attr;
                    attr.location = location;
                    attr.name = resource.name;

                    // Determine format based on type
                    attr.format = getFormatFromType(type);

                    m_vertexAttributes.push_back(attr);
                }
            }
        }

        // Merge reflection data from multiple shaders (e.g., vertex + fragment)
        void ShaderReflection::merge(const ShaderReflection& other) {
            // Merge resource bindings, combining stage flags for duplicate bindings
            for (const auto& binding : other.m_resourceBindings) {
                auto it = std::find_if(m_resourceBindings.begin(), m_resourceBindings.end(),
                    [&binding](const ResourceBinding& existing) {
                        return existing.set == binding.set &&
                            existing.binding == binding.binding &&
                            existing.descriptorType == binding.descriptorType;
                    });

                if (it != m_resourceBindings.end()) {
                    it->stageFlags |= binding.stageFlags;
                }
                else {
                    m_resourceBindings.push_back(binding);
                }
            }

            // For UBOs
            for (const auto& ubo : other.m_uniformBuffers) {
                auto it = std::find_if(m_uniformBuffers.begin(), m_uniformBuffers.end(),
                    [&ubo](const UniformBuffer& existing) {
                        return existing.set == ubo.set && existing.binding == ubo.binding;
                    });

                if (it != m_uniformBuffers.end()) {
                    // Combine stage flags
                    it->stageFlags |= ubo.stageFlags;

                    // Preserve member info if we don't have it yet
                    if (it->members.empty() && !ubo.members.empty()) {
                        it->members = ubo.members;
                    }
                }
                else {
                    m_uniformBuffers.push_back(ubo);
                }
            }

            // Merge push constant ranges with proper offsets
            for (const auto& range : other.m_pushConstantRanges) {
                // For simplicity we'll just add the new range
                // A more complete implementation would merge overlapping ranges
                m_pushConstantRanges.push_back(range);
            }

            // Merge vertex attributes (typically only from vertex shader, but included for completeness)
            for (const auto& attr : other.m_vertexAttributes) {
                if (std::find_if(m_vertexAttributes.begin(), m_vertexAttributes.end(),
                    [&attr](const VertexAttribute& existing) {
                        return existing.location == attr.location;
                    }) == m_vertexAttributes.end()) {
                    m_vertexAttributes.push_back(attr);
                }
            }
        }

        std::unique_ptr<DescriptorSetLayoutResource> ShaderReflection::createDescriptorSetLayout(VkDevice device, uint32_t setNumber) const {

            std::vector<VkDescriptorSetLayoutBinding> bindings;

            // Debug output before creating layout
            //Logger::get().info("Creating descriptor set layout for set {}", setNumber);

            for (const auto& binding : m_resourceBindings) {
                if (binding.set == setNumber) {
                    VkDescriptorSetLayoutBinding layoutBinding{};
                    layoutBinding.binding = binding.binding;
                    layoutBinding.descriptorType = binding.descriptorType;
                    layoutBinding.descriptorCount = binding.count;
                    layoutBinding.stageFlags = binding.stageFlags;
                    layoutBinding.pImmutableSamplers = nullptr;

                    // Log each binding we're adding
                    const char* typeStr = "Unknown";
                    switch (binding.descriptorType) {
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: typeStr = "UBO"; break;
                    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: typeStr = "Sampler"; break;
                    default: typeStr = "Other"; break;
                    }

                    //Logger::get().info("  Adding binding {}.{}: {} (count={}, stages=0x{:X})",
                    //    binding.set, binding.binding, typeStr, binding.count, binding.stageFlags);

                    bindings.push_back(layoutBinding);
                }
            }

            if (bindings.empty()) {
                //Logger::get().info("No bindings found for set {}", setNumber);
                //return nullptr;  // No bindings for this set, continue anyway...
            }

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            //Logger::get().info("Creating descriptor set layout with {} bindings", bindings.size());

            auto layout = std::make_unique<DescriptorSetLayoutResource>(device);

            VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout->handle());
            if (result != VK_SUCCESS) {
                //Logger::get().error("Failed to create descriptor set layout for set {}: Error code {}",
                //    setNumber, static_cast<int>(result));
                return nullptr;
            }

            //Logger::get().info("Successfully created descriptor set layout for set {}", setNumber);
            return layout;
        }


        // Create pipeline layout with all descriptor set layouts and push constants
        std::unique_ptr<PipelineLayoutResource> ShaderReflection::createPipelineLayout(VkDevice device) const {
            // First determine how many unique descriptor sets we need
            uint32_t maxSet = 0;
            for (const auto& binding : m_resourceBindings) {
                maxSet = std::max(maxSet, binding.set);
            }

            std::vector<std::unique_ptr<DescriptorSetLayoutResource>> setLayouts;
            std::vector<VkDescriptorSetLayout> rawSetLayouts;

            // Create a layout for each set
            for (uint32_t i = 0; i <= maxSet; i++) {
                auto layout = createDescriptorSetLayout(device, i);
                if (layout != nullptr) {
                    rawSetLayouts.push_back(layout->handle());
                    setLayouts.push_back(std::move(layout));
                }
                else {
                    // No bindings for this set, but we need to maintain the set indexes
                    // Create an empty layout
                    VkDescriptorSetLayoutCreateInfo emptyLayoutInfo{};
                    emptyLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                    emptyLayoutInfo.bindingCount = 0;

                    auto emptyLayout = std::make_unique<DescriptorSetLayoutResource>(device);
                    if (vkCreateDescriptorSetLayout(device, &emptyLayoutInfo, nullptr, &emptyLayout->handle()) != VK_SUCCESS) {
                        //Logger::get().error("Failed to create empty descriptor set layout for set {}", i);
                        return nullptr;
                    }

                    rawSetLayouts.push_back(emptyLayout->handle());
                    setLayouts.push_back(std::move(emptyLayout));
                }
            }

            // Organize push constant ranges
            std::vector<VkPushConstantRange> pushConstantRanges;
            for (const auto& range : m_pushConstantRanges) {
                VkPushConstantRange vkRange{};
                vkRange.stageFlags = range.stageFlags;
                vkRange.offset = range.offset;
                vkRange.size = range.size;
                pushConstantRanges.push_back(vkRange);
            }

            // Create the pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(rawSetLayouts.size());
            pipelineLayoutInfo.pSetLayouts = rawSetLayouts.data();
            pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
            pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

            VkPipelineLayout pipelineLayout;
            if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
                //Logger::get().error("Failed to create pipeline layout");
                return nullptr;
            }

            return std::make_unique<PipelineLayoutResource>(device, pipelineLayout);
        }

        // Create a descriptor pool based on the reflected shader needs
        std::unique_ptr<DescriptorPoolResource> ShaderReflection::createDescriptorPool(VkDevice device, uint32_t maxSets) const {
            // Count needed descriptors by type
            std::unordered_map<VkDescriptorType, uint32_t> typeCount;

            for (const auto& binding : m_resourceBindings) {
                typeCount[binding.descriptorType] += binding.count;
            }

            // Create pool sizes for each needed descriptor type
            std::vector<VkDescriptorPoolSize> poolSizes;
            for (const auto& [type, count] : typeCount) {
                VkDescriptorPoolSize poolSize{};
                poolSize.type = type;
                poolSize.descriptorCount = count * maxSets; // Multiply by max sets to allow multiple allocations
                poolSizes.push_back(poolSize);
            }

            // Create the descriptor pool
            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow freeing individual sets
            poolInfo.maxSets = maxSets;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();

            auto pool = std::make_unique<DescriptorPoolResource>(device);
            if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool->handle()) != VK_SUCCESS) {
                //Logger::get().error("Failed to create descriptor pool");
                return nullptr;
            }

            return pool;
        }

        // Generate vertex input state based on vertex attributes
        VkPipelineVertexInputStateCreateInfo ShaderReflection::createVertexInputState() const {
            // For simplicity, we'll assume a single binding
            // A more complete implementation would support multiple vertex buffers

            // Sort attributes by location
            std::vector<VertexAttribute> sortedAttrs = m_vertexAttributes;
            std::sort(sortedAttrs.begin(), sortedAttrs.end(),
                [](const VertexAttribute& a, const VertexAttribute& b) {
                    return a.location < b.location;
                });

            // Create attribute descriptions
            m_attributeDescriptions.clear();
            for (const auto& attr : sortedAttrs) {
                VkVertexInputAttributeDescription attrDesc{};
                attrDesc.location = attr.location;
                attrDesc.binding = 0;  // Single binding
                attrDesc.format = attr.format;

                // Calculate offset based on previous attributes
                uint32_t offset = 0;
                for (size_t i = 0; i < m_attributeDescriptions.size(); i++) {
                    offset += getFormatSize(m_attributeDescriptions[i].format);
                }
                attrDesc.offset = offset;

                m_attributeDescriptions.push_back(attrDesc);
            }

            // Create binding description
            uint32_t stride = 0;
            for (const auto& attr : m_attributeDescriptions) {
                stride += getFormatSize(attr.format);
            }

            m_bindingDescription.binding = 0;
            m_bindingDescription.stride = stride;
            m_bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            // Create vertex input state
            m_vertexInputState = {};
            m_vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            m_vertexInputState.vertexBindingDescriptionCount = 1;
            m_vertexInputState.pVertexBindingDescriptions = &m_bindingDescription;
            m_vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_attributeDescriptions.size());
            m_vertexInputState.pVertexAttributeDescriptions = m_attributeDescriptions.data();

            return m_vertexInputState;
        }

        
        // Helper to determine VkFormat from SPIR-V type
        VkFormat ShaderReflection::getFormatFromType(const spirv_cross::SPIRType& type) const {
            // Basic mapping from SPIR-V types to Vulkan formats
            if (type.basetype == spirv_cross::SPIRType::Float) {
                if (type.vecsize == 1) return VK_FORMAT_R32_SFLOAT;
                if (type.vecsize == 2) return VK_FORMAT_R32G32_SFLOAT;
                if (type.vecsize == 3) return VK_FORMAT_R32G32B32_SFLOAT;
                if (type.vecsize == 4) return VK_FORMAT_R32G32B32A32_SFLOAT;
            }
            else if (type.basetype == spirv_cross::SPIRType::Int) {
                if (type.vecsize == 1) return VK_FORMAT_R32_SINT;
                if (type.vecsize == 2) return VK_FORMAT_R32G32_SINT;
                if (type.vecsize == 3) return VK_FORMAT_R32G32B32_SINT;
                if (type.vecsize == 4) return VK_FORMAT_R32G32B32A32_SINT;
            }
            else if (type.basetype == spirv_cross::SPIRType::UInt) {
                if (type.vecsize == 1) return VK_FORMAT_R32_UINT;
                if (type.vecsize == 2) return VK_FORMAT_R32G32_UINT;
                if (type.vecsize == 3) return VK_FORMAT_R32G32B32_UINT;
                if (type.vecsize == 4) return VK_FORMAT_R32G32B32A32_UINT;
            }

            // Default for unknown type
            return VK_FORMAT_UNDEFINED;
        }

        // Helper to get size of a format in bytes
        uint32_t ShaderReflection::getFormatSize(VkFormat format) const {
            switch (format) {
            case VK_FORMAT_R32_SFLOAT:
            case VK_FORMAT_R32_UINT:
            case VK_FORMAT_R32_SINT:
                return 4;
            case VK_FORMAT_R32G32_SFLOAT:
            case VK_FORMAT_R32G32_UINT:
            case VK_FORMAT_R32G32_SINT:
                return 8;
            case VK_FORMAT_R32G32B32_SFLOAT:
            case VK_FORMAT_R32G32B32_UINT:
            case VK_FORMAT_R32G32B32_SINT:
                return 12;
            case VK_FORMAT_R32G32B32A32_SFLOAT:
            case VK_FORMAT_R32G32B32A32_UINT:
            case VK_FORMAT_R32G32B32A32_SINT:
                return 16;
            default:
                return 0;
            }
        }





        // Default constructor

        // Constructor with device and raw module
        ShaderModule::ShaderModule(VkDevice device, VkShaderModule rawModule, ShaderType type)
            : m_device(device), m_type(type), m_entryPoint("main") {
            if (rawModule != VK_NULL_HANDLE) {
                m_module = std::make_unique<ShaderModuleResource>(device, rawModule);
                //Logger::get().info("Shader module created with raw handle");
                if (m_spirvCode.size() > 0) {
                    m_reflection = std::make_unique<ShaderReflection>();
                    m_reflection->reflect(m_spirvCode, getShaderStageFlagBits());
                }
            }
        }

        std::unique_ptr<ShaderReflection> m_reflection;

        // Load from precompiled SPIR-V file
        std::unique_ptr<ShaderModule> ShaderModule::loadFromFile(VkDevice device, const std::string& filename,
            ShaderType type,
            const std::string& entryPoint) {
            // Read file...
            std::ifstream file(filename);

            if (!file.is_open()) {
                //Logger::get().error("Failed to open shader file: {}", filename);
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
                //Logger::get().error("Failed to create shader module from file: {}", filename);
                return nullptr;
            }

            // Create ShaderModule object
            auto result = std::make_unique<ShaderModule>();
            result->m_device = device;
            result->m_module = std::make_unique<ShaderModuleResource>(device, shaderModule);
            result->m_type = type;
            result->m_entryPoint = entryPoint;
            result->m_filename = filename;

            // ADD THIS: Store the SPIRV code for reflection
            result->m_spirvCode.resize(fileSize / sizeof(uint32_t));
            memcpy(result->m_spirvCode.data(), shaderCode.data(), fileSize);

            // ADD THIS: Initialize reflection
            result->m_reflection = std::make_unique<ShaderReflection>();
            result->m_reflection->reflect(result->m_spirvCode, result->getShaderStageFlagBits());

            return result;
        }

        // Create shader stage info for pipeline creation
        VkPipelineShaderStageCreateInfo ShaderModule::createShaderStageInfo() const {
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = getShaderStageFlagBits();
            stageInfo.module = m_module ? m_module->handle() : VK_NULL_HANDLE;
            stageInfo.pName = m_entryPoint.c_str();
            return stageInfo;
        }

        // Check if valid
        bool ShaderModule::isValid() const {
            return m_module && m_module->handle() != VK_NULL_HANDLE;
        }

        // Explicit conversion operator
        ShaderModule::operator bool() const {
            return isValid();
        }


        // Compile and load from GLSL/HLSL source
        std::unique_ptr<ShaderModule> ShaderModule::compileFromSource(
            VkDevice device,
            const std::string& source,
            ShaderType type,
            const std::string& filename,
            const std::string& entryPoint,
            const ShaderCompiler::CompileOptions& options) {

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
                //Logger::get().error("Failed to create shader module from compiled source");
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
        std::unique_ptr<ShaderModule> ShaderModule::compileFromFile(
            VkDevice device,
            const std::string& filename,
            const std::string& entryPoint,
            int flags) {

            // Detect shader type from filename
            ShaderType type = inferShaderTypeFromFilename(filename);

            // Compile file to SPIR-V
            static ShaderCompiler compiler;
            auto spirv = compiler.compileFileToSpv(filename, type, ShaderCompiler::CompileOptions{});

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
                //Logger::get().error("Failed to create shader module from compiled file: {}", filename);
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

            // ADD THIS: Initialize reflection
            result->m_reflection = std::make_unique<ShaderReflection>();
            result->m_reflection->reflect(result->m_spirvCode, result->getShaderStageFlagBits());

            return result;
        }

        // Convert shader type to Vulkan stage flag
        VkShaderStageFlagBits ShaderModule::getShaderStageFlagBits() const {
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

    // Forward declarations
    class Camera;
    class RenderContext;


    /**
     * Updated TaffyAssetLoader with mesh shader support
     */
    class TaffyAssetLoader {
    private:
        VkDevice device_;
        VkPhysicalDevice physical_device_;
        std::unordered_map<std::string, std::unique_ptr<TaffyMeshShaderPipeline>> mesh_pipelines_;

    public:
        struct LoadedAsset {
            std::vector<uint32_t> mesh_ids;
            std::vector<uint32_t> material_ids;

            // Mesh shader pipeline
            std::unique_ptr<TaffyMeshShaderPipeline> mesh_pipeline;
            bool uses_mesh_shaders = false;

            // Fallback for non-mesh shader hardware
            bool has_fallback_pipeline = false;
        };

        /**
         * Load Taffy asset with mesh shader support
         */
        std::unique_ptr<LoadedAsset> load_asset(const std::string& filepath) {
            auto loaded_asset = std::make_unique<LoadedAsset>();

            Taffy::Asset asset;
            if (!asset.load_from_file_safe(filepath)) {
                std::cerr << "Failed to load Taffy asset: " << filepath << std::endl;
                return nullptr;
            }

            std::cout << "Successfully loaded Taffy asset: " << filepath << std::endl;

            // Check for mesh shader support
            if (asset.has_feature(Taffy::FeatureFlags::MeshShaders)) {
                std::cout << "Asset contains mesh shaders!" << std::endl;

                auto mesh_pipeline = std::make_unique<TaffyMeshShaderPipeline>(device_, physical_device_);
                if (mesh_pipeline->create_from_taffy_asset(asset)) {
                    loaded_asset->mesh_pipeline = std::move(mesh_pipeline);
                    loaded_asset->uses_mesh_shaders = true;

                    std::cout << "Mesh shader pipeline created successfully!" << std::endl;
                }
                else {
                    std::cout << "Failed to create mesh shader pipeline, using fallback" << std::endl;
                    // TODO: Create traditional vertex/fragment fallback
                }
            }

            // Check for SPIR-V Cross transpilation
            if (asset.has_feature(Taffy::FeatureFlags::SPIRVCross)) {
                std::cout << "Asset supports universal shader transpilation!" << std::endl;

                // Demonstrate transpilation to current platform
                auto target = TaffyShaderTranspiler::get_preferred_target();
                std::cout << "Using target API: " << static_cast<int>(target) << std::endl;
            }

            // Load geometry (minimal for mesh shaders since they generate geometry)
            if (asset.has_chunk(Taffy::ChunkType::GEOM)) {
                // For mesh shaders, geometry chunk might just contain parameters
                // The actual triangles are generated by the mesh shader!
                loaded_asset->mesh_ids.push_back(1); // Dummy mesh ID
            }

            return loaded_asset;
        }

        /**
         * Render mesh shader asset
         */
        void render_asset(const LoadedAsset& asset, VkCommandBuffer command_buffer) {
            if (asset.uses_mesh_shaders && asset.mesh_pipeline) {
                // Render using mesh shaders - no vertex buffers needed!
                asset.mesh_pipeline->render(command_buffer);
            }
            else {
                // Render using traditional vertex/index buffers
                // TODO: Implement fallback rendering
            }
        }
    };

    uint32_t VulkanClusteredRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("Failed to find suitable memory type");
    }

    // Implementation
    VulkanClusteredRenderer::VulkanClusteredRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
        VkQueue graphicsQueue, uint32_t graphicsQueueFamily,
        VkCommandPool commandPool, const ClusterConfig& config)
        : m_device(device)
        , m_physicalDevice(physicalDevice)
        , m_graphicsQueue(graphicsQueue)
        , m_graphicsQueueFamily(graphicsQueueFamily)
        , m_commandPool(commandPool)
        , m_config(config)
        , m_totalClusters(config.xSlices* config.ySlices* config.zSlices)
    {
        //Logger::get().info("Creating VulkanClusteredRenderer with {} clusters ({}x{}x{})",
        //    m_totalClusters, config.xSlices, config.ySlices, config.zSlices);
    }

    VulkanClusteredRenderer::~VulkanClusteredRenderer() {
        shutdown();
    }

    void VulkanClusteredRenderer::createClusterGrid() {
        m_clusters.resize(m_totalClusters);

        // Initialize all clusters
        for (auto& cluster : m_clusters) {
            cluster.lightOffset = 0;
            cluster.lightCount = 0;
            cluster.objectOffset = 0;
            cluster.objectCount = 0;
        }

        //Logger::get().info("Created cluster grid: {} total clusters", m_totalClusters);
    }

    void VulkanClusteredRenderer::buildClusters(Camera* camera, Octree<RenderableObject>& octree) {

        //Logger::get().info("=== BYPASS MODE: FORCE RENDER ALL OBJECTS ===");




        // Clear vectors but keep capacity to avoid reallocations
        m_clusterLightIndices.clear();
        m_clusterObjectIndices.clear();
        m_visibleObjects.clear();
        
        // Reserve capacity if needed
        if (m_visibleObjects.capacity() < 1000) {
            m_visibleObjects.reserve(1000);
            m_clusterObjectIndices.reserve(1000);
        }



        // Get ALL objects from octree and mark them visible
        auto allObjects = octree.getAllObjects();
        //Logger::get().info("Found {} objects in octree", allObjects.size());

        if (allObjects.empty()) {
            //Logger::get().error("CRITICAL: Octree is empty!");
            return;
        }



        // Copy all objects to visible list
        m_visibleObjects = allObjects;

        //Logger::get().info("OCTREE DEBUG: getAllObjects() returned {} objects", allObjects.size());
        for (size_t i = 0; i < std::min(size_t(25), allObjects.size()); i++) {
            const auto& obj = allObjects[i];
            glm::vec3 pos = obj.transform[3];
            //Logger::get().info("OCTREE Object {}: pos=({:.2f}, {:.2f}, {:.2f})", i, pos.x, pos.y, pos.z);
        }

        // Force all objects into cluster 0
        for (size_t i = 0; i < m_visibleObjects.size(); i++) {
            m_clusterObjectIndices.push_back(static_cast<uint32_t>(i));
        }

        // Set cluster 0 to contain all objects
        for (auto& cluster : m_clusters) {
            cluster.objectOffset = 0;
            cluster.objectCount = 0;
            cluster.lightOffset = 0;
            cluster.lightCount = 0;

        }

        m_clusters[0].objectOffset = 0;
        m_clusters[0].objectCount = static_cast<uint32_t>(m_visibleObjects.size());


        //Logger::get().info("BYPASS: Forced {} objects into cluster 0", m_visibleObjects.size());





        // Update GPU buffers and continue
        updateGPUBuffers();
        updateUniformBuffers(camera);


        //Logger::get().info("BYPASS MODE COMPLETE");
        return;  // Skip all normal clustering logic
    }
    bool VulkanClusteredRenderer::initialize(Format color, Format depth) {
        m_colorFormat = &color.format;
        m_depthFormat = &depth.format;

        //Logger::get().info("Initializing VulkanClusteredRenderer...");

        try {
            // Create GPU buffers
            if (!createMeshBuffers()) {
                //Logger::get().error("Failed to create mesh buffers");
                return false;
            }

            // Create default textures
            if (!createDefaultTextures()) {
                //Logger::get().error("Failed to create default textures");
                return false;
            }

            createClusterGrid();
            

            // Create default material
            //createDefaultMaterial();

            //Logger::get().info("VulkanClusteredRenderer initialized successfully");
            return true;
        }
        catch (const std::exception& e) {
            //Logger::get().error("Exception during VulkanClusteredRenderer initialization: {}", e.what());
            return false;
        }
    }

    void VulkanClusteredRenderer::shutdown() {
        // RAII will handle cleanup
        //Logger::get().info("VulkanClusteredRenderer shutdown complete");
    }

    uint32_t VulkanClusteredRenderer::loadMesh(const std::vector<MeshVertex>& vertices,
        const std::vector<uint32_t>& indices,
        const std::string& name) {
        if (vertices.empty()) {
            //Logger::get().warning("Attempting to load empty mesh");
            return UINT32_MAX;
        }

        // Check if mesh with this name already exists
        if (!name.empty()) {
            auto it = m_meshNameToID.find(name);
            if (it != m_meshNameToID.end()) {
                //Logger::get().info("Mesh '{}' already loaded, returning existing ID {}", name, it->second);
                return it->second;
            }
        }

        MeshInfo meshInfo{};
        meshInfo.vertexOffset = static_cast<uint32_t>(m_allVertices.size());
        meshInfo.vertexCount = static_cast<uint32_t>(vertices.size());
        meshInfo.indexOffset = static_cast<uint32_t>(m_allIndices.size());
        meshInfo.indexCount = static_cast<uint32_t>(indices.size());

        // Calculate bounds
        meshInfo.boundsMin = vertices[0].position.toFloat();
        meshInfo.boundsMax = vertices[0].position.toFloat();

        for (const auto& vertex : vertices) {
            meshInfo.boundsMin = glm::min((glm::vec3)meshInfo.boundsMin, (glm::vec3)vertex.position.toFloat());
            meshInfo.boundsMax = glm::max((glm::vec3)meshInfo.boundsMax, (glm::vec3)vertex.position.toFloat());
        }

        // Store mesh info
        uint32_t meshID = static_cast<uint32_t>(m_meshInfos.size());
        m_meshInfos.push_back(meshInfo);

        // Store name mapping
        if (!name.empty()) {
            m_meshNameToID[name] = meshID;
        }

        // Append to vertex and index arrays
        m_allVertices.insert(m_allVertices.end(), vertices.begin(), vertices.end());
        m_allIndices.insert(m_allIndices.end(), indices.begin(), indices.end());

        // Update GPU buffers
        updateMeshBuffers();

        //Logger::get().info("Loaded mesh '{}' with ID {}: {} vertices, {} indices",
        //    name.empty() ? "unnamed" : name, meshID, vertices.size(), indices.size());

        return meshID;
    }

    uint32_t VulkanClusteredRenderer::createMaterial(const PBRMaterial& material) {
        uint32_t materialID = static_cast<uint32_t>(m_materials.size());
        m_materials.push_back(material);

        updateMaterialBuffer();

        //Logger::get().info("Created material with ID {}", materialID);
        return materialID;
    }






    void  VulkanClusteredRenderer::updateLights(const std::vector<ClusterLight>& lights) {
        m_lights = lights;

        // Update light buffer
        if (!m_lights.empty() && m_lightBuffer) {
            VkDeviceSize lightBufferSize = m_lights.size() * sizeof(ClusterLight);
            if (lightBufferSize <= m_lightBuffer->getSize()) {
                m_lightBuffer->update(m_lights.data(), lightBufferSize);
            }
            else {
                //Logger::get().warning("Light buffer too small for {} lights", m_lights.size());
            }
        }

        //Logger::get().info("Updated {} lights", m_lights.size());
    }

    void VulkanClusteredRenderer::render(VkCommandBuffer cmdBuffer, Camera* camera) {
        //renderDebug(cmdBuffer, camera);

        updateUniformBuffers(camera);

        //Logger::get().info("C++ EnhancedClusterUBO size: {}", sizeof(EnhancedClusterUBO));
        //Logger::get().info("C++ time offset: {}", offsetof(EnhancedClusterUBO, time));


        //Logger::get().info("=== CLUSTERED RENDERER DEBUG START ===");
        //Logger::get().info("Pipeline valid: {}", m_pipeline ? "YES" : "NO");
        //Logger::get().info("Pipeline layout valid: {}", m_pipelineLayout ? "YES" : "NO");
        //Logger::get().info("Descriptor set valid: {}", m_descriptorSet ? "YES" : "NO");

        //Logger::get().info("Data summary:");
        //Logger::get().info("  Visible objects: {}", m_visibleObjects.size());
        //Logger::get().info("  Cluster object indices: {}", m_clusterObjectIndices.size());
        //Logger::get().info("  Cluster light indices: {}", m_clusterLightIndices.size());
        //Logger::get().info("  Total clusters: {}", m_totalClusters);
        //Logger::get().info("  Lights: {}", m_lights.size());

        // Count non-empty clusters
        uint32_t clustersWithObjects = 0;
        uint32_t clustersWithLights = 0;
        for (const auto& cluster : m_clusters) {
            if (cluster.objectCount > 0) clustersWithObjects++;
            if (cluster.lightCount > 0) clustersWithLights++;
        }
        //Logger::get().info("  Clusters with objects: {}", clustersWithObjects);
        //Logger::get().info("  Clusters with lights: {}", clustersWithLights);

        if (m_visibleObjects.empty()) {
            //Logger::get().error("CRITICAL: No visible objects to render!");
            return;
        }

        if (m_clusterObjectIndices.empty()) {
            //Logger::get().error("CRITICAL: No cluster object indices!");
            return;
        }

        if (!m_pipeline) {
            //Logger::get().error("Pipeline is NULL!");
            return;
        }

        if (!m_pipelineLayout) {
            //Logger::get().error("Pipeline layout is NULL!");
            return;
        }

        if (!m_descriptorSet) {
            //Logger::get().error("Descriptor set is NULL!");
            return;
        }

        //Logger::get().info("All pipeline resources valid");

        // Memory barriers for buffer updates
        std::vector<VkBufferMemoryBarrier> barriers;

        updateGPUBuffers();

        auto addBarrier = [&](VkBuffer buffer, VkDeviceSize size) {
            VkBufferMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.buffer = buffer;
            barrier.offset = 0;
            barrier.size = size;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers.push_back(barrier);
            };

        addBarrier(m_uniformBuffer->getBuffer(), m_uniformBuffer->getSize());
        addBarrier(m_clusterBuffer->getBuffer(), m_clusterBuffer->getSize());
        addBarrier(m_objectBuffer->getBuffer(), m_objectBuffer->getSize());
        addBarrier(m_lightBuffer->getBuffer(), m_lightBuffer->getSize());
        addBarrier(m_indexBuffer->getBuffer(), m_indexBuffer->getSize());


        vkCmdPipelineBarrier(
            cmdBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT,
            0,
            0, nullptr,
            static_cast<uint32_t>(barriers.size()), barriers.data(),
            0, nullptr
        );

        // Choose pipeline based on mode
        VkPipeline currentPipeline = m_debugClusters ?
            (m_debugPipeline ? m_debugPipeline->handle() : m_pipeline->handle()) :
            (m_wireframeMode && m_wireframePipeline ? m_wireframePipeline->handle() : m_pipeline->handle());

        //Logger::get().info("Using pipeline: {} | (bind {})",
        //    m_wireframeMode ? "Wireframe" : (m_debugClusters ? "Debug" : "Normal"), (void*)currentPipeline);

        if (currentPipeline) {
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline);
            //Logger::get().info("Pipeline bound successfully");
        }
        else {
            //Logger::get().error("Failed to bind pipeline!");
            return;
        }

        // Bind pipeline


        if (m_descriptorSet) {
            vkCmdBindDescriptorSets(
                cmdBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelineLayout->handle(),
                0, 1,
                &m_descriptorSet->handle(),
                0, nullptr
            );
            //Logger::get().info("Descriptor sets bound successfully");
        }
        else {
            //Logger::get().error("No descriptor set to bind!");
            return;
        }

        // Set viewport and scissor
        VkExtent2D extent = camera->extent;

        //Logger::get().info("EXTENT: ({}, {})", extent.width, extent.height);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = extent;
        vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

        // Calculate task shader workgroups
        uint32_t taskGroupX = (m_totalClusters + 31) / 32;
        taskGroupX = std::max(taskGroupX, 1u);

        //Logger::get().info("Dispatching {} task groups", taskGroupX);

        // Dispatch mesh shaders
        vkCmdDrawMeshTasksEXT(cmdBuffer, taskGroupX, 1, 1);

    }

    // Private implementation methods...

    bool VulkanClusteredRenderer::createMeshBuffers() {
        try {
            // Large pre-allocated buffers for scalability
            const VkDeviceSize VERTEX_BUFFER_SIZE = sizeof(MeshVertex) * 1000000; // 1M vertices
            const VkDeviceSize INDEX_BUFFER_SIZE = sizeof(uint32_t) * 3000000;    // 3M indices
            const VkDeviceSize MESH_INFO_SIZE = sizeof(MeshInfo) * 10000;         // 10K meshes
            const VkDeviceSize MATERIAL_SIZE = sizeof(PBRMaterial) * 1000;        // 1K materials
            const VkDeviceSize CLUSTER_SIZE = sizeof(Cluster) * m_totalClusters;
            const VkDeviceSize OBJECT_SIZE = sizeof(RenderableObject) * 25; // 100K objects
            const VkDeviceSize LIGHT_SIZE = sizeof(ClusterLight) * 1;         // 10K lights
            const VkDeviceSize INDEX_BUFFER_SIZE_CLUSTER = sizeof(uint32_t) * 1000000; // 1M indices
            const VkDeviceSize UBO_SIZE = sizeof(EnhancedClusterUBO);

            // Create all buffers
            m_vertexBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, VERTEX_BUFFER_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_meshIndexBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, INDEX_BUFFER_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_meshInfoBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, MESH_INFO_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_materialBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, MATERIAL_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_clusterBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, CLUSTER_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_objectBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, OBJECT_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_lightBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, LIGHT_SIZE,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_indexBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, INDEX_BUFFER_SIZE_CLUSTER,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            m_uniformBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, UBO_SIZE,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            //Logger::get().info("Created all mesh buffers successfully");
            return true;
        }
        catch (const std::exception& e) {
            //Logger::get().error("Failed to create mesh buffers: {}", e.what());
            return false;
        }
    }

    // Fixed createDefaultTextures method for VulkanClusteredRenderer
    bool VulkanClusteredRenderer::createDefaultTextures() {
        try {
            //Logger::get().info("Creating default textures for VulkanClusteredRenderer...");

            // Create a simple 4x4 white texture for debugging
            const uint32_t size = 4;
            const uint32_t pixelCount = size * size;
            std::vector<uint8_t> whitePixels(pixelCount * 4);

            // Fill with pure white pixels
            for (uint32_t i = 0; i < pixelCount; i++) {
                whitePixels[i * 4 + 0] = 255; // R
                whitePixels[i * 4 + 1] = 255; // G  
                whitePixels[i * 4 + 2] = 255; // B
                whitePixels[i * 4 + 3] = 255; // A
            }

            //Logger::get().info("Creating staging buffer for texture data...");

            // Create staging buffer
            VkDeviceSize imageSize = whitePixels.size();

            auto stagingBuffer = std::make_unique<Buffer>(
                m_device, m_physicalDevice, imageSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Upload data to staging buffer
            stagingBuffer->update(whitePixels.data(), imageSize);
            //Logger::get().info("Uploaded {} bytes to staging buffer", imageSize);

            // Create albedo texture image
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = { size, size, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            // Create albedo image
            m_defaultAlbedoTexture = std::make_unique<ImageResource>(m_device);
            if (vkCreateImage(m_device, &imageInfo, nullptr, &m_defaultAlbedoTexture->handle()) != VK_SUCCESS) {
                //Logger::get().error("Failed to create default albedo image");
                return false;
            }

            // Allocate memory for albedo
            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(m_device, m_defaultAlbedoTexture->handle(), &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(
                memRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            VkDeviceMemory albedoMemory;
            if (vkAllocateMemory(m_device, &allocInfo, nullptr, &albedoMemory) != VK_SUCCESS) {
                //Logger::get().error("Failed to allocate albedo memory");
                return false;
            }

            vkBindImageMemory(m_device, m_defaultAlbedoTexture->handle(), albedoMemory, 0);

            // Create normal texture (same process)
            m_defaultNormalTexture = std::make_unique<ImageResource>(m_device);
            if (vkCreateImage(m_device, &imageInfo, nullptr, &m_defaultNormalTexture->handle()) != VK_SUCCESS) {
                //Logger::get().error("Failed to create default normal image");
                return false;
            }

            // Allocate memory for normal texture
            vkGetImageMemoryRequirements(m_device, m_defaultNormalTexture->handle(), &memRequirements);
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(
                memRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );

            VkDeviceMemory normalMemory;
            if (vkAllocateMemory(m_device, &allocInfo, nullptr, &normalMemory) != VK_SUCCESS) {
                //Logger::get().error("Failed to allocate normal memory");
                return false;
            }

            vkBindImageMemory(m_device, m_defaultNormalTexture->handle(), normalMemory, 0);

            //Logger::get().info("Transitioning image layouts and copying data...");

            // Transition image layouts and copy data
            VkCommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandPool = m_commandPool;
            cmdAllocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer;
            vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &commandBuffer);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(commandBuffer, &beginInfo);

            // Transition both images to transfer destination
            VkImageMemoryBarrier barriers[2] = {};

            // Albedo image barrier
            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].image = m_defaultAlbedoTexture->handle();
            barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[0].subresourceRange.baseMipLevel = 0;
            barriers[0].subresourceRange.levelCount = 1;
            barriers[0].subresourceRange.baseArrayLayer = 0;
            barriers[0].subresourceRange.layerCount = 1;
            barriers[0].srcAccessMask = 0;
            barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            // Normal image barrier (same settings)
            barriers[1] = barriers[0];
            barriers[1].image = m_defaultNormalTexture->handle();

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                2, barriers
            );

            // Copy data from staging buffer to both images
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
                stagingBuffer->getBuffer(),
                m_defaultAlbedoTexture->handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region
            );

            vkCmdCopyBufferToImage(
                commandBuffer,
                stagingBuffer->getBuffer(),
                m_defaultNormalTexture->handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region
            );

            // Transition both images to shader read layout
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            barriers[1] = barriers[0];
            barriers[1].image = m_defaultNormalTexture->handle();

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                2, barriers
            );

            vkEndCommandBuffer(commandBuffer);

            // Submit command buffer
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(m_graphicsQueue);

            // Free command buffer
            vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);

            //Logger::get().info("Creating image views...");

            // Create image views
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            // Albedo view
            viewInfo.image = m_defaultAlbedoTexture->handle();
            m_defaultAlbedoView = std::make_unique<ImageViewResource>(m_device);
            if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_defaultAlbedoView->handle()) != VK_SUCCESS) {
                //Logger::get().error("Failed to create albedo image view");
                return false;
            }

            // Normal view
            viewInfo.image = m_defaultNormalTexture->handle();
            m_defaultNormalView = std::make_unique<ImageViewResource>(m_device);
            if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_defaultNormalView->handle()) != VK_SUCCESS) {
                //Logger::get().error("Failed to create normal image view");
                return false;
            }

            //Logger::get().info("Creating sampler...");

            // Create sampler
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_NEAREST; // Use NEAREST for debugging
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable = VK_FALSE;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.mipLodBias = 0.0f;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = 0.0f;

            m_defaultSampler = std::make_unique<SamplerResource>(m_device);
            if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_defaultSampler->handle()) != VK_SUCCESS) {
                //Logger::get().error("Failed to create default sampler");
                return false;
            }

            //Logger::get().info("Default textures created successfully!");
            //Logger::get().info("  Albedo texture: {}x{} white", size, size);
            //Logger::get().info("  Normal texture: {}x{} white", size, size);
            //Logger::get().info("  Sampler: NEAREST filtering");

            return true;
        }
        catch (const std::exception& e) {
            //Logger::get().error("Exception in createDefaultTextures: {}", e.what());
            return false;
        }
    }

    // UBO for cluster rendering
    struct ClusterUBO {
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
        glm::vec4 cameraPos;
        glm::uvec4 clusterDimensions;  // x, y, z, pad
        glm::vec4 zPlanes;             // near, far, clustersPerZ, pad
        uint32_t numLights;
        uint32_t numObjects;
        uint32_t numClusters;
        uint32_t padding;
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

    // First, add these as member variables if they don't exist already

    // Instance RAII wrapper

    // Surface RAII wrapper

    std::shared_ptr<ShaderModule> ShaderManager::loadShader(
        const std::string& filename,
        const std::string& entryPoint,
        const ShaderCompiler::CompileOptions& options) {

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

        void ShaderManager::checkForChanges() {
            for (auto& [filename, shader] : m_shaders) {
                auto currentTimestamp = getFileTimestamp(filename);
                if (currentTimestamp > m_shaderFileTimestamps[filename]) {
                    //Logger::get().info("Shader file changed, reloading: {}", filename);

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

        std::filesystem::file_time_type ShaderManager::getFileTimestamp(const std::string& filename) {
            try {
                return std::filesystem::last_write_time(filename);
            }
            catch (const std::exception& e) {
                //Logger::get().error("Failed to get file timestamp: {}", e.what());
                return std::filesystem::file_time_type();
            }
        }

        // Notify systems about shader reloads
        void ShaderManager::notifyShaderReloaded(const std::string& filename, std::shared_ptr<ShaderModule> shader) {
            // You would implement this to notify pipeline cache or other systems
            //Logger::get().info("Shader reloaded: {}", filename);
        }


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

        void setupFromShaderReflection(const std::vector<std::shared_ptr<ShaderModule>>& shaders) {
            // Combine shader reflection data
            ShaderReflection combinedReflection;

            for (const auto& shader : shaders) {
                const ShaderReflection* reflection = shader->getReflection();
                if (reflection) {
                    combinedReflection.merge(*reflection);
                }
            }

            // Setup vertex input state from reflection
            if (shaders[0]->getType() == ShaderType::Vertex) {
                vertexInputState = combinedReflection.createVertexInputState();
            }

            // The rest of the pipeline state would typically be set manually
            // or from other configuration sources
        }
    };

    class DescriptorSetBuilder {
    public:
        DescriptorSetBuilder(VkDevice device, ShaderReflection& reflection)
            : m_device(device), m_reflection(reflection) {
        }

        // Create descriptor sets for all shader resource sets
        std::vector<std::unique_ptr<DescriptorSetResource>> createDescriptorSets() {
            std::vector<std::unique_ptr<DescriptorSetResource>> result;

            // Create pool
            auto pool = m_reflection.createDescriptorPool(m_device);
            if (!pool) {
                return result;
            }

            // Find the max set number used in the shaders
            uint32_t maxSet = 0;
            for (const auto& binding : m_reflection.getResourceBindings()) {
                maxSet = std::max(maxSet, binding.set);
            }

            // Create layouts for each set
            std::vector<std::unique_ptr<DescriptorSetLayoutResource>> layouts;
            std::vector<VkDescriptorSetLayout> rawLayouts;

            //Logger::get().info("Creating descriptor sets for {} sets", maxSet + 1);

            for (uint32_t i = 0; i <= maxSet; i++) {
                auto layout = m_reflection.createDescriptorSetLayout(m_device, i);
                if (layout != nullptr) {
                    rawLayouts.push_back(layout->handle());
                    layouts.push_back(std::move(layout));
                }
                else {
                    // Create an empty layout for this set
                    //Logger::get().info("Creating empty descriptor set layout for set {}", i);

                    VkDescriptorSetLayoutCreateInfo emptyLayoutInfo{};
                    emptyLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                    emptyLayoutInfo.bindingCount = 0;

                    auto emptyLayout = std::make_unique<DescriptorSetLayoutResource>(m_device);
                    if (vkCreateDescriptorSetLayout(m_device, &emptyLayoutInfo, nullptr, &emptyLayout->handle()) != VK_SUCCESS) {
                        //Logger::get().error("Failed to create empty descriptor set layout for set {}", i);
                        continue;
                    }

                    rawLayouts.push_back(emptyLayout->handle());
                    layouts.push_back(std::move(emptyLayout));
                }
            }

            if (rawLayouts.empty()) {
                return result;  // No descriptor sets needed
            }

            // Allocate descriptor sets
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = pool->handle();
            allocInfo.descriptorSetCount = static_cast<uint32_t>(rawLayouts.size());
            allocInfo.pSetLayouts = rawLayouts.data();

            std::vector<VkDescriptorSet> descriptorSets(rawLayouts.size());
            if (vkAllocateDescriptorSets(m_device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
                //Logger::get().error("Failed to allocate descriptor sets");
                return result;
            }

            // Create RAII wrappers
            for (auto& set : descriptorSets) {
                result.push_back(std::make_unique<DescriptorSetResource>(m_device, set));
            }

            // Store for later use when updating
            m_pool = std::move(pool);
            m_layouts = std::move(layouts);

            return result;
        }

        // Update descriptor set for a uniform buffer
        void updateUniformBuffer(VkDescriptorSet set, uint32_t binding, VkBuffer buffer,
            VkDeviceSize offset, VkDeviceSize range) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = buffer;
            bufferInfo.offset = offset;
            bufferInfo.range = range;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = set;
            descriptorWrite.dstBinding = binding;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
        }

        // Update descriptor set for a combined image sampler
        void updateCombinedImageSampler(VkDescriptorSet set, uint32_t binding,
            VkImageView imageView, VkSampler sampler,
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = imageLayout;
            imageInfo.imageView = imageView;
            imageInfo.sampler = sampler;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = set;
            descriptorWrite.dstBinding = binding;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
        }

    private:
        VkDevice m_device;
        ShaderReflection& m_reflection;
        std::unique_ptr<DescriptorPoolResource> m_pool;
        std::vector<std::unique_ptr<DescriptorSetLayoutResource>> m_layouts;
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

        //Logger::get().info("Swap chain recreated: {}x{}", m_extent.width, m_extent.height);

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
        
        Logger::get().info("Swap chain created: {}x{}, {} images, format: {}, {}",
                          m_extent.width, m_extent.height, m_images.size(), 
                          static_cast<uint32_t>(m_imageFormat),
                          m_vsync ? "VSync ON" : "VSync OFF");
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

    // Implementation

    // Template specializations for VulkanDevice::getStructureType
    template<> VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceFeatures2>() { 
        return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; 
    }
    template<> VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceVulkan12Features>() { 
        return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES; 
    }
    template<> VkStructureType VulkanDevice::getStructureType<VkPhysicalDeviceVulkan13Features>() { 
        return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES; 
    }

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
            meshShaderFeatures.meshShader = VK_TRUE;
            meshShaderFeatures.taskShader = VK_TRUE;

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
        //Logger::get().info("Color format: {}", m_colorFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ?
        //    "A2B10G10R10 (10-bit)" : "R8G8B8A8 (8-bit)");

        std::string depthFormatStr;
        switch (m_depthFormat) {
        case VK_FORMAT_D32_SFLOAT_S8_UINT: depthFormatStr = "D32_S8 (32-bit)"; break;
        case VK_FORMAT_D24_UNORM_S8_UINT: depthFormatStr = "D24_S8 (24-bit)"; break;
        case VK_FORMAT_D16_UNORM_S8_UINT: depthFormatStr = "D16_S8 (16-bit)"; break;
        default: depthFormatStr = "Unknown";
        }
        //Logger::get().info("Depth format: {}", depthFormatStr);

        // Log capabilities
        //Logger::get().info("Device capabilities:");
        //Logger::get().info("  - Ray Query: {}", m_capabilities.rayQuery ? "Yes" : "No");
        //Logger::get().info("  - Mesh Shaders: {}", m_capabilities.meshShaders ? "Yes" : "No");
        //Logger::get().info("  - Bresenham Line Rasterization: {}", m_capabilities.bresenhamLineRasterization ? "Yes" : "No");
        //Logger::get().info("  - Sparse Binding (MegaTextures): {}", m_capabilities.sparseBinding ? "Yes" : "No");
        //Logger::get().info("  - Dynamic Rendering: {}", m_capabilities.dynamicRendering ? "Yes" : "No");
        //Logger::get().info("  - Buffer Device Address: {}", m_capabilities.bufferDeviceAddress ? "Yes" : "No");
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

    class DescriptorAllocator {
    public:
        // Main initialization with sensible defaults for various descriptor types
        DescriptorAllocator(VkDevice device, uint32_t maxSets = 1000)
            : m_device(device) {
            // Create descriptor pool with capacity for all common descriptor types
            std::vector<VkDescriptorPoolSize> poolSizes = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10000},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10000},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10000},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10000},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10000}
            };

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            poolInfo.maxSets = maxSets;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();

            vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool);
        }

        ~DescriptorAllocator() {
            if (m_pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(m_device, m_pool, nullptr);
            }
        }

        // Allocate a descriptor set with the given layout
        VkDescriptorSet allocate(VkDescriptorSetLayout layout) {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_pool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &layout;

            VkDescriptorSet set;
            VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &set);

            if (result == VK_ERROR_OUT_OF_POOL_MEMORY) {
                // Pool is full, create a new one
                resetPool();
                // Try again
                result = vkAllocateDescriptorSets(m_device, &allocInfo, &set);
            }

            if (result != VK_SUCCESS) {
                //Logger::get().error("Failed to allocate descriptor set: {}", static_cast<int>(result));
                return VK_NULL_HANDLE;
            }

            return set;
        }

        // Reset the pool to reuse memory
        void resetPool() {
            vkResetDescriptorPool(m_device, m_pool, 0);
        }

    private:
        VkDevice m_device;
        VkDescriptorPool m_pool = VK_NULL_HANDLE;
    };

    // Layout cache to avoid recreating the same layouts
    class DescriptorLayoutCache {
    public:
        DescriptorLayoutCache(VkDevice device) : m_device(device) {}

        ~DescriptorLayoutCache() {
            for (auto& pair : m_layouts) {
                vkDestroyDescriptorSetLayout(m_device, pair.second, nullptr);
            }
        }

        // Get or create a descriptor set layout
        VkDescriptorSetLayout getLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
            // Create a hash for the bindings
            size_t hash = 0;
            for (const auto& binding : bindings) {
                // Combine hash with binding parameters
                hash = hash_combine(hash, binding.binding);
                hash = hash_combine(hash, binding.descriptorType);
                hash = hash_combine(hash, binding.descriptorCount);
                hash = hash_combine(hash, binding.stageFlags);
            }

            // Check if layout already exists
            auto it = m_layouts.find(hash);
            if (it != m_layouts.end()) {
                return it->second;
            }

            // Create a new layout
            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            VkDescriptorSetLayout layout;
            vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &layout);

            // Cache and return
            m_layouts[hash] = layout;
            return layout;
        }

    private:
        // Hash combining function
        size_t hash_combine(size_t seed, size_t value) const {
            return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
        }

        VkDevice m_device;
        std::unordered_map<size_t, VkDescriptorSetLayout> m_layouts;
    };

    // High-level descriptor writer for easier updates
    class DescriptorWriter {


    public:
        struct WriteInfo {
            uint32_t binding;
            VkDescriptorType type;
            int32_t bufferIndex;
            int32_t imageIndex;
        };

        DescriptorLayoutCache& m_layoutCache;
        DescriptorAllocator& m_allocator;
        VkDevice m_device;

        DescriptorWriter(DescriptorLayoutCache& layoutCache, DescriptorAllocator& allocator)
            : m_layoutCache(layoutCache), m_allocator(allocator) {
        }
        std::vector<VkDescriptorSetLayoutBinding> m_bindings;
        std::vector<VkDescriptorBufferInfo> m_bufferInfos;
        std::vector<VkDescriptorImageInfo> m_imageInfos;
        std::vector<WriteInfo> m_writes;


        // Add buffer binding
        DescriptorWriter& writeBuffer(uint32_t binding, VkBuffer buffer,
            VkDeviceSize offset, VkDeviceSize range,
            VkDescriptorType type, VkShaderStageFlags stageFlags) {
            // Add binding to layout bindings
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = binding;
            layoutBinding.descriptorType = type;
            layoutBinding.descriptorCount = 1;
            layoutBinding.stageFlags = stageFlags;

            m_bindings.push_back(layoutBinding);

            // Add write info
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = buffer;
            bufferInfo.offset = offset;
            bufferInfo.range = range;

            m_bufferInfos.push_back(bufferInfo);

            // Keep track of the write
            m_writes.push_back({ binding, type, static_cast<int32_t>(m_bufferInfos.size() - 1), -1 });

            return *this;
        }

        // Add image binding
        DescriptorWriter& writeImage(uint32_t binding, VkImageView imageView, VkSampler sampler,
            VkImageLayout layout, VkDescriptorType type,
            VkShaderStageFlags stageFlags) {
            // Add binding to layout bindings
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = binding;
            layoutBinding.descriptorType = type;
            layoutBinding.descriptorCount = 1;
            layoutBinding.stageFlags = stageFlags;

            m_bindings.push_back(layoutBinding);

            // Add write info
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageView = imageView;
            imageInfo.sampler = sampler;
            imageInfo.imageLayout = layout;

            m_imageInfos.push_back(imageInfo);

            // Keep track of the write
            m_writes.push_back({ binding, type, -1, static_cast<int32_t>(m_imageInfos.size() - 1) });

            return *this;
        }

        // Build and update a descriptor set
        bool build(VkDescriptorSet& set) {
            // Create layout from bindings
            VkDescriptorSetLayout layout = m_layoutCache.getLayout(m_bindings);

            // Allocate the descriptor set
            set = m_allocator.allocate(layout);
            if (set == VK_NULL_HANDLE) {
                return false;
            }

            // Update the descriptor set
            return update(set);
        }

        // Update an existing descriptor set
        bool update(VkDescriptorSet set) {
            std::vector<VkWriteDescriptorSet> writes;
            writes.reserve(m_writes.size());

            for (const auto& write : m_writes) {
                VkWriteDescriptorSet descriptorWrite{};
                descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.dstSet = set;
                descriptorWrite.dstBinding = write.binding;
                descriptorWrite.dstArrayElement = 0;
                descriptorWrite.descriptorType = write.type;
                descriptorWrite.descriptorCount = 1;

                if (write.bufferIndex != -1) {
                    descriptorWrite.pBufferInfo = &m_bufferInfos[write.bufferIndex];
                }
                else if (write.imageIndex != -1) {
                    descriptorWrite.pImageInfo = &m_imageInfos[write.imageIndex];
                }

                writes.push_back(descriptorWrite);
            }

            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            return true;
        }

    };

    class DescriptorBuilder {
    public:
        DescriptorBuilder(VkDevice device, const ShaderReflection& reflection,
            std::unique_ptr<DescriptorPoolResource>& pool)
            : m_device(device), m_reflection(reflection), m_descriptorPool(pool) {
        }

        void takeDescriptorSets(std::vector<std::unique_ptr<DescriptorSetResource>>& outSets) {
            outSets.clear();
            for (auto& set : m_descriptorSets) {
                outSets.push_back(std::move(set));
            }
            m_descriptorSets.clear();
        }


        // Initialize from shader reflection
        bool buildFromReflection() {
            // First, create set layouts from reflection
            createDescriptorSetLayouts();

            // Then allocate descriptor sets
            if (!allocateDescriptorSets()) {
                return false;
            }

            // Finally, update the descriptor sets based on reflection data
            return updateDescriptorSetsFromReflection();
        }

        // Get the descriptor sets
        const std::vector<std::unique_ptr<DescriptorSetResource>>& getDescriptorSets() const {
            return std::move(m_descriptorSets);
        }

        // Register a uniform buffer for automatic binding
        DescriptorBuilder& registerUniformBuffer(const std::string& name, Buffer* buffer,
            size_t size) {
            m_registeredBuffers[name] = { buffer, size };
            return *this;
        }

        DescriptorBuilder& registerTexture(const std::string& name, ImageViewResource* imageView,
            SamplerResource* sampler) {
            m_registeredTextures[name] = { imageView, sampler };
            return *this;
        }

        DescriptorBuilder& setDefaultTexture(ImageViewResource* imageView, SamplerResource* sampler) {
            m_defaultImageView = imageView;
            m_defaultSampler = sampler;
            return *this;
        }

    private:
        struct BufferInfo {
            Buffer* buffer;
            size_t size;
        };

        struct TextureInfo {
            ImageViewResource* imageView;
            SamplerResource* sampler;
        };

        bool createDescriptorSetLayouts() {
            // Use reflection data to create layouts
            std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> setBindings;

            // Add UBO bindings
            for (const auto& ubo : m_reflection.getUniformBuffers()) {
                VkDescriptorSetLayoutBinding binding{};
                binding.binding = ubo.binding;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                binding.descriptorCount = 1;
                binding.stageFlags = ubo.stageFlags;

                setBindings[ubo.set].push_back(binding);
            }

            // Add resource bindings (textures, etc.)
            for (const auto& resource : m_reflection.getResourceBindings()) {
                VkDescriptorSetLayoutBinding binding{};
                binding.binding = resource.binding;
                binding.descriptorType = resource.descriptorType;
                binding.descriptorCount = 1;
                binding.stageFlags = resource.stageFlags;

                setBindings[resource.set].push_back(binding);
            }

            // Create layouts for each set
            m_setLayouts.clear();
            for (const auto& [set, bindings] : setBindings) {
                VkDescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
                layoutInfo.pBindings = bindings.data();

                VkDescriptorSetLayout layout;
                if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
                    //Logger::get().error("Failed to create descriptor set layout for set {}", set);
                    return false;
                }

                m_setLayouts[set] = layout;
            }

            return true;
        }

        bool allocateDescriptorSets() {
            // Skip if no layouts
            if (m_setLayouts.empty()) {
                return true;
            }

            // Approach 1: Only include valid layouts without gaps
            std::vector<VkDescriptorSetLayout> rawLayouts;
            std::vector<uint32_t> setIndices;  // Keep track of which set each layout corresponds to

            for (const auto& [set, layout] : m_setLayouts) {
                //Logger::get().info("Set {}: layout = {}", set,
                //    layout != VK_NULL_HANDLE ? "VALID" : "NULL");
                if (layout != VK_NULL_HANDLE) {  // Ensure layout is valid
                    rawLayouts.push_back(layout);
                    setIndices.push_back(set);
                }
            }

            // Allocate descriptor sets for valid layouts
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_descriptorPool->handle();
            allocInfo.descriptorSetCount = static_cast<uint32_t>(rawLayouts.size());
            allocInfo.pSetLayouts = rawLayouts.data();

            std::vector<VkDescriptorSet> rawSets(rawLayouts.size());
            if (vkAllocateDescriptorSets(m_device, &allocInfo, rawSets.data()) != VK_SUCCESS) {
                //Logger::get().error("Failed to allocate descriptor sets");
                return false;
            }

            // Create descriptor set wrappers, placing them at their correct set indices
            m_descriptorSets.resize(setIndices.empty() ? 0 : *std::max_element(setIndices.begin(), setIndices.end()) + 1);
            for (size_t i = 0; i < rawSets.size(); i++) {
                m_descriptorSets[setIndices[i]] = std::make_unique<DescriptorSetResource>(m_device, rawSets[i]);
            }

            return true;
        }


        bool updateDescriptorSetsFromReflection() {
            // Pre-allocate infos
            std::vector<VkDescriptorBufferInfo> bufferInfos;
            std::vector<VkDescriptorImageInfo> imageInfos;
            std::vector<std::pair<VkWriteDescriptorSet, size_t>> descriptorWritesWithIndices;

            // Process UBOs
            for (const auto& ubo : m_reflection.getUniformBuffers()) {
                // Find the buffer for this UBO
                BufferInfo bufferInfo = { nullptr, 0 };

                // Try to find registered buffer by name
                auto it = m_registeredBuffers.find(ubo.name);
                if (it != m_registeredBuffers.end()) {
                    bufferInfo = it->second;
                }
                else {
                    //Logger::get().warning("UBO {} not registered, skipping", ubo.name);
                    continue;
                }

                if (!bufferInfo.buffer) {
                    //Logger::get().warning("Buffer for UBO {} is null, skipping", ubo.name);
                    continue;
                }

                if (ubo.set >= m_descriptorSets.size()) {
                    //Logger::get().error("UBO references set {} which doesn't exist", ubo.set);
                    continue;
                }

                // Add buffer info
                size_t bufferInfoIndex = bufferInfos.size();
                bufferInfos.push_back({
                    bufferInfo.buffer->getBuffer(),
                    0,
                    bufferInfo.size
                    });

                // Create write descriptor
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_descriptorSets[ubo.set]->handle();
                write.dstBinding = ubo.binding;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.descriptorCount = 1;
                write.pBufferInfo = nullptr; // Will set later

                descriptorWritesWithIndices.push_back({ write, bufferInfoIndex });
                //Logger::get().info("Set up UBO: {} with size {} bytes", ubo.name, bufferInfo.size);
            }

            // Process resources (textures, etc.)
            for (const auto& resource : m_reflection.getResourceBindings()) {
                if (resource.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    // Find the texture for this resource
                    TextureInfo textureInfo = { nullptr, nullptr };

                    // Try to find registered texture by name
                    auto it = m_registeredTextures.find(resource.name);
                    if (it != m_registeredTextures.end()) {
                        textureInfo = it->second;
                    }
                    else {
                        // Use default texture
                        if (m_defaultImageView && m_defaultSampler) {
                            textureInfo = { m_defaultImageView, m_defaultSampler };
                            //Logger::get().info("Using default texture for {}", resource.name);
                        }
                        else {
                            //Logger::get().warning("Texture {} not registered and no default texture, skipping", resource.name);
                            continue;
                        }
                    }

                    if (!textureInfo.imageView || !textureInfo.sampler) {
                        //Logger::get().warning("Image view or sampler for texture {} is null, skipping", resource.name);
                        continue;
                    }

                    if (resource.set >= m_descriptorSets.size()) {
                        //Logger::get().error("Resource references set {} which doesn't exist", resource.set);
                        continue;
                    }

                    // Add image info
                    size_t imageInfoIndex = imageInfos.size();
                    imageInfos.push_back({
                        textureInfo.sampler->handle(),
                        textureInfo.imageView->handle(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        });

                    // Create write descriptor
                    VkWriteDescriptorSet write{};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = m_descriptorSets[resource.set]->handle();
                    write.dstBinding = resource.binding;
                    write.dstArrayElement = 0;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.descriptorCount = 1;
                    write.pImageInfo = nullptr; // Will set later

                    descriptorWritesWithIndices.push_back({ write, imageInfoIndex });
                    //Logger::get().info("Set up texture: {}", resource.name);
                }
                // Add support for other descriptor types here as needed
            }

            // Create final descriptor writes with correct pointers
            std::vector<VkWriteDescriptorSet> descriptorWrites;
            descriptorWrites.reserve(descriptorWritesWithIndices.size());

            for (const auto& [write, index] : descriptorWritesWithIndices) {
                VkWriteDescriptorSet finalWrite = write;
                if (write.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                    finalWrite.pBufferInfo = &bufferInfos[index];
                }
                else if (write.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    finalWrite.pImageInfo = &imageInfos[index];
                }
                descriptorWrites.push_back(finalWrite);
            }

            // Update descriptors
            if (!descriptorWrites.empty()) {
                vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
                    descriptorWrites.data(), 0, nullptr);
                //Logger::get().info("Updated {} descriptor writes", descriptorWrites.size());
            }

            return true;
        }

        VkDevice m_device;
        const ShaderReflection& m_reflection;
        std::unique_ptr<DescriptorPoolResource>& m_descriptorPool;

        std::unordered_map<uint32_t, VkDescriptorSetLayout> m_setLayouts;
        std::vector<std::unique_ptr<DescriptorSetResource>> m_descriptorSets;

        std::unordered_map<std::string, BufferInfo> m_registeredBuffers;
        std::unordered_map<std::string, TextureInfo> m_registeredTextures;

        ImageViewResource* m_defaultImageView = nullptr;
        SamplerResource* m_defaultSampler = nullptr;
    };



    void VulkanBackend::initializeOverlayWorkflow() {
        // Run the example workflow to create test overlays

        // Initialize the overlay system
        initializeOverlaySystem();

        // Asset loading moved to a single location to prevent multiple loads
        // m_overlayManager->load_master_asset("assets/proper_triangle.taf");

    }

        void VulkanBackend::createEnhancedScene() {
            //Logger::get().info("=== CREATING ENHANCED SCENE (FIXED) ===");

            // CLEAR any existing octree
            AABBQ worldBounds{
                Vec3Q::fromFloat(glm::vec3(-20.0f, -20.0f, -20.0f)),
                Vec3Q::fromFloat(glm::vec3(20.0f,  20.0f,  20.0f))
            };
            m_sceneOctree = Octree<RenderableObject>(worldBounds);

            //Logger::get().info("Creating exactly 25 objects...");

            // Create exactly 25 objects - no more, no less
            for (int i = 0; i < 25; i++) {
                RenderableObject obj;
                obj.meshID = m_cubeMeshID;
                obj.materialID = m_materialIDs[i % m_materialIDs.size()];
                obj.instanceID = i;
                obj.flags = 1; // Visible

                // Simple grid positioning
                float spacing = 2.5f;
                float x = (i % 5 - 2) * spacing; // -6, -3, 0, 3, 6
                float z = (i / 5 - 2) * spacing; // -6, -3, 0, 3, 6  
                float y = 5.0f;

                obj.transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
                obj.prevTransform = obj.transform;

                glm::mat4& transform = obj.transform;
                //Logger::get().info("Object {}: matrix row 0=({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                //    i, transform[0][0], transform[0][1], transform[0][2], transform[0][3]);
                //Logger::get().info("Object {}: matrix row 1=({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                //    i, transform[1][0], transform[1][1], transform[1][2], transform[1][3]);
                //Logger::get().info("Object {}: matrix row 2=({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                //    i, transform[2][0], transform[2][1], transform[2][2], transform[2][3]);
                //Logger::get().info("Object {}: matrix row 3=({:.2f}, {:.2f}, {:.2f}, {:.2f})",
                //    i, transform[3][0], transform[3][1], transform[3][2], transform[3][3]);

                // Calculate bounds
                AABBF localBounds{ glm::vec3(-0.5f), glm::vec3(0.5f) };
                AABBF worldBounds = transformAABB(obj.transform, localBounds);
                obj.bounds = AABBQ::fromFloat(worldBounds);

                //Logger::get().info("Creating object {}: pos=({:.1f},{:.1f},{:.1f})", i, x, y, z);

                // Insert ONCE into octree
                try {
                    m_sceneOctree.insert(obj, obj.bounds);
                    //Logger::get().info("  Inserted object {} successfully", i);
                }
                catch (const std::exception& e) {
                    //Logger::get().error("  Failed to insert object {}: {}", i, e.what());
                }
            }

            // Verify exactly 25 objects
            auto allOctreeObjects = m_sceneOctree.getAllObjects();
            //Logger::get().info("VERIFICATION: Expected 25 objects, octree has {}", allOctreeObjects.size());

            if (allOctreeObjects.size() != 25) {
                //Logger::get().error("CRITICAL: Object count mismatch! Expected 25, got {}",
                //    allOctreeObjects.size());

                // Log all objects to find duplicates
                std::map<uint32_t, int> instanceCounts;
                for (const auto& obj : allOctreeObjects) {
                    instanceCounts[obj.instanceID]++;
                }

                for (const auto& [instanceID, count] : instanceCounts) {
                    if (count > 1) {
                        //Logger::get().error("  Instance {} appears {} times (DUPLICATE!)",
                        //    instanceID, count);
                    }
                }
            }
            m_clusteredRenderer->updateGPUBuffers();

            // Create simple lighting
            std::vector<ClusterLight> lights;
            ClusterLight mainLight;
            mainLight.position = glm::vec3(0.0f, 10.0f, 5.0f);
            mainLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
            mainLight.intensity = 3.0f;
            mainLight.radius = 50.0f;
            mainLight.type = 0;
            lights.push_back(mainLight);

            m_clusteredRenderer->updateLights(lights);
            //Logger::get().info("Scene creation complete");
        }


        void VulkanBackend::createSceneLighting() {
            std::vector<ClusterLight> lights;

            tremor::gfx::ClusterLight mainLight;
            mainLight.position = glm::vec3(0.0f, 20.0f, 10.0f);
            mainLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
            mainLight.intensity = 5.0f;
            mainLight.radius = 100.0f;
            mainLight.type = 0;
            lights.push_back(mainLight);

            m_clusteredRenderer->updateLights(lights);
        }

        void VulkanBackend::createTaffyMeshes() {
            //Logger::get().info("=== LOADING TAFFY ASSETS ===");


            // Try to load some Taffy assets
            std::vector<std::string> asset_paths = {
				"assets/triangle_hot_pink.taf",
            };
            std::cout << "=== ASSET LOADING DEBUG ===" << std::endl;
            std::cout << "asset_paths.size(): " << asset_paths.size() << std::endl;
            
            for (size_t i = 0; i < asset_paths.size(); ++i) {
                //Logger::get().info("asset_paths[ {} ]: {}", i, asset_paths[i]);
            }

            if (asset_paths.empty()) {
                std::cout << "ERROR: asset_paths is empty!" << std::endl;
            }
            else {
                //Logger::get().info("About to start loading loop...");
            }
            
            for (const auto& path : asset_paths) {

                auto loaded_asset = taffy_loader_->load_asset(path);
                if (loaded_asset) {
                    loaded_assets_.push_back(std::move(loaded_asset));
                    //Logger::get().info("Successfully loaded Taffy asset: {}", path);
                }
                else {
                    //Logger::get().warning("Failed to load Taffy asset: {}", path);

                    // Fallback to creating a simple mesh manually
                    if (path.find("cube") != std::string::npos) {
                    }
                }
            }

            // If no assets loaded, create fallback content
            if (loaded_assets_.empty()) {
                //Logger::get().info("No Taffy assets loaded, creating fallback content");
            }
            else {
                //Logger::get().info("Loaded {} Taffy assets", loaded_assets_.size());
            }
        }

        void VulkanBackend::createTaffyScene() {
            //Logger::get().info("=== CREATING TAFFY-BASED SCENE ===");

            // Clear existing octree
            AABBQ worldBounds{
                Vec3Q::fromFloat(glm::vec3(-50.0f, -50.0f, -50.0f)),
                Vec3Q::fromFloat(glm::vec3(50.0f, 50.0f, 50.0f))
            };
            m_sceneOctree = Octree<RenderableObject>(worldBounds);

            if (loaded_assets_.empty()) {
                //Logger::get().error("No loaded assets to create scene from");
                return;
            }

            // Create objects using loaded Taffy assets
            int object_count = 0;
            const int grid_size = 5;
            const float spacing = 8.0f;

            for (int x = 0; x < grid_size; ++x) {
                for (int z = 0; z < grid_size; ++z) {
                    // Pick a random asset (or cycle through them)
                    const auto& asset = loaded_assets_[object_count % loaded_assets_.size()];

                    RenderableObject obj;
                    obj.meshID = asset->get_primary_mesh_id();
                    obj.materialID = asset->get_primary_material_id();
                    obj.instanceID = object_count;
                    obj.flags = 1; // Visible

                    // Position in grid
                    float pos_x = (x - grid_size / 2) * spacing;
                    float pos_z = (z - grid_size / 2) * spacing;
                    float pos_y = 0.0f; // Ground level

                    obj.transform = glm::translate(glm::mat4(1.0f), glm::vec3(pos_x, pos_y, pos_z));
                    obj.prevTransform = obj.transform;

                    // Use bounds from the Taffy mesh
                    glm::vec3 bounds_min = asset->meshes[0]->get_bounds_min();
                    glm::vec3 bounds_max = asset->meshes[0]->get_bounds_max();

                    // Transform bounds to world space
                    AABBF localBounds{ bounds_min, bounds_max };
                    AABBF worldBounds = transformAABB(obj.transform, localBounds);
                    obj.bounds = AABBQ::fromFloat(worldBounds);

                    //Logger::get().info("Creating Taffy object {}: pos=({:.1f},{:.1f},{:.1f})",
                    //    object_count, pos_x, pos_y, pos_z);

                    // Insert into octree
                    try {
                        m_sceneOctree.insert(obj, obj.bounds);
                        //Logger::get().info("  Inserted object {} successfully", object_count);
                    }
                    catch (const std::exception& e) {
                        //Logger::get().error("  Failed to insert object {}: {}", object_count, e.what());
                    }

                    object_count++;
                }
            }
        }

        // Create a method to create the UBO
        bool VulkanBackend::createUniformBuffer() {
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

        bool VulkanBackend::createMinimalMeshShaderPipeline() {
            // Load shaders
            auto taskShader = ShaderModule::compileFromFile(device, "shaders/diag.task");
            auto meshShader = ShaderModule::compileFromFile(device, "shaders/diag.mesh");
            auto fragShader = ShaderModule::compileFromFile(device, "shaders/diag.frag");

            if (!taskShader || !meshShader || !fragShader) {
                //Logger::get().error("Failed to compile mesh shaders");
                return false;
            }

            // Create simple pipeline layout (no descriptors yet)
            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
                //Logger::get().error("Failed to create pipeline layout");
                return false;
            }

            m_meshShaderPipelineLayout = std::make_unique<PipelineLayoutResource>(device, pipelineLayout);
            //Logger::get().info("Created mesh shader pipeline layout");

            // Create shader stages
            std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
                taskShader->createShaderStageInfo(),
                meshShader->createShaderStageInfo(),
                fragShader->createShaderStageInfo()
            };

            // Simple pipeline state
            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling to start
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

            VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };

            VkPipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 2;
            dynamicState.pDynamicStates = dynamicStates;

            // Setup the pipeline using dynamic rendering
            VkPipelineRenderingCreateInfoKHR renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            renderingInfo.colorAttachmentCount = 1;
            VkFormat colorFormat = vkSwapchain->imageFormat();
            renderingInfo.pColorAttachmentFormats = &colorFormat;

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.pNext = &renderingInfo;  // Use dynamic rendering
            pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
            pipelineInfo.pStages = shaderStages.data();
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = pipelineLayout;

            // Create the pipeline
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkResult result = vkCreateGraphicsPipelines(
                device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

            if (result != VK_SUCCESS) {
                //Logger::get().error("Failed to create mesh shader pipeline: {}", (int)result);
                return false;
            }

            m_meshShaderPipeline = std::make_unique<PipelineResource>(device, pipeline);
            //Logger::get().info("Created mesh shader pipeline successfully");

            return true;
        }

        // Add a method to update the UBO every frame
        void VulkanBackend::updateUniformBuffer() {
            static auto startTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

            // Position camera to see the 5x5 grid clearly


            UniformBufferObject ubo{};
            ubo.model = glm::mat4(1.0f); // Identity - objects have their own transforms
            ubo.view = cam.getViewMatrix();
            ubo.proj = cam.getProjectionMatrix();
            ubo.cameraPos = cam.getLocalPosition();

            m_uniformBuffer->update(&ubo, sizeof(ubo));

            // Debug camera position occasionally
            static int frameCount = 0;
            if (++frameCount % 60 == 0) { // Every 60 frames
                glm::vec3 pos = cam.getLocalPosition();
                glm::vec3 forward = cam.getForward();
                //Logger::get().info("Camera: pos=({:.1f},{:.1f},{:.1f}), forward=({:.2f},{:.2f},{:.2f})",
                //    pos.x, pos.y, pos.z, forward.x, forward.y, forward.z);
            }


        }

        std::unique_ptr<Buffer> m_lightBuffer;


        bool VulkanBackend::createLightBuffer() {
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
            light.position = glm::vec3(0.0f, 0.0f, 5.0f);  // Light position above and in front
            light.color = glm::vec3(1.0f, 1.0f, 1.0f);     // White light
            light.ambientStrength = 0.1f;                  // Subtle ambient light
            light.diffuseStrength = 0.7f;                  // Strong diffuse component
            light.specularStrength = 0.5f;                 // Medium specular highlights
            light.shininess = 32.0f;                       // Moderately focused highlights

            m_lightBuffer->update(&light, sizeof(light));
            //Logger::get().info("Light buffer created successfully");
            return true;
        }

        bool VulkanBackend::updateLight() {
            static auto startTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();


            LightUBO light{};
            light.position = glm::vec3(sin(time) * 5, 0.0, cos(time) * 5);  // Light position above and in front
            light.color = glm::vec3(1.0f, 1.0f, 1.0f);     // White light
            light.ambientStrength = 0.1f;                  // Subtle ambient light
            light.diffuseStrength = 0.7f;                  // Strong diffuse component
            light.specularStrength = 0.5f;                 // Medium specular highlights
            light.shininess = 32.0f;                       // Moderately focused highlights

            m_lightBuffer->update(&light, sizeof(light));
            //Logger::get().info("Light buffer created successfully");
            return true;

        }

        std::unique_ptr<Buffer> m_materialBuffer;

        // Add this function to create and initialize the material buffer
        bool VulkanBackend::createMaterialBuffer() {
            // Create the material uniform buffer
            VkDeviceSize bufferSize = sizeof(MaterialUBO);
            m_materialBuffer = std::make_unique<Buffer>(
                device,
                physicalDevice,
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Initialize with default PBR material values
            MaterialUBO material{};
            material.baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // White base color
            material.metallic = 0.0f;                                // Non-metallic by default
            material.roughness = 0.5f;                               // Medium roughness
            material.ao = 1.0f;                                      // Full ambient occlusion
            material.emissiveFactor = 0.0f;                          // No emission by default
            material.emissiveColor = glm::vec3(1.0f, 1.0f, 1.0f);    // White emission color
            material.padding = 0.0f;                                 // Padding for alignment

            // No textures by default
            material.hasAlbedoMap = 1;
            material.hasNormalMap = 0;
            material.hasMetallicRoughnessMap = 0;
            material.hasEmissiveMap = 0;
            material.hasOcclusionMap = 0;

            m_materialBuffer->update(&material, sizeof(material));
            //Logger::get().info("Material buffer created successfully");
            return true;
        }


        bool VulkanBackend::createCommandPool() {
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
                //Logger::get().error("Failed to create command pool");
                return false;
            }

            // Store in RAII wrapper
            m_commandPool = std::make_unique<CommandPoolResource>(device, commandPool);
            //Logger::get().info("Command pool created successfully");

            return true;
        }

        bool VulkanBackend::createCommandBuffers() {
            // Make sure we have a command pool
            if (!m_commandPool || !*m_commandPool) {
                //Logger::get().error("Cannot create command buffers without a valid command pool");
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
                //Logger::get().error("Failed to allocate command buffers");
                return false;
            }

            //Logger::get().info("Command buffers created successfully: {}", m_commandBuffers.size());
            return true;
        }

        bool VulkanBackend::createSyncObjects() {
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
                    //Logger::get().error("Failed to create image available semaphore for frame {}", i);
                    return false;
                }
                m_imageAvailableSemaphores[i] = SemaphoreResource(device, imageAvailableSemaphore);

                // Create render finished semaphore
                VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
                if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {
                    //Logger::get().error("Failed to create render finished semaphore for frame {}", i);
                    return false;
                }
                m_renderFinishedSemaphores[i] = SemaphoreResource(device, renderFinishedSemaphore);

                // Create in-flight fence
                VkFence inFlightFence = VK_NULL_HANDLE;
                if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
                    //Logger::get().error("Failed to create in-flight fence for frame {}", i);
                    return false;
                }
                m_inFlightFences[i] = FenceResource(device, inFlightFence);
            }

            //Logger::get().info("Synchronization objects created successfully");
            return true;
        }

        // Add a simple vertex buffer class
        bool VulkanBackend::createFramebuffers() {
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
                    //Logger::get().error("Failed to create framebuffer {}: {}", i, e.what());
                    return false;
                }
            }

            //Logger::get().info("Created {} framebuffers", m_framebuffers.size());
            return true;
        }

        // Add member variable to hold framebuffers
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


        bool VulkanBackend::createRenderPass() {
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
                //Logger::get().info("Render pass created successfully");
            }
            catch (const std::exception& e) {
                //Logger::get().error("Failed to create render pass: {}", e.what());
                throw; // Rethrow to be caught by initialize
            }
            return true;
        }

        // Make sure you have a method to create depth resources
        bool VulkanBackend::createDepthResources() {
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
            imageInfo.samples = m_msaaSamples;

            // Create the image with RAII wrapper
            m_depthImage = std::make_unique<ImageResource>(device);
            if (vkCreateImage(device, &imageInfo, nullptr, &m_depthImage->handle()) != VK_SUCCESS) {
                //Logger::get().error("Failed to create depth image");
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
                //Logger::get().error("Failed to allocate depth image memory");
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
                //Logger::get().error("Failed to create depth image view");
                return false;
            }

            //Logger::get().info("Depth resources created successfully");
            return true;
        }

        // Helper function to find a suitable depth format
        VkFormat VulkanBackend::findDepthFormat() {
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

        VkSampleCountFlagBits VulkanBackend::getMaxUsableSampleCount() {
            VkPhysicalDeviceProperties physicalDeviceProperties;
            vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

            VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & 
                                       physicalDeviceProperties.limits.framebufferDepthSampleCounts;
            
            // Try to use 4x MSAA for good quality without too much performance impact
            //if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
            //if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }
            return VK_SAMPLE_COUNT_1_BIT;
        }

        bool VulkanBackend::createColorResources() {
            VkExtent2D swapChainExtent = vkSwapchain->extent();

            // Create multisampled color image
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = swapChainExtent.width;
            imageInfo.extent.height = swapChainExtent.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = vkSwapchain->imageFormat();
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.samples = m_msaaSamples;
            imageInfo.flags = 0;

            m_colorImage = std::make_unique<ImageResource>(device);
            if (vkCreateImage(device, &imageInfo, nullptr, &m_colorImage->handle()) != VK_SUCCESS) {
                //Logger::get().error("Failed to create color image for MSAA");
                return false;
            }

            // Allocate memory
            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(device, *m_colorImage, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = res->findMemoryType(memRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            m_colorImageMemory = std::make_unique<DeviceMemoryResource>(device);
            if (vkAllocateMemory(device, &allocInfo, nullptr, &m_colorImageMemory->handle()) != VK_SUCCESS) {
                //Logger::get().error("Failed to allocate color image memory");
                return false;
            }

            vkBindImageMemory(device, *m_colorImage, *m_colorImageMemory, 0);

            // Create image view
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = *m_colorImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = vkSwapchain->imageFormat();
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            m_colorImageView = std::make_unique<ImageViewResource>(device);
            if (vkCreateImageView(device, &viewInfo, nullptr, &m_colorImageView->handle()) != VK_SUCCESS) {
                //Logger::get().error("Failed to create color image view");
                return false;
            }

            //Logger::get().info("MSAA color resources created successfully ({}x MSAA)", 
            //                  static_cast<uint32_t>(m_msaaSamples));
            return true;
        }


        void VulkanBackend::beginFrame() {
            static int frameCount = 0;
            if (frameCount < 5) {
                Logger::get().critical("beginFrame() called, frame {}", frameCount++);
            }

            updateOverlaySystem();

            // Update model editor
            if (m_editorIntegration) {
                // Calculate deltaTime (simple approach)
                static auto lastFrameTime = std::chrono::high_resolution_clock::now();
                auto currentTime = std::chrono::high_resolution_clock::now();
                float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
                lastFrameTime = currentTime;
                
                m_editorIntegration->update(deltaTime);
            }

            cam.extent = vkSwapchain.get()->extent();

            updateUniformBuffer();
            updateLight();

            cam.setClipPlanes(0.01f,100000000000.0f);
            // Camera orbiting at a reasonable distance for a 0.5 unit cube
            float time = std::chrono::steady_clock::now().time_since_epoch().count()/1000000000.0f;
            cam.setPosition({sin(time)*10.0f, 2.0f, cos(time)*10.0f});
            cam.lookAt({0.0f,0.0f,0.0f});  // Look at origin where the cube is centered

            // Add camera debug info
            glm::vec3 camPos = cam.getLocalPosition();
            glm::vec3 camForward = cam.getForward();
            //Logger::get().info("Camera pos: ({:.2f}, {:.2f}, {:.2f}), forward: ({:.2f}, {:.2f}, {:.2f})",
            //    camPos.x, camPos.y, camPos.z, camForward.x, camForward.y, camForward.z);

            m_clusteredRenderer->setCamera(&cam);
            m_clusteredRenderer->buildClusters(&cam, (m_sceneOctree));

            // Wait for previous frame to finish with timeout to prevent hangs
            if (m_inFlightFences.size() > 0) {
                // Debug: Log fence status before waiting
                // Logger::get().info("Waiting for fence {}, total fences: {}", 
                //                   currentFrame, m_inFlightFences.size());
                
                // Use 1 second timeout instead of infinite wait
                VkResult fenceResult = vkWaitForFences(device, 1, &m_inFlightFences[currentFrame].handle(), VK_TRUE, 1000000000ULL);
                if (fenceResult == VK_TIMEOUT) {
                    Logger::get().error("Fence wait timeout for frame {}! GPU might be hung.", currentFrame);
                    // Skip this frame to avoid complete hang
                    return;
                } else if (fenceResult != VK_SUCCESS) {
                    Logger::get().error("vkWaitForFences failed with result: {} for fence index {}", 
                                      static_cast<int>(fenceResult), currentFrame);
                    
                    // Check device status
                    VkPhysicalDeviceProperties deviceProps;
                    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
                    Logger::get().error("Device: {}", deviceProps.deviceName);
                    
                    return;
                }
                
                // Logger::get().info("Fence {} signaled successfully", currentFrame);
            }

            if (!vkSwapchain || !vkSwapchain.get()) {
                //Logger::get().error("Swapchain is null in beginFrame()");
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
                vkSwapchain.get()->recreate(width, height);
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
                if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT && m_colorImageView) {
                    // Use MSAA color attachment
                    colorAttachment.imageView = *m_colorImageView;
                    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.2f, 1.0f} };  // Dark blue
                    
                    // Add resolve attachment for MSAA
                    colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                    colorAttachment.resolveImageView = vkSwapchain.get()->imageViews()[imageIndex];
                    colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                } else {
                    // Non-MSAA path
                    colorAttachment.imageView = vkSwapchain.get()->imageViews()[imageIndex];
                    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.2f, 1.0f} };  // Dark blue
                }

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


            


            //if (m_taffyMeshShaderManager) {
            //}

            // Render using clustered renderer
            //m_clusteredRenderer->render(m_commandBuffers[currentFrame], &cam);
            
            // Removed runtime overlay manipulation to prevent modifying the asset
            // demonstrateOverlayControls();
            
            // Asset should already be loaded during initialization
            // Removed duplicate load to prevent multiple instances

			// Debug camera and MVP
			{
				glm::vec3 camPos = cam.getLocalPosition();
				glm::vec3 camForward = cam.getForward();
				/*Logger::get().info("Camera position: ({:.2f}, {:.2f}, {:.2f})", camPos.x, camPos.y, camPos.z);
				//Logger::get().info("Camera forward: ({:.2f}, {:.2f}, {:.2f})", camForward.x, camForward.y, camForward.z);*/
				
				// Force camera update
				cam.update(0.0f);
				
				glm::mat4 mvp = cam.getViewProjectionMatrix();
				/*Logger::get().info("View-Projection Matrix:");
				for (int i = 0; i < 4; i++) {
					Logger::get().info("  [{:.3f}, {:.3f}, {:.3f}, {:.3f}]", 
						mvp[i][0], mvp[i][1], mvp[i][2], mvp[i][3]);
				}*/

				// Also check view and projection separately
				glm::mat4 view = cam.getViewMatrix();
				glm::mat4 proj = cam.getProjectionMatrix();
				/*Logger::get().info("View Matrix [3]: ({:.3f}, {:.3f}, {:.3f})", view[3][0], view[3][1], view[3][2]);
				//Logger::get().info("Projection Matrix [0][0]: {:.3f}, [1][1]: {:.3f}", proj[0][0], proj[1][1]);*/
			}
			
			// TEST: Check rendering state
			{
				//Logger::get().info("=== RENDER STATE CHECK ===");
				//Logger::get().info("Using dynamic rendering: {}", dr ? "YES" : "NO");
				//Logger::get().info("Command buffer: {}", (void*)m_commandBuffers[currentFrame]);
				//Logger::get().info("Current swapchain extent: {}x{}", vkSwapchain.get()->extent().width, vkSwapchain.get()->extent().height);
			}
			
            //hot_pink_enabled = std::chrono::steady_clock::now().time_since_epoch().count() % 1000000000 > 500000000;
            
            static bool last_hot_pink_enabled = false;
            if (hot_pink_enabled != last_hot_pink_enabled) {
                if(hot_pink_enabled){
                    m_overlayManager->loadAssetWithOverlay("assets/cube.taf","assets/overlays/tri_hot_pink.tafo");
                }
                else {
                    m_overlayManager->clear_overlays("assets/cube.taf");
                }
                last_hot_pink_enabled = hot_pink_enabled;
            }

            // Check if asset reload was requested (could be set by keyboard input)
            if (reload_assets_requested) {
                //Logger::get().info("Reloading assets...");
                m_overlayManager->reloadAsset("assets/cube.taf");
                reload_assets_requested = false;
                //Logger::get().info("Assets reloaded!");
            }

            m_overlayManager->checkForPipelineUpdates();


			//Logger::get().critical("About to call renderMeshAsset");
			//Logger::get().critical("  m_overlayManager pointer: {}", (void*)m_overlayManager.get());
			//Logger::get().critical("  VulkanBackend instance: {}", (void*)this);
			
			if (!m_overlayManager) {
				//Logger::get().error("m_overlayManager is null!");
				return;
			}
			
			//m_overlayManager->renderMeshAsset("assets/cube.taf", m_commandBuffers[currentFrame], cam.getViewProjectionMatrix());


        }

        /**
 * Initialize overlay system - call this in your existing initialize() method
 */

        void VulkanBackend::initializeOverlaySystem() {
            std::cout << "🎨 Initializing Taffy Overlay System..." << std::endl;

            // Use the raw device handles from your existing vkDevice

            last_overlay_check_ = std::chrono::steady_clock::now();

            std::cout << "✅ Overlay system initialized!" << std::endl;
        }

        /**
         * Create development overlays for testing
         */
        void VulkanBackend::createDevelopmentOverlays() {
            std::string overlay_dir = "assets/overlays";
            std::filesystem::create_directories(overlay_dir);

            // Create preset overlays for testing
            
            // Create audio test assets
            std::string audio_dir = "assets/audio";
            std::filesystem::create_directories(audio_dir);
            
            // Create a simple 440Hz sine wave (A4 note)
            tremor::taffy::tools::createSineWaveAudioAsset("assets/audio/sine_440hz.taf", 440.0f, 2.0f);
            
            // Create a lower frequency sine wave (220Hz, A3)
            tremor::taffy::tools::createSineWaveAudioAsset("assets/audio/sine_220hz.taf", 220.0f, 2.0f);
            
            // Create font test assets
            std::string font_dir = "assets/fonts";
            std::filesystem::create_directories(font_dir);
            
            // Create test SDF font using Bebas Neue
            
            std::cout << "✅ Development overlays, audio assets, and fonts created!" << std::endl;
        }

        /**
         * Load test asset with overlays
         */
        void VulkanBackend::loadTestAssetWithOverlays() {
            std::cout << "🎮 Loading test assets with overlays..." << std::endl;

            // Define overlays to apply
            std::vector<std::string> overlays = {
                //"assets/overlays/metallic_material.tafo",
                "assets/overlays/hot_pink_vertex.tafo"
            };

            // Load asset with overlays
            
            // Create a test triangle with Vec3Q positions to test our conversion fix
            {
                std::vector<MeshVertex> testVertices;
                
                // Create vertices with Vec3Q positions (testing the conversion)
                MeshVertex v1;
                v1.position = Vec3Q::fromFloat(glm::vec3(-0.5f, -0.5f, 0.0f));
                v1.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                v1.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
                v1.texCoord = glm::vec2(0.0f, 0.0f);
                testVertices.push_back(v1);
                
                MeshVertex v2;
                v2.position = Vec3Q::fromFloat(glm::vec3(0.5f, -0.5f, 0.0f));
                v2.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                v2.color = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f); // Magenta
                v2.texCoord = glm::vec2(1.0f, 0.0f);
                testVertices.push_back(v2);
                
                MeshVertex v3;
                v3.position = Vec3Q::fromFloat(glm::vec3(0.0f, 0.5f, 0.0f));
                v3.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                v3.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
                v3.texCoord = glm::vec2(0.5f, 1.0f);
                testVertices.push_back(v3);
                
                std::vector<uint32_t> testIndices = {0, 1, 2};
                
                std::cout << "📐 Creating test triangle with Vec3Q positions..." << std::endl;
                uint32_t testMeshId = m_clusteredRenderer->loadMesh(testVertices, testIndices, "test_vec3q_triangle");
                
                if (testMeshId != UINT32_MAX) {
                    std::cout << "✅ Test triangle created with mesh ID: " << testMeshId << std::endl;
                } else {
                    std::cout << "❌ Failed to create test triangle" << std::endl;
                }
            }
        }

        /**
         * Update overlay system for hot-reloading
         */
        void VulkanBackend::updateOverlaySystem() {

            auto now = std::chrono::steady_clock::now();
            if (now - last_overlay_check_ > overlay_check_interval_) {
                //m_overlayMeshShaderManager->check_for_overlay_changes();
                last_overlay_check_ = now;
            }
        }

        void VulkanBackend::endFrame() {
        static int endFrameCount = 0;
        if (endFrameCount < 50) {
            //Logger::get().critical("endFrame() called, count {}", endFrameCount++);
        }

        			// Render text overlay
			if (m_textRenderer) {
				// Create orthographic projection for UI
				float width = static_cast<float>(vkSwapchain->extent().width);
				float height = static_cast<float>(vkSwapchain->extent().height);
				glm::mat4 orthoProjection = glm::orthoZO(0.0f, width, 0.0f, height, -10.0f, 1.0f);
				

				m_textRenderer->render(m_commandBuffers[currentFrame], orthoProjection);
			}
			
			// // Update sequencer
			// // Disabled sequencer update to debug hanging issue
		    if (m_sequencerUI) {
				m_sequencerUI->update();
			}
			
			// Render UI elements
			if (m_uiRenderer) {
				// Create orthographic projection for UI
				float width = static_cast<float>(vkSwapchain->extent().width);
				float height = static_cast<float>(vkSwapchain->extent().height);
				glm::mat4 orthoProjection = glm::orthoZO(0.0f, width, 0.0f, height, -10.0f, 1.0f);
				
				m_uiRenderer->render(m_commandBuffers[currentFrame], orthoProjection);
			}

			// Render model editor
			if (m_editorIntegration) {
				m_editorIntegration->render();
			}

        
        // FPS counter
        static auto lastTime = std::chrono::high_resolution_clock::now();
        static int frameCount = 0;
        static float fps = 0.0f;
        
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        frameCount++;
        
        if (deltaTime >= 1.0f) {
            fps = frameCount / deltaTime;
            Logger::get().info("FPS: {:.1f} ({:.2f}ms)", fps, 1000.0f / fps);
            frameCount = 0;
            lastTime = currentTime;
        }
            if (vkDevice->capabilities().dynamicRendering) {
                dr.get()->end(m_commandBuffers[currentFrame]);
            }
            else {
                vkCmdEndRenderPass(m_commandBuffers[currentFrame]);
            }

            // End command buffer recording
            VkResult endResult = vkEndCommandBuffer(m_commandBuffers[currentFrame]);
            if (endResult != VK_SUCCESS) {
                //Logger::get().error("Failed to end command buffer: {}", static_cast<int>(endResult));
                return;
            }
            //Logger::get().info("Command buffer ended successfully");

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

            // Logger::get().info("Submitting command buffer with fence {}", currentFrame);
            VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, m_inFlightFences[currentFrame].handle());
            if (submitResult != VK_SUCCESS) {
                Logger::get().error("Failed to submit command buffer: {}", static_cast<int>(submitResult));
                return;
            }
            // Logger::get().info("Command buffer submitted successfully");

            // Present the image
            VkResult presentResult = vkSwapchain.get()->present(m_currentImageIndex, m_renderFinishedSemaphores[currentFrame]);

            //Logger::get().info("Present result: {}", static_cast<int>(presentResult));

            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
                //Logger::get().info("Recreating swapchain");
                int width, height;
                SDL_GetWindowSize(w, &width, &height);
                vkSwapchain.get()->recreate(width, height);
            }
            else if (presentResult != VK_SUCCESS) {
                //Logger::get().error("Failed to present: {}", static_cast<int>(presentResult));
                return;
            }

            



            // Move to next frame
            currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        }


        void VulkanBackend::createCubeRenderableObject() {
            // Register the cube mesh
            uint32_t cubeID = m_meshRegistry.registerMesh(m_vertexBuffer.get(), "cube");

            // Use material 0 for now
            uint32_t materialID = 0;

            // Create transform matrix
            glm::mat4 transform = glm::mat4(1.0f); // Identity matrix

            // Calculate AABBQ bounds
            AABBF localBounds = {
                glm::vec3(-0.5f, -0.5f, -0.5f),
                glm::vec3(0.5f, 0.5f, 0.5f)
            };

            // Transform bounds to world space
            AABBF worldBounds = transformAABB(transform, localBounds);

            // Convert to quantized space
            AABBQ quantizedBounds = AABBQ::fromFloat(worldBounds);

            // Create the renderable object
            tremor::gfx::RenderableObject cubeObject;
            cubeObject.meshID = cubeID;
            cubeObject.materialID = materialID;
            cubeObject.transform = transform;
            cubeObject.prevTransform = transform;
            cubeObject.bounds = quantizedBounds;

            // Add to octree
            //m_sceneOctree.insert(cubeObject, quantizedBounds);

            //Logger::get().info("Cube added to octree as renderable object");
        }


        bool VulkanBackend::initialize(SDL_Window* window) {
            Logger::get().info("*** VulkanBackend::initialize() CALLED ***");
            //Logger::get().critical("VulkanBackend::initialize called!");
            //Logger::get().critical("  VulkanBackend instance: {}", (void*)this);
            
            static int initCount = 0;
            Logger::get().info("*** VulkanBackend::initialize() call count: {} ***", ++initCount);
            
            if (initCount > 1) {
                //Logger::get().critical("WARNING: VulkanBackend::initialize called multiple times!");
            }

            ShaderReflection combinedReflection;
            m_combinedReflection = combinedReflection;

            w = window;

            createInstance();
            createDeviceAndSwapChain();

            createCommandPool();
            createCommandBuffers();

            // Initialize MSAA
            m_msaaSamples = getMaxUsableSampleCount();
            //Logger::get().info("Using {}x MSAA", static_cast<uint32_t>(m_msaaSamples));

            createDepthResources();
            if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
                createColorResources();
            }
            createUniformBuffer();
            createLightBuffer();
            createMaterialBuffer();

            cam = tremor::gfx::Camera(10.0f, 16.0f / 9.0f, 0.1f, 100.0f);

            cam.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
            cam.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));

            if (vkDevice.get()->capabilities().dynamicRendering) {
                dr = std::make_unique<DynamicRenderer>();
                //Logger::get().info("Dynamic renderer created.");
            }
            else {
                createRenderPass();
                createFramebuffers();
            }

            sm = std::make_unique<ShaderManager>(vkDevice.get()->device());

            createTestTexture();

            createDescriptorSetLayouts();

            createGraphicsPipeline();
            createSyncObjects();

            // Create world octree with 64-bit bounds
            AABBQ worldBounds{
                Vec3Q::fromFloat(glm::vec3(-20.0f, -20.0f, -20.0f)),
                Vec3Q::fromFloat(glm::vec3(20.0f,  20.0f,  20.0f))
            };
            m_sceneOctree = tremor::gfx::Octree<tremor::gfx::RenderableObject>(worldBounds);

            //createCubeRenderableObject();

            // Replace clustered renderer initialization:
            tremor::gfx::ClusterConfig clusterConfig{};
            clusterConfig.xSlices = 16;
            clusterConfig.ySlices = 9;
            clusterConfig.zSlices = 24;
            clusterConfig.nearPlane = 0.1f;
            clusterConfig.farPlane = 1000.0f;
            clusterConfig.logarithmicZ = true;




			m_taffyMeshShaderManager = std::make_unique<tremor::gfx::TaffyMeshShaderManager>(
                device,
                physicalDevice
			);

            m_overlayManager = std::make_unique<TaffyOverlayManager>(
                device,
                physicalDevice,
                vkDevice->capabilities().dynamicRendering ? VkRenderPass(VK_NULL_HANDLE) : *rp,  // Use null for dynamic rendering
                vkSwapchain->extent(),
                vkSwapchain->imageFormat(),
                vkDevice->depthFormat(),
                m_msaaSamples
            );

            m_clusteredRenderer = std::make_unique<tremor::gfx::VulkanClusteredRenderer>(
                device,
                physicalDevice,
                graphicsQueue,        // Add this
                vkDevice->graphicsQueueFamily(),  // Add this
                m_commandPool->handle(),       // Add this - pass the command pool
                clusterConfig
            );

            if (!m_clusteredRenderer->initialize((Format)vkDevice.get()->colorFormat(), (Format)vkDevice.get()->depthFormat())) {
                //Logger::get().error("Failed to initialize enhanced clustered renderer");
                return false;
            }

            // Initialize text renderer
            m_textRenderer = std::make_unique<tremor::gfx::SDFTextRenderer>(
                device, physicalDevice, m_commandPool->handle(), graphicsQueue);
            if (!m_textRenderer->initialize(
                vkDevice->capabilities().dynamicRendering ? VkRenderPass(VK_NULL_HANDLE) : *rp,
                vkSwapchain->imageFormat(),
                m_msaaSamples)) {
                //Logger::get().error("Failed to initialize SDF text renderer");
                // Continue anyway - text rendering is optional
            }

            // Initialize UI renderer
            m_uiRenderer = std::make_unique<tremor::gfx::UIRenderer>(
                device, physicalDevice, m_commandPool->handle(), graphicsQueue);
            if (!m_uiRenderer->initialize(
                vkDevice->capabilities().dynamicRendering ? VkRenderPass(VK_NULL_HANDLE) : *rp,
                vkSwapchain->imageFormat(),
                m_msaaSamples)) {
                Logger::get().error("Failed to initialize UI renderer");
                // Continue anyway - UI is optional
            } else {
                // Set text renderer for the UI
                m_uiRenderer->setTextRenderer(m_textRenderer.get());
                
                // Add some test buttons
                /*m_uiRenderer->addButton("Reload Assets", glm::vec2(20, 100), glm::vec2(160, 40),
                    [this]() {
                        Logger::get().info("🔄 Reload Assets button clicked!");
                        reload_assets_requested = true;
                    });*/
                
                m_toggleOverlayButtonId = m_uiRenderer->addButton("Toggle Overlay", glm::vec2(20, 150), glm::vec2(160, 40),
                    [this]() {
                        Logger::get().info("🎨 Toggle Overlay button clicked!");
                        hot_pink_enabled = !hot_pink_enabled;
                    });
                
                m_modelEditorButtonId = m_uiRenderer->addButton("Model Editor", glm::vec2(20, 200), glm::vec2(160, 40),
                    [this]() {
                        Logger::get().info("🔧 Model Editor button clicked!");
                        if (m_editorIntegration) {
                            bool isEnabled = m_editorIntegration->isEditorEnabled();
                            m_editorIntegration->setEditorEnabled(!isEnabled);
                            Logger::get().info("Model Editor {}", !isEnabled ? "enabled" : "disabled");
                        }
                    });
                
                m_exitButtonId = m_uiRenderer->addButton("Exit", glm::vec2(20, 250), glm::vec2(160, 40),
                    []() {
                        Logger::get().info("❌ Exit button clicked!");
                        SDL_Event quit_event;
                        quit_event.type = SDL_QUIT;
                        SDL_PushEvent(&quit_event);
                    });
                
                // Initialize sequencer
                // m_sequencerUI = std::make_unique<tremor::gfx::SequencerUI>(m_uiRenderer.get());
                // m_sequencerUI->initialize();
                // m_sequencerUI->onStepTriggered([this](int step) {
                //     Logger::get().info("🎵 Sequencer step {} triggered!", step);
                //     // Call the external callback if set
                //     if (m_sequencerCallback) {
                //         m_sequencerCallback(step);
                //     }
                // });
            }

            Logger::get().info("*** VulkanBackend::initialize() ABOUT TO CREATE ModelEditorIntegration ***");
            // Initialize model editor integration
            Logger::get().info("*** VulkanBackend: Creating ModelEditorIntegration ***");
            m_editorIntegration = std::make_unique<tremor::editor::ModelEditorIntegration>(*this);
            Logger::get().info("*** VulkanBackend: ModelEditorIntegration created, calling initialize() ***");
            Logger::get().info("*** VulkanBackend: About to call m_editorIntegration->initialize() ***");
            bool initResult = m_editorIntegration->initialize();
            Logger::get().info("*** VulkanBackend: m_editorIntegration->initialize() returned: {} ***", initResult);
            if (!initResult) {
                Logger::get().error("*** VulkanBackend: Model Editor failed to initialize ***");
                // Continue anyway - editor is optional
            } else {
                Logger::get().info("*** VulkanBackend: Model Editor initialized successfully ***");
            }

            createDevelopmentOverlays();
            // loadTestAssetWithOverlays(); // Commented out - creating test triangle interferes with proper_triangle.taf

            // Load test font
            if (m_textRenderer) {
                if (!m_textRenderer->loadFont("assets/fonts/test_font.taf")) {
                    Logger::get().warning("Failed to load test font - UI will render without text");
                }
                
                m_uiRenderer->addLabel("NOT REAL GAMES",glm::vec2(24,36),0xFF0050FF);
            }

            initializeOverlaySystem();
            initializeOverlayWorkflow();

			// Load the cube instead of triangle
			m_overlayManager->reloadAsset("assets/cube.taf");
			m_overlayManager->load_master_asset("assets/cube.taf");

            // Create enhanced content

            return true;
        };
        void VulkanBackend::shutdown() {};
        
        void VulkanBackend::handleInput(const SDL_Event& event) {
            // Pass input to model editor first (it may consume the event)
            if (m_editorIntegration) {
                m_editorIntegration->handleInput(event);
                if (m_editorIntegration->isEditorEnabled()) {
                    return; // Editor consumed the event
                }
            }
            
            // Pass input to UI renderer
            if (m_uiRenderer) {
                m_uiRenderer->updateInput(event);
            }
        }
        
        void VulkanBackend::setSequencerCallback(SequencerCallback callback) {
            m_sequencerCallback = callback;
            
            // If sequencer is already initialized, update its callback
            if (m_sequencerUI) {
                m_sequencerUI->onStepTriggered([this](int step) {
                    Logger::get().info("🎵 Sequencer step {} triggered!", step);
                    if (m_sequencerCallback) {
                        m_sequencerCallback(step);
                    }
                });
            }
        }


        TextureHandle VulkanBackend::createTexture(const TextureDesc& desc) {
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
            
            // Return the created texture handle
            return TextureHandle(texture);
        }
        BufferHandle createBuffer(const BufferDesc& desc);
        ShaderHandle createShader(const ShaderDesc& desc);

        // Vulkan-specific methods

        bool VulkanBackend::createInstance() {
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
            if (!SDL_Vulkan_GetInstanceExtensions(w, &sdlExtensionCount, nullptr)) {
                //Logger::get().error("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
                return false;
            }

            // Allocate space for extensions (SDL + additional ones)
            auto instanceExtensions = tremor::mem::ScopedAlloc<const char*>(sdlExtensionCount + 5);
            if (!SDL_Vulkan_GetInstanceExtensions(w, &sdlExtensionCount, instanceExtensions.get())) {
                //Logger::get().error("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
                return false;
            }

            // Track our added extensions
            uint32_t additionalExtensionCount = 0;

            // Query available extensions
            uint32_t availableExtensionCount = 0;
            err = vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, nullptr);
            if (err != VK_SUCCESS) {
                //Logger::get().error("Failed to query instance extension count");
                return false;
            }

            // Check for optional extensions
            bool hasSurfaceCapabilities2 = false;
            bool hasDebugUtils = false;

            if (availableExtensionCount > 0) {
                auto extensionProps = tremor::mem::ScopedAlloc<VkExtensionProperties>(availableExtensionCount);

                err = vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, extensionProps.get());
                if (err != VK_SUCCESS) {
                    //Logger::get().error("Failed to enumerate instance extensions");
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
            //Logger::get().info("Available Vulkan layers:");
            for (const auto& layer : availableLayers) {
                //Logger::get().info("  {}", layer.layerName);

                // Check if it's the validation layer
                if (strcmp("VK_LAYER_KHRONOS_validation", layer.layerName) == 0) {
                    enableValidation = true;
                }
            }

            if (!enableValidation) {
                //Logger::get().warning("Validation layer not found. Continuing without validation.");
                //Logger::get().warning("To enable validation, use vkconfig from the Vulkan SDK.");
            }
            else {
                //Logger::get().info("Validation layer found and enabled.");
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
                //Logger::get().error("Failed to create Vulkan instance: {}", (int)err);
                return false;
            }

            instance.reset(inst);

            volkLoadInstance(instance);

            //Logger::get().info("Vulkan instance created successfully");

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
                    //Logger::get().error("Failed to set up debug messenger: {}", (int)err);
                    // Continue anyway, this is not fatal
                }
            }
#endif
            VkSurfaceKHR surf;
            if (!SDL_Vulkan_CreateSurface(w, instance, &surf)) {
                //Logger::get().error("Failed to create Vulkan surface : {}", (int)err);
                return false;
            }

            surface = SurfaceResource(instance, surf);

            //Logger::get().info("Vulkan surface created successfully");

            return true;
        }

#if _DEBUG
        // Debug callback function
        VKAPI_ATTR VkBool32 VKAPI_CALL VulkanBackend::debugCallback(
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


        bool VulkanBackend::createDeviceAndSwapChain() {

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
            swapChainInfo.vsync = false;  // Disable VSync for maximum performance
            swapChainInfo.imageCount = 3;  // Triple buffering for better performance

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

        bool VulkanBackend::createTestTexture() {
            try {
                // Create a simple 2x2 checkerboard texture
                const uint32_t size = 256;
                std::vector<uint8_t> pixels(size * size * 4);

                // Fill with a checkerboard pattern
                for (uint32_t y = 0; y < size; y++) {
                    for (uint32_t x = 0; x < size; x++) {
                        uint8_t color = 255; //((x / 32 + y / 32) % 2) ? 255 : 0;
                        pixels[(y * size + x) * 4 + 0] = color;     // R
                        pixels[(y * size + x) * 4 + 1] = color;     // G
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
                    //Logger::get().error("Failed to create staging buffer for texture");
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
                    //Logger::get().error("Failed to allocate staging buffer memory");
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
                    //Logger::get().error("Failed to create texture image");
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
                    //Logger::get().error("Failed to allocate texture image memory");
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
                    //Logger::get().error("Failed to create texture image view");
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
                samplerInfo.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
                samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                samplerInfo.mipLodBias = 0.0f;
                samplerInfo.minLod = 0.0f;
                samplerInfo.maxLod = 0.0f;

                m_textureSampler = std::make_unique<SamplerResource>(device);
                if (vkCreateSampler(device, &samplerInfo, nullptr, &m_textureSampler->handle()) != VK_SUCCESS) {
                    //Logger::get().error("Failed to create texture sampler");
                    return false;
                }

                // Clean up staging buffer
                vkDestroyBuffer(device, stagingBuffer, nullptr);
                vkFreeMemory(device, stagingBufferMemory, nullptr);

                //Logger::get().info("Texture created successfully");
                return true;
            }
            catch (const std::exception& e) {
                //Logger::get().error("Exception in createTestTexture: {}", e.what());
                return false;
            }
        }

        // Helper command for single-time command submission
        VkCommandBuffer VulkanBackend::beginSingleTimeCommands() {
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

        void VulkanBackend::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
            vkEndCommandBuffer(commandBuffer);

            //Logger::get().info("Ending command buffer...");

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
            //Logger::get().info("Queue submit result: {}", static_cast<int>(submitResult));

            VkResult waitResult = vkQueueWaitIdle(graphicsQueue);
            //Logger::get().info("Queue wait result: {}", static_cast<int>(waitResult));

            vkFreeCommandBuffers(device, *m_commandPool, 1, &commandBuffer);
        }
        bool VulkanBackend::createDescriptorSetLayouts() {
            // Get the combined reflection data
            ShaderReflection combinedReflection;

            // Add all your shaders
            for (const auto& shader : m_pipelineShaders) {
                const ShaderReflection* reflection = shader->getReflection();
                if (reflection) {
                    combinedReflection.merge(*reflection);
                }
            }

            // Find highest set number to determine how many layouts to create
            uint32_t maxSetNumber = 0;
            for (const auto& binding : combinedReflection.getResourceBindings()) {
                maxSetNumber = std::max(maxSetNumber, binding.set);
            }

            // Create descriptor set layouts for each set
            m_descriptorSetLayouts.resize(maxSetNumber + 1);
            for (uint32_t i = 0; i <= maxSetNumber; i++) {
                m_descriptorSetLayouts[i] = combinedReflection.createDescriptorSetLayout(device, i);
                if (!m_descriptorSetLayouts[i]) {
                    //Logger::get().error("Failed to create descriptor set layout for set {}", i);
                    return false;
                }
            }

            // Create pipeline layout using all descriptor set layouts
            m_pipelineLayout = combinedReflection.createPipelineLayout(device);
            if (!m_pipelineLayout) {
                //Logger::get().error("Failed to create pipeline layout");
                return false;
            }

            // Create descriptor pool sized appropriately based on reflection
            m_descriptorPool = combinedReflection.createDescriptorPool(device);
            if (!m_descriptorPool) {
                //Logger::get().error("Failed to create descriptor pool");
                return false;
            }

            return true;
        }

    void VulkanClusteredRenderer::updateMeshBuffers() {
        if (!m_vertexBuffer || !m_meshIndexBuffer || !m_meshInfoBuffer) {
            //Logger::get().error("Mesh buffers not initialized");
            return;
        }

        try {
            // Update vertex buffer - convert Vec3Q to float for shader compatibility
            if (!m_allVertices.empty()) {
                // The shader expects float vertex data, so we need to convert Vec3Q positions to floats
                // Calculate the size for float vertex data
                // OverlayVertex layout: position(6) + normal(3) + texcoord(2) + color(4) = 15 floats
                const size_t floatsPerVertex = 15;
                std::vector<float> floatVertexData;
                floatVertexData.reserve(m_allVertices.size() * floatsPerVertex);
                
                for (size_t i = 0; i < m_allVertices.size(); i++) {
                    const auto& vertex = m_allVertices[i];
                    // Convert Vec3Q position to float
                    glm::vec3 floatPos = vertex.position.toFloat();
                    
                    // IMPORTANT: Pack data to match Taffy's OverlayVertex structure
                    // OverlayVertex layout:
                    // - position at offset 0 (Vec3Q = 24 bytes, stored as 6 floats)
                    // - normal at offset 24 bytes (3 floats)
                    // - uv at offset 36 bytes (2 floats) - UV comes BEFORE color!
                    // - color at offset 44 bytes (4 floats)
                    // Total: 60 bytes (15 floats)
                    
                    // Position as 6 floats to simulate Vec3Q (24 bytes)
                    floatVertexData.push_back(floatPos.x);
                    floatVertexData.push_back(floatPos.y);
                    floatVertexData.push_back(floatPos.z);
                    floatVertexData.push_back(0.0f); // Padding
                    floatVertexData.push_back(0.0f); // Padding
                    floatVertexData.push_back(0.0f); // Padding to make 24 bytes
                    
                    // Normal (3 floats = 12 bytes, at offset 24)
                    floatVertexData.push_back(vertex.normal.x);
                    floatVertexData.push_back(vertex.normal.y);
                    floatVertexData.push_back(vertex.normal.z);
                    
                    // TexCoord (2 floats = 8 bytes, at offset 36) - BEFORE color!
                    floatVertexData.push_back(vertex.texCoord.x);
                    floatVertexData.push_back(vertex.texCoord.y);
                    
                    // Color (4 floats = 16 bytes, at offset 44)
                    floatVertexData.push_back(vertex.color.x);
                    floatVertexData.push_back(vertex.color.y);
                    floatVertexData.push_back(vertex.color.z);
                    floatVertexData.push_back(vertex.color.w);
                }
                
                VkDeviceSize vertexSize = floatVertexData.size() * sizeof(float);
                if (vertexSize <= m_vertexBuffer->getSize()) {
                    m_vertexBuffer->update(floatVertexData.data(), vertexSize);
                    //Logger::get().info("Updated vertex buffer with {} floats ({} vertices)", 
                    //    floatVertexData.size(), m_allVertices.size());
                }
                else {
                    //Logger::get().warning("Vertex buffer too small: need {}, have {}",
                    //    vertexSize, m_vertexBuffer->getSize());
                }
            }

            // Update mesh index buffer
            if (!m_allIndices.empty()) {
                VkDeviceSize indexSize = m_allIndices.size() * sizeof(uint32_t);
                if (indexSize <= m_meshIndexBuffer->getSize()) {
                    m_meshIndexBuffer->update(m_allIndices.data(), indexSize);
                }
                else {
                    //Logger::get().warning("Mesh index buffer too small: need {}, have {}",
                    //    indexSize, m_meshIndexBuffer->getSize());
                }
            }

            // Update mesh info buffer
            if (!m_meshInfos.empty()) {
                VkDeviceSize meshInfoSize = m_meshInfos.size() * sizeof(MeshInfo);
                if (meshInfoSize <= m_meshInfoBuffer->getSize()) {
                    m_meshInfoBuffer->update(m_meshInfos.data(), meshInfoSize);
                }
                else {
                    //Logger::get().warning("Mesh info buffer too small: need {}, have {}",
                    //    meshInfoSize, m_meshInfoBuffer->getSize());
                }
            }

            //Logger::get().info("Updated mesh buffers: {} vertices, {} indices, {} meshes",
            //    m_allVertices.size(), m_allIndices.size(), m_meshInfos.size());
        }
        catch (const std::exception& e) {
            //Logger::get().error("Exception in updateMeshBuffers: {}", e.what());
        }
    }

    void VulkanClusteredRenderer::updateMaterialBuffer() {
        if (!m_materialBuffer) {
            //Logger::get().error("Material buffer not initialized");
            return;
        }

        try {
            if (!m_materials.empty()) {
                VkDeviceSize materialSize = m_materials.size() * sizeof(PBRMaterial);
                if (materialSize <= m_materialBuffer->getSize()) {
                    m_materialBuffer->update(m_materials.data(), materialSize);
                    //Logger::get().info("Updated material buffer with {} materials", m_materials.size());
                }
                else {
                    //Logger::get().warning("Material buffer too small: need {}, have {}",
                    //    materialSize, m_materialBuffer->getSize());
                }
            }
        }
        catch (const std::exception& e) {
            //Logger::get().error("Exception in updateMaterialBuffer: {}", e.what());
        }
    }

    void VulkanClusteredRenderer::updateGPUBuffers(){}

    void VulkanClusteredRenderer::updateUniformBuffers(Camera* camera) {
        if (!camera || !m_uniformBuffer) {
            //Logger::get().error("Camera or uniform buffer is null");
            return;
        }

        try {
            EnhancedClusterUBO ubo{};
            static auto startTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto time = std::chrono::duration<float>(currentTime - startTime).count();

            // Removed automatic camera movement - let the user control the camera
            // camera->update(0.0f);


            ubo.viewMatrix = camera->getViewMatrix();
            ubo.projMatrix = camera->getProjectionMatrix();
            ubo.invViewMatrix = glm::inverse(ubo.viewMatrix);
            ubo.invProjMatrix = glm::inverse(ubo.projMatrix);
            ubo.cameraPos = glm::vec4(camera->getLocalPosition(), 1.0f);
            ubo.clusterDimensions = glm::uvec4(m_config.xSlices, m_config.ySlices, m_config.zSlices, 0);
            ubo.zPlanes = glm::vec4(m_config.nearPlane, m_config.farPlane, static_cast<float>(m_config.zSlices), 0.0f);

            // Screen size
            VkExtent2D extent = camera->extent;
            ubo.screenSize = glm::vec4(
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                1.0f / static_cast<float>(extent.width),
                1.0f / static_cast<float>(extent.height)
            );

            ubo.numLights = static_cast<uint32_t>(m_lights.size());
            ubo.numObjects = static_cast<uint32_t>(m_visibleObjects.size());
            ubo.numClusters = m_totalClusters;

            // Frame data
            static uint32_t frameCounter = 0;

            ubo.frameNumber = frameCounter++;
            ubo.time = std::chrono::duration<float>(currentTime - startTime).count();
            ubo.deltaTime = 1.0f / 60.0f; // You should track real delta time
            ubo.flags = 0; // Debug flags, etc.

            m_uniformBuffer->update(&ubo, sizeof(ubo));

            glm::vec3 camPos = camera->getPosition().fractional;
            glm::vec3 camForward = camera->getForward();
            glm::mat4 view = camera->getViewMatrix();
            glm::mat4 proj = camera->getProjectionMatrix();

            //Logger::get().info("Camera Debug:");
            //Logger::get().info("  Position: ({:.2f}, {:.2f}, {:.2f})", camPos.x, camPos.y, camPos.z);
            //Logger::get().info("  Forward: ({:.2f}, {:.2f}, {:.2f})", camForward.x, camForward.y, camForward.z);
            //Logger::get().info("  View[3]: ({:.2f}, {:.2f}, {:.2f})", view[3][0], view[3][1], view[3][2]);

            // Test a specific object position
            glm::vec3 objPos(0.0f, 0.0f, 0.0f); // Center object
            glm::vec4 screenPos = proj * view * glm::vec4(objPos, 1.0f);
            //Logger::get().info("  Object at origin projects to: ({:.2f}, {:.2f}, {:.2f}, {:.2f})",
            //    screenPos.x, screenPos.y, screenPos.z, screenPos.w);

            size_t requiredSize = sizeof(RenderableObject) * ubo.numObjects;
            size_t actualSize = m_objectBuffer->getSize();
            //Logger::get().info("Required: {} bytes, Actual: {} bytes", requiredSize, actualSize);
        }
        catch (const std::exception& e) {
            //Logger::get().error("Exception in updateUniformBuffers: {}", e.what());
        }
    }

    void VulkanBackend::setMainMenuVisible(bool visible) {
        if (m_uiRenderer) {
            m_uiRenderer->setElementVisible(m_toggleOverlayButtonId, visible);
            m_uiRenderer->setElementVisible(m_modelEditorButtonId, visible);
            m_uiRenderer->setElementVisible(m_exitButtonId, visible);
        }
    }

}