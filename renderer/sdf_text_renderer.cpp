#include "sdf_text_renderer.h"
#include "taffy.h"
#include "asset.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <array>

namespace tremor::gfx {

    SDFTextRenderer::SDFTextRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
                                   VkCommandPool commandPool, VkQueue graphicsQueue)
        : device_(device)
        , physicalDevice_(physicalDevice)
        , commandPool_(commandPool)
        , graphicsQueue_(graphicsQueue)
        , pipeline_(VK_NULL_HANDLE)
        , pipelineLayout_(VK_NULL_HANDLE)
        , descriptorSetLayout_(VK_NULL_HANDLE)
        , descriptorPool_(VK_NULL_HANDLE)
        , descriptorSet_(VK_NULL_HANDLE)
        , vertexBuffer_(VK_NULL_HANDLE)
        , vertexBufferMemory_(VK_NULL_HANDLE)
        , vertexBufferSize_(0)
        , uniformBuffer_(VK_NULL_HANDLE)
        , uniformBufferMemory_(VK_NULL_HANDLE) {
    }

    SDFTextRenderer::~SDFTextRenderer() {
        if (vertexBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, vertexBuffer_, nullptr);
        }
        if (vertexBufferMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, vertexBufferMemory_, nullptr);
        }
        if (uniformBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, uniformBuffer_, nullptr);
        }
        if (uniformBufferMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, uniformBufferMemory_, nullptr);
        }
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        }
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        }
        if (descriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        }
        
        // Clean up font resources
        if (currentFont_) {
            if (currentFont_->textureView != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, currentFont_->textureView, nullptr);
            }
            if (currentFont_->texture != VK_NULL_HANDLE) {
                vkDestroyImage(device_, currentFont_->texture, nullptr);
            }
            if (currentFont_->textureMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device_, currentFont_->textureMemory, nullptr);
            }
            if (currentFont_->sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device_, currentFont_->sampler, nullptr);
            }
        }
    }

    bool SDFTextRenderer::initialize(VkRenderPass renderPass, VkFormat colorFormat, 
                                   VkSampleCountFlagBits sampleCount) {
        Logger::get().info("🔤 Initializing SDF Text Renderer with {}x MSAA...", 
                          static_cast<uint32_t>(sampleCount));
        sampleCount_ = sampleCount;
        
        // Create descriptor set layout
        VkDescriptorSetLayoutBinding uniformBinding{};
        uniformBinding.binding = 0;
        uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformBinding.descriptorCount = 1;
        uniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        
        VkDescriptorSetLayoutBinding samplerBinding{};
        samplerBinding.binding = 1;
        samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBinding.descriptorCount = 1;
        samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        
        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uniformBinding, samplerBinding};
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        
        if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
            Logger::get().error("Failed to create descriptor set layout for text renderer");
            return false;
        }
        
        // Create pipeline
        if (!createPipeline(renderPass, colorFormat)) {
            return false;
        }
        
        // Create descriptor pool
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 1;
        
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;
        
        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
            Logger::get().error("Failed to create descriptor pool for text renderer");
            return false;
        }
        
        // Handle dynamic rendering - pass VK_NULL_HANDLE for render pass
        bool useDynamicRendering = (renderPass == VK_NULL_HANDLE);
        if (!useDynamicRendering) {
            // Create pipeline for traditional rendering
            if (!createPipeline(renderPass, colorFormat)) {
                return false;
            }
        }
        
        // Create uniform buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(glm::mat4) + sizeof(glm::vec2) + sizeof(float) * 2;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &uniformBuffer_) != VK_SUCCESS) {
            Logger::get().error("Failed to create uniform buffer");
            return false;
        }
        
        // Allocate memory for uniform buffer
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device_, uniformBuffer_, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        
        // Find suitable memory type
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);
        
        uint32_t memoryType = UINT32_MAX;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((memRequirements.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                memoryType = i;
                break;
            }
        }
        
        if (memoryType == UINT32_MAX) {
            Logger::get().error("Failed to find suitable memory type");
            return false;
        }
        
        allocInfo.memoryTypeIndex = memoryType;
        
        if (vkAllocateMemory(device_, &allocInfo, nullptr, &uniformBufferMemory_) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate uniform buffer memory");
            return false;
        }
        
        vkBindBufferMemory(device_, uniformBuffer_, uniformBufferMemory_, 0);
        
        Logger::get().info("✅ SDF Text Renderer initialized");
        return true;
    }

    bool SDFTextRenderer::loadFont(const std::string& fontPath) {
        Logger::get().info("📖 Loading SDF font: {}", fontPath);
        
        // Load the TAF asset
        Taffy::Asset fontAsset;
        if (!fontAsset.load_from_file_safe(fontPath)) {
            Logger::get().error("Failed to load font asset: {}", fontPath);
            return false;
        }
        
        // Get the FONT chunk
        auto fontData = fontAsset.get_chunk_data(Taffy::ChunkType::FONT);
        if (!fontData) {
            Logger::get().error("No FONT chunk found in asset");
            return false;
        }
        
        // Parse FontChunk header
        if (fontData->size() < sizeof(Taffy::FontChunk)) {
            Logger::get().error("FONT chunk too small");
            return false;
        }
        
        const Taffy::FontChunk* fontChunk = reinterpret_cast<const Taffy::FontChunk*>(fontData->data());
        
        Logger::get().info("Font loaded:");
        Logger::get().info("  Glyphs: {}", fontChunk->glyph_count);
        Logger::get().info("  Texture: {}x{}", fontChunk->texture_width, fontChunk->texture_height);
        Logger::get().info("  SDF Range: {}", fontChunk->sdf_range);
        
        // Create font data
        currentFont_ = std::make_unique<FontData>();
        currentFont_->fontSize = fontChunk->font_size;
        currentFont_->lineHeight = fontChunk->line_height;
        currentFont_->ascent = fontChunk->ascent;
        currentFont_->descent = fontChunk->descent;
        
        // Load glyphs
        const uint8_t* glyphPtr = fontData->data() + fontChunk->glyph_data_offset;
        currentFont_->glyphs.resize(fontChunk->glyph_count);
        std::memcpy(currentFont_->glyphs.data(), glyphPtr, 
                   fontChunk->glyph_count * sizeof(Taffy::FontChunk::Glyph));
        
        // Debug: Log first few glyphs
        Logger::get().info("Loaded {} glyphs:", fontChunk->glyph_count);
        for (size_t i = 0; i < std::min(5u, fontChunk->glyph_count); i++) {
            const auto& g = currentFont_->glyphs[i];
            Logger::get().info("  Glyph {}: codepoint={}, uv=({:.3f}, {:.3f}, {:.3f}, {:.3f}), size={}x{}", 
                              i, g.codepoint, g.uv_x, g.uv_y, g.uv_width, g.uv_height, 
                              static_cast<int>(g.width), static_cast<int>(g.height));
        }
        
        // Create Vulkan texture from texture data
        const uint8_t* texturePtr = fontData->data() + fontChunk->texture_data_offset;
        
        // Create texture
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = fontChunk->texture_width;
        imageInfo.extent.height = fontChunk->texture_height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8_UNORM;  // Single channel for SDF
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        
        if (vkCreateImage(device_, &imageInfo, nullptr, &currentFont_->texture) != VK_SUCCESS) {
            Logger::get().error("Failed to create font texture image");
            return false;
        }
        
        // Allocate memory
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device_, currentFont_->texture, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        
        // Find suitable memory type
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);
        
        uint32_t memoryType = UINT32_MAX;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((memRequirements.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memoryType = i;
                break;
            }
        }
        
        allocInfo.memoryTypeIndex = memoryType;
        
        if (vkAllocateMemory(device_, &allocInfo, nullptr, &currentFont_->textureMemory) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate font texture memory");
            return false;
        }
        
        vkBindImageMemory(device_, currentFont_->texture, currentFont_->textureMemory, 0);
        
        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = currentFont_->texture;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(device_, &viewInfo, nullptr, &currentFont_->textureView) != VK_SUCCESS) {
            Logger::get().error("Failed to create font texture view");
            return false;
        }
        
        // Create sampler
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        
        if (vkCreateSampler(device_, &samplerInfo, nullptr, &currentFont_->sampler) != VK_SUCCESS) {
            Logger::get().error("Failed to create font sampler");
            return false;
        }
        
        // Upload texture data using staging buffer
        VkDeviceSize imageSize = fontChunk->texture_width * fontChunk->texture_height;
        
        // Create staging buffer
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = imageSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
            Logger::get().error("Failed to create staging buffer");
            return false;
        }
        
        // Allocate staging buffer memory
        VkMemoryRequirements stagingMemRequirements;
        vkGetBufferMemoryRequirements(device_, stagingBuffer, &stagingMemRequirements);
        
        VkMemoryAllocateInfo stagingAllocInfo{};
        stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        stagingAllocInfo.allocationSize = stagingMemRequirements.size;
        
        // Find suitable memory type for staging
        // (reuse memProperties from earlier declaration)
        
        uint32_t stagingMemoryType = UINT32_MAX;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((stagingMemRequirements.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                stagingMemoryType = i;
                break;
            }
        }
        
        stagingAllocInfo.memoryTypeIndex = stagingMemoryType;
        
        if (vkAllocateMemory(device_, &stagingAllocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate staging buffer memory");
            vkDestroyBuffer(device_, stagingBuffer, nullptr);
            return false;
        }
        
        vkBindBufferMemory(device_, stagingBuffer, stagingBufferMemory, 0);
        
        // Copy texture data to staging buffer
        void* data;
        vkMapMemory(device_, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, texturePtr, imageSize);
        vkUnmapMemory(device_, stagingBufferMemory);
        
        // Create command buffer for transfer operations
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandPool = commandPool_;
        cmdAllocInfo.commandBufferCount = 1;
        
        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device_, &cmdAllocInfo, &commandBuffer);
        
        // Begin command buffer
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        // Transition image layout from UNDEFINED to TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = currentFont_->texture;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
        
        // Copy buffer to image
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {fontChunk->texture_width, fontChunk->texture_height, 1};
        
        vkCmdCopyBufferToImage(
            commandBuffer,
            stagingBuffer,
            currentFont_->texture,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
        );
        
        // Transition image layout from TRANSFER_DST_OPTIMAL to SHADER_READ_ONLY_OPTIMAL
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
        
        // End command buffer
        vkEndCommandBuffer(commandBuffer);
        
        // Submit command buffer
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        
        vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue_);
        
        // Clean up
        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
        
        Logger::get().info("✅ Texture data uploaded to GPU ({} bytes)", imageSize);
        
        // Create descriptor set now that we have a font
        createDescriptorSets();
        
        Logger::get().info("✅ Font loaded successfully");
        return true;
    }

    bool SDFTextRenderer::createDescriptorSets() {
        if (!currentFont_ || descriptorPool_ == VK_NULL_HANDLE) {
            return false;
        }
        
        // Allocate descriptor set
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout_;
        
        if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet_) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate descriptor set");
            return false;
        }
        
        // Update descriptor set with uniform buffer
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer_;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(glm::mat4) + sizeof(glm::vec2) + sizeof(float) * 2;
        
        VkWriteDescriptorSet descriptorWrites[2]{};
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSet_;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;
        
        // Update descriptor set with texture
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = currentFont_->textureView;
        imageInfo.sampler = currentFont_->sampler;
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSet_;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;
        
        vkUpdateDescriptorSets(device_, 2, descriptorWrites, 0, nullptr);
        
        return true;
    }

    bool SDFTextRenderer::createPipeline(VkRenderPass renderPass, VkFormat colorFormat) {
        Logger::get().info("Creating text rendering pipeline...");
        
        // Load shaders
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
            if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                Logger::get().error("Failed to create shader module");
                return VK_NULL_HANDLE;
            }
            
            return shaderModule;
        };
        
        VkShaderModule vertShaderModule = loadShader("shaders/sdf_text.vert.spv");
        VkShaderModule fragShaderModule = loadShader("shaders/sdf_text.frag.spv");
        
        if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
            Logger::get().error("Failed to load text shaders");
            return false;
        }
        
        Logger::get().info("Text shaders loaded successfully");
        
        // Shader stages
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";
        
        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";
        
        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
        
        // Vertex input
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(float) * 8; // pos(2) + uv(2) + color(4)
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
        
        // Position
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = 0;
        
        // UV
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = sizeof(float) * 2;
        
        // Color
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[2].offset = sizeof(float) * 4;
        
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        
        // Input assembly
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;
        
        // Viewport and scissor will be dynamic
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
        rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for UI
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        
        // Multisampling
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = sampleCount_;
        
        // Depth stencil
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;
        
        // Color blending - enable alpha blending for text
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        
        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        
        // Dynamic state
        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();
        
        // Push constants for text rendering parameters
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(float) * 16; // smoothing, outline, shadow params
        
        // Pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        
        if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
            Logger::get().error("Failed to create pipeline layout");
            vkDestroyShaderModule(device_, vertShaderModule, nullptr);
            vkDestroyShaderModule(device_, fragShaderModule, nullptr);
            return false;
        }
        
        // Create pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout_;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
            Logger::get().error("Failed to create graphics pipeline");
            vkDestroyShaderModule(device_, vertShaderModule, nullptr);
            vkDestroyShaderModule(device_, fragShaderModule, nullptr);
            return false;
        }
        
        // Clean up shader modules
        vkDestroyShaderModule(device_, vertShaderModule, nullptr);
        vkDestroyShaderModule(device_, fragShaderModule, nullptr);
        
        Logger::get().info("✅ Text rendering pipeline created successfully");
        return true;
    }

    void SDFTextRenderer::addText(const TextInstance& text) {
        textInstances_.push_back(text);
    }

    void SDFTextRenderer::clearText() {
        textInstances_.clear();
    }

    bool SDFTextRenderer::createVertexBuffer(size_t size) {
        // Create vertex buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &vertexBuffer_) != VK_SUCCESS) {
            Logger::get().error("Failed to create vertex buffer");
            return false;
        }
        
        // Allocate memory
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device_, vertexBuffer_, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        
        // Find suitable memory type
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);
        
        uint32_t memoryType = UINT32_MAX;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((memRequirements.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                memoryType = i;
                break;
            }
        }
        
        allocInfo.memoryTypeIndex = memoryType;
        
        if (vkAllocateMemory(device_, &allocInfo, nullptr, &vertexBufferMemory_) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate vertex buffer memory");
            return false;
        }
        
        vkBindBufferMemory(device_, vertexBuffer_, vertexBufferMemory_, 0);
        vertexBufferSize_ = size;
        
        return true;
    }

    void SDFTextRenderer::updateVertexBuffer() {
        struct Vertex {
            glm::vec2 pos;
            glm::vec2 uv;
            glm::vec4 color;
        };
        
        std::vector<Vertex> vertices;
        
        // Generate vertices for all text instances
        for (const auto& text : textInstances_) {
            float x = text.position.x;
            float y = text.position.y;
            
            // Unpack color (RGBA format)
            glm::vec4 color;
            color.r = ((text.color >> 24) & 0xFF) / 255.0f;
            color.g = ((text.color >> 16) & 0xFF) / 255.0f;
            color.b = ((text.color >> 8) & 0xFF) / 255.0f;
            color.a = ((text.color >> 0) & 0xFF) / 255.0f;
            
            // Generate vertices for each character
            for (char c : text.text) {
                // Find glyph
                const Taffy::FontChunk::Glyph* glyph = nullptr;
                for (const auto& g : currentFont_->glyphs) {
                    if (g.codepoint == static_cast<uint32_t>(c)) {
                        glyph = &g;
                        break;
                    }
                }
                
                if (!glyph) {
                    Logger::get().warning("Glyph not found for character '{}' (code: {})", c, static_cast<int>(c));
                    continue;
                }
                
                // Debug first character
                static bool firstChar = true;
                if (firstChar) {
                    Logger::get().info("First character '{}' (code: {}) found glyph with uv=({:.3f}, {:.3f}, {:.3f}, {:.3f})",
                                      c, static_cast<int>(c), glyph->uv_x, glyph->uv_y, glyph->uv_width, glyph->uv_height);
                    firstChar = false;
                }
                
                // Calculate scaled dimensions
                float width = glyph->width * text.scale;
                float height = glyph->height * text.scale;
                float bearingX = glyph->bearing_x * std::min(text.scale,1.0f);
                float bearingY = glyph->bearing_y * std::min(text.scale,1.0f);
                
                // Calculate quad position
                float quadX = x + bearingX;
                float quadY = y + (currentFont_->ascent - bearingY) * std::min(text.scale,1.0f);
                
                // Add 6 vertices (2 triangles)
                // Don't flip V coordinates - the font generation already handles coordinate system
                float u0 = glyph->uv_x;
                float v0 = glyph->uv_y;
                float u1 = glyph->uv_x + glyph->uv_width;
                float v1 = glyph->uv_y + glyph->uv_height;
                
                // Top-left
                vertices.push_back({
                    {quadX, quadY},
                    {u0, v0},
                    color
                });
                
                // Top-right
                vertices.push_back({
                    {quadX + width, quadY},
                    {u1, v0},
                    color
                });
                
                // Bottom-left
                vertices.push_back({
                    {quadX, quadY + height},
                    {u0, v1},
                    color
                });
                
                // Second triangle
                // Top-right
                vertices.push_back({
                    {quadX + width, quadY},
                    {u1, v0},
                    color
                });
                
                // Bottom-right
                vertices.push_back({
                    {quadX + width, quadY + height},
                    {u1, v1},
                    color
                });
                
                // Bottom-left
                vertices.push_back({
                    {quadX, quadY + height},
                    {u0, v1},
                    color
                });
                
                // Advance cursor
                x += glyph->advance * text.scale * text.font_spacing;
            }
        }
        
        // Check if we need to recreate the buffer
        size_t requiredSize = vertices.size() * sizeof(Vertex);
        Logger::get().info("Generated {} vertices ({} bytes) for text", vertices.size(), requiredSize);
        
        if (requiredSize > vertexBufferSize_) {
            // Destroy old buffer
            if (vertexBuffer_ != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, vertexBuffer_, nullptr);
            }
            if (vertexBufferMemory_ != VK_NULL_HANDLE) {
                vkFreeMemory(device_, vertexBufferMemory_, nullptr);
            }
            
            // Create new buffer
            createVertexBuffer(requiredSize * 2); // Double size for growth
        }
        
        // Copy vertex data
        if (!vertices.empty()) {
            // Log first few vertices for debugging
            /*if (vertices.size() >= 6) {
                Logger::get().info("First quad vertices:");
                for (int i = 0; i < 6 && i < vertices.size(); i++) {
                    Logger::get().info("  V{}: pos({:.1f}, {:.1f}) uv({:.3f}, {:.3f}) color({:.2f}, {:.2f}, {:.2f}, {:.2f})", 
                                      i, vertices[i].pos.x, vertices[i].pos.y,
                                      vertices[i].uv.x, vertices[i].uv.y,
                                      vertices[i].color.r, vertices[i].color.g, vertices[i].color.b, vertices[i].color.a);
                }
            }*/
            
            void* data;
            vkMapMemory(device_, vertexBufferMemory_, 0, requiredSize, 0, &data);
            memcpy(data, vertices.data(), requiredSize);
            vkUnmapMemory(device_, vertexBufferMemory_);
        }
    }

    void SDFTextRenderer::render(VkCommandBuffer commandBuffer, const glm::mat4& projection) {
        //Logger::get().info("SDFTextRenderer::render called");
        //Logger::get().info("  Text instances: {}", textInstances_.size());
        //Logger::get().info("  Current font: {}", currentFont_ ? "YES" : "NO");
        //Logger::get().info("  Pipeline: {}", pipeline_ != VK_NULL_HANDLE ? "VALID" : "NULL");
        //Logger::get().info("  Descriptor set: {}", descriptorSet_ != VK_NULL_HANDLE ? "VALID" : "NULL");
        
        if (textInstances_.empty() || !currentFont_ || pipeline_ == VK_NULL_HANDLE || descriptorSet_ == VK_NULL_HANDLE) {
            //Logger::get().info("Early return from render - missing resources");
            return;
        }
        
        // Update vertex buffer
        updateVertexBuffer();
        
        // Log first few vertex positions for debugging
        if (vertexBuffer_ != VK_NULL_HANDLE && textInstances_.size() > 0) {
            //Logger::get().info("First text instance position: ({}, {})", 
             //                 textInstances_[0].position.x, textInstances_[0].position.y);
        }
        
        // Update uniform buffer with projection matrix
        void* data;
        vkMapMemory(device_, uniformBufferMemory_, 0, sizeof(glm::mat4), 0, &data);
        memcpy(data, &projection, sizeof(glm::mat4));
        vkUnmapMemory(device_, uniformBufferMemory_);
        
        // Bind pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        
        // Bind descriptor set
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                               pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
        
        // Set viewport
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = 1280.0f;  // TODO: Get from swapchain
        viewport.height = 720.0f;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        
        //Logger::get().info("Viewport set: {}x{}", viewport.width, viewport.height);
        
        // Set scissor
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {1280, 720};  // TODO: Get from swapchain
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        
        // Set push constants for text rendering parameters
        struct PushConstants {
            float smoothing = 0.25f;
            float outlineWidth = 0.0f;
            glm::vec4 outlineColor = glm::vec4(0.0f);
            float shadowOffsetX = 0.0f;
            float shadowOffsetY = 0.0f;
            float shadowSoftness = 0.0f;
            glm::vec4 shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
        } pushConstants;
        
        vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 
                          0, sizeof(pushConstants), &pushConstants);
        
        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {vertexBuffer_};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        
        // Calculate total vertex count
        uint32_t vertexCount = 0;
        for (const auto& text : textInstances_) {
            vertexCount += text.text.length() * 6; // 6 vertices per character
        }
        
        // Draw
        vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
        
        //Logger::get().info("Rendered {} text instances ({} vertices)", textInstances_.size(), vertexCount);
    }

    glm::vec2 SDFTextRenderer::measureText(const std::string& text, float scale) {
        if (!currentFont_) {
            return glm::vec2(0, 0);
        }
        
        float width = 0;
        float height = currentFont_->lineHeight * scale;
        
        // Calculate text width
        for (char c : text) {
            // Find glyph
            for (const auto& glyph : currentFont_->glyphs) {
                if (glyph.codepoint == static_cast<uint32_t>(c)) {
                    width += glyph.advance * scale;
                    break;
                }
            }
        }
        
        return glm::vec2(width, height);
    }

} // namespace tremor::gfx