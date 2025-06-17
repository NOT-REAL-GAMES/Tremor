#include "ui_renderer.h"
#include "../main.h"  // For Logger
#include <iostream>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <array>

namespace tremor::gfx {

    UIRenderer::UIRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
                         VkCommandPool commandPool, VkQueue graphicsQueue)
        : m_device(device)
        , m_physicalDevice(physicalDevice)
        , m_commandPool(commandPool)
        , m_graphicsQueue(graphicsQueue)
        , m_pipeline(VK_NULL_HANDLE)
        , m_pipelineLayout(VK_NULL_HANDLE)
        , m_descriptorSetLayout(VK_NULL_HANDLE)
        , m_descriptorPool(VK_NULL_HANDLE)
        , m_descriptorSet(VK_NULL_HANDLE)
        , m_vertexBuffer(VK_NULL_HANDLE)
        , m_vertexBufferMemory(VK_NULL_HANDLE)
        , m_vertexBufferSize(0)
        , m_uniformBuffer(VK_NULL_HANDLE)
        , m_uniformBufferMemory(VK_NULL_HANDLE) {
    }

    UIRenderer::~UIRenderer() {
        if (m_vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        }
        if (m_vertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
        }
        if (m_uniformBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_uniformBuffer, nullptr);
        }
        if (m_uniformBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_uniformBufferMemory, nullptr);
        }
        if (m_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
        }
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        }
        if (m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        }
        if (m_descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        }
    }

    bool UIRenderer::initialize(VkRenderPass renderPass, VkFormat colorFormat,
                               VkSampleCountFlagBits sampleCount) {
        Logger::get().info("🖱️ Initializing UI Renderer...");
        m_sampleCount = sampleCount;
        
        // Create descriptor set layout
        VkDescriptorSetLayoutBinding uniformBinding{};
        uniformBinding.binding = 0;
        uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformBinding.descriptorCount = 1;
        uniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uniformBinding;
        
        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
            Logger::get().error("Failed to create descriptor set layout for UI renderer");
            return false;
        }
        
        // Create pipeline
        if (!createPipeline(renderPass, colorFormat)) {
            return false;
        }
        
        // Create descriptor pool with proper capacity for UI rendering
        // We need multiple descriptor sets for multi-frame rendering
        const uint32_t MAX_FRAMES_IN_FLIGHT = 3;  // Match the main renderer
        
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;  // Extra capacity
        
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT * 2;  // Allow for multiple frames in flight
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // Allow freeing
        
        if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
            Logger::get().error("Failed to create descriptor pool for UI renderer");
            return false;
        }
        
        // Create uniform buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(glm::mat4);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_uniformBuffer) != VK_SUCCESS) {
            Logger::get().error("Failed to create uniform buffer");
            return false;
        }
        
        // Allocate memory for uniform buffer
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, m_uniformBuffer, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        
        // Find suitable memory type
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
        
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
        
        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_uniformBufferMemory) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate uniform buffer memory");
            return false;
        }
        
        vkBindBufferMemory(m_device, m_uniformBuffer, m_uniformBufferMemory, 0);
        
        // Create descriptor sets
        createDescriptorSets();
        
        // Create initial vertex buffer
        createVertexBuffer(1024 * sizeof(float) * 8); // Initial size for UI quads
        
        Logger::get().info("✅ UI Renderer initialized");
        return true;
    }

    bool UIRenderer::createPipeline(VkRenderPass renderPass, VkFormat colorFormat) {
        // Load UI shaders
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
        
        VkShaderModule vertShader = loadShader("shaders/ui.vert.spv");
        VkShaderModule fragShader = loadShader("shaders/ui.frag.spv");
        
        if (vertShader == VK_NULL_HANDLE || fragShader == VK_NULL_HANDLE) {
            Logger::get().warning("UI shaders not found, using fallback rendering");
            return true; // Continue without pipeline for now
        }
        
        // Create pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
        
        if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            Logger::get().error("Failed to create pipeline layout");
            vkDestroyShaderModule(m_device, vertShader, nullptr);
            vkDestroyShaderModule(m_device, fragShader, nullptr);
            return false;
        }
        
        // Shader stages
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShader;
        vertShaderStageInfo.pName = "main";
        
        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShader;
        fragShaderStageInfo.pName = "main";
        
        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
        
        // Vertex input
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(UIVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(UIVertex, position);
        
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(UIVertex, texCoord);
        
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32_UINT;
        attributeDescriptions[2].offset = offsetof(UIVertex, color);
        
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
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        
        // Multisampling
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = m_sampleCount;
        
        // Color blending - enable alpha blending
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
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();
        
        // Create the graphics pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            Logger::get().error("Failed to create graphics pipeline");
            vkDestroyShaderModule(m_device, vertShader, nullptr);
            vkDestroyShaderModule(m_device, fragShader, nullptr);
            return false;
        }
        
        vkDestroyShaderModule(m_device, vertShader, nullptr);
        vkDestroyShaderModule(m_device, fragShader, nullptr);
        
        Logger::get().info("✅ UI pipeline created successfully");
        return true;
    }

    bool UIRenderer::createDescriptorSets() {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;
        
        if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate descriptor set");
            return false;
        }
        
        // Update descriptor set with uniform buffer
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(glm::mat4);
        
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

    bool UIRenderer::createVertexBuffer(size_t size) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_vertexBuffer) != VK_SUCCESS) {
            Logger::get().error("Failed to create vertex buffer");
            return false;
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, m_vertexBuffer, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        
        // Find suitable memory type
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
        
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
        
        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_vertexBufferMemory) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate vertex buffer memory");
            return false;
        }
        
        vkBindBufferMemory(m_device, m_vertexBuffer, m_vertexBufferMemory, 0);
        m_vertexBufferSize = size;
        
        return true;
    }

    uint32_t UIRenderer::addButton(const std::string& text, glm::vec2 position, glm::vec2 size,
                                   std::function<void()> onClick) {
        auto button = std::make_unique<UIButton>();
        button->id = m_nextElementId++;
        button->text = text;
        button->position = position;
        button->size = size;
        button->onClick = onClick;
        
        uint32_t buttonId = button->id;  // Store ID before moving
        m_elements.push_back(std::move(button));
        
        // Mark both as dirty since we added a new element
        m_textDirty = true;
        m_quadsDirty = true;
        
        Logger::get().info("Added button '{}' at ({}, {}) with size ({}, {})", 
                          text, position.x, position.y, size.x, size.y);
        
        return buttonId;
    }

    uint32_t UIRenderer::addLabel(const std::string& text, glm::vec2 position) {
        auto label = std::make_unique<UILabel>();
        label->id = m_nextElementId++;
        label->text = text;
        label->position = position;
        label->size = glm::vec2(0, 0); // Labels don't have explicit size
        
        auto lb = label->id;
        m_elements.push_back(std::move(label));
        
        return lb;
    }

    void UIRenderer::removeElement(uint32_t id) {
        auto sizeBefore = m_elements.size();
        m_elements.erase(
            std::remove_if(m_elements.begin(), m_elements.end(),
                          [id](const std::unique_ptr<UIElement>& elem) {
                              return elem->id == id;
                          }),
            m_elements.end()
        );
        
        if (m_elements.size() != sizeBefore) {
            m_textDirty = true;
            m_quadsDirty = true;
        }
    }

    void UIRenderer::clearElements() {
        if (!m_elements.empty()) {
            m_textDirty = true;
            m_quadsDirty = true;
        }
        m_elements.clear();
        m_hoveredElement = nullptr;
        m_pressedElement = nullptr;
    }

    void UIRenderer::updateInput(const SDL_Event& event) {
        switch (event.type) {
            case SDL_MOUSEMOTION:
                updateMousePosition(event.motion.x, event.motion.y);
                break;
                
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_mousePressed = true;
                    
                    // Check if clicking on any element
                    for (auto& elem : m_elements) {
                        if (elem->enabled && elem->visible && elem->contains(m_mousePosition)) {
                            m_pressedElement = elem.get();
                            elem->state = UIElementState::Pressed;
                            break;
                        }
                    }
                }
                break;
                
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_mousePressed = false;
                    
                    // If we were pressing an element and still hovering it, trigger click
                    if (m_pressedElement && m_pressedElement->contains(m_mousePosition)) {
                        if (m_pressedElement->onClick) {
                            m_pressedElement->onClick();
                        }
                        // Set state to hovered since mouse is still over the element
                        m_pressedElement->state = UIElementState::Hovered;
                    }
                    
                    m_pressedElement = nullptr;
                    updateElementStates();
                }
                break;
        }
    }

    void UIRenderer::updateMousePosition(int x, int y) {
        m_mousePosition = glm::vec2(x, y);
        updateElementStates();
    }

    void UIRenderer::updateElementStates() {
        UIElement* newHovered = nullptr;
        
        // Find which element is hovered
        for (auto& elem : m_elements) {
            if (elem->enabled && elem->visible && elem->contains(m_mousePosition)) {
                newHovered = elem.get();
                break;
            }
        }
        
        // Update hover states
        if (newHovered != m_hoveredElement) {
            if (m_hoveredElement && m_hoveredElement != m_pressedElement) {
                auto oldState = m_hoveredElement->state;
                m_hoveredElement->state = UIElementState::Normal;
                if (oldState != UIElementState::Normal) {
                    m_quadsDirty = true; // State changed, need to update quads
                    m_textDirty = true;  // Also need to update text colors
                }
            }
            
            m_hoveredElement = newHovered;
            
            if (m_hoveredElement && m_hoveredElement != m_pressedElement) {
                auto oldState = m_hoveredElement->state;
                m_hoveredElement->state = UIElementState::Hovered;
                if (oldState != UIElementState::Hovered) {
                    m_quadsDirty = true; // State changed, need to update quads
                    m_textDirty = true;  // Also need to update text colors
                }
                if (m_hoveredElement->onHover) {
                    m_hoveredElement->onHover();
                }
            }
        }
        
        // Update pressed element state
        if (m_pressedElement) {
            auto oldState = m_pressedElement->state;
            if (m_pressedElement->contains(m_mousePosition)) {
                m_pressedElement->state = UIElementState::Pressed;
            } else {
                m_pressedElement->state = UIElementState::Normal;
            }
            if (oldState != m_pressedElement->state) {
                m_quadsDirty = true; // State changed, need to update quads
            }
        }
    }

    void UIRenderer::render(VkCommandBuffer commandBuffer, const glm::mat4& projection) {
        if (m_elements.empty()) {
            return;
        }
        
        // First, update and render background quads if needed
        if (m_quadsDirty) {
            updateQuadBuffer();
            m_quadsDirty = false;
        }
        renderQuads(commandBuffer, projection);
        
        // Skip text rendering if no text renderer is set
        if (!m_textRenderer) {
            // Logger::get().info("UI rendering without text (no font loaded)");
            return;
        }
        
        // Track if any text colors changed (which happens with hover)
        bool textColorsChanged = false;
        
        // Only rebuild text if UI has changed
        if (m_textDirty) {
            // Clear text renderer for this frame
            m_textRenderer->clearText();
            m_textDirty = false;
        } else {
            // Skip text processing if nothing changed
            // Don't render here - let the main renderer handle it
            return;
        }
        
        // Process all elements
        for (const auto& elem : m_elements) {
            if (!elem->visible) continue;
            
            uint32_t bgColor = getElementColor(elem.get());
            
            // For now, we'll render background quads using the text renderer's quad capabilities
            // In a full implementation, you'd have a dedicated UI quad renderer
            
            switch (elem->type) {
                case UIElementType::Button: {
                    const UIButton* button = static_cast<const UIButton*>(elem.get());
                    
                    // TODO: Render button background with proper quad rendering
                    
                    std::string displayText = button->text;
                    
                    // Calculate dynamic text scale based on button height
                    // Make text height about 50% of button height
                    float dynamicTextScale = (button->size.y * 0.5f) / 64.0f;  // 32 is base font size estimate
                    
                    // Calculate text position (centered in button)
                    glm::vec2 textSize = m_textRenderer->measureText(button->text, dynamicTextScale);
                    
                    // If measureText returns 0 (no font loaded), estimate size
                    if (textSize.x == 0 || textSize.y == 0) {
                        // Rough estimate: 16 pixels per character at scale 1.0
                        textSize.x = button->text.length() * 16.0f * dynamicTextScale;
                        textSize.y = 32.0f * dynamicTextScale;  // Estimated line height
                    }
                    
                    // Center horizontally, but for vertical we need to account for baseline
                    glm::vec2 textPos;
                    textPos.x = button->position.x + (button->size.x - textSize.x) * 0.5f;
                    
                    // For vertical: position at center, then add half the text height to move baseline to center
                    // Since text is rendered from baseline, we need to push it down
                    textPos.y = button->position.y + (button->size.y * 0.125f);
                    
                    // Determine text color based on element state
                    uint32_t textColor = button->textColor;
                    if (elem->state == UIElementState::Hovered) {
                        // Make text brighter when hovered (add 0x40 to RGB components)
                        uint8_t r = 255;//(textColor >> 24) & 0xFF;
                        uint8_t g = 0;//(textColor >> 16) & 0xFF;
                        uint8_t b = 80;//(textColor >> 8) & 0xFF;
                        uint8_t a = textColor & 0xFF;
                                                
                        textColor = (r << 24) | (g << 16) | (b << 8) | a;
                    } else if (elem->state == UIElementState::Pressed) {
                        // Make text even brighter when pressed
                        uint8_t r = 127;//(textColor >> 24) & 0xFF;
                        uint8_t g = 0;//(textColor >> 16) & 0xFF;
                        uint8_t b = 40;//(textColor >> 8) & 0xFF;
                        uint8_t a = textColor & 0xFF;
                                                
                        textColor = (r << 24) | (g << 16) | (b << 8) | a;
                    }
                    
                    // Add text to renderer
                    TextInstance textInst;
                    textInst.position = textPos;
                    textInst.scale = dynamicTextScale;  // Use dynamic scale instead of fixed
                    textInst.font_spacing = 1.0f;
                    textInst.color = textColor;
                    textInst.text = displayText;
                    textInst.flags = 0;
                    
                    m_textRenderer->addText(textInst);
                    
                    // Debug output removed - was causing log spam
                    break;
                }
                
                case UIElementType::Label: {
                    const UILabel* label = static_cast<const UILabel*>(elem.get());
                    
                    TextInstance textInst;
                    textInst.position = label->position;
                    textInst.scale = label->textScale;
                    textInst.font_spacing = 1.0f;
                    textInst.color = label->textColor;
                    textInst.text = label->text;
                    textInst.flags = 0;
                    
                    m_textRenderer->addText(textInst);
                    break;
                }
                
                default:
                    break;
            }
        }
        
        // Don't render text here - the main renderer will call m_textRenderer->render()
        // This prevents double rendering which was causing GPU crashes
    }

    UIElement* UIRenderer::getElement(uint32_t id) {
        for (auto& elem : m_elements) {
            if (elem->id == id) {
                return elem.get();
            }
        }
        return nullptr;
    }

    void UIRenderer::setElementVisible(uint32_t id, bool visible) {
        if (UIElement* elem = getElement(id)) {
            if (elem->visible != visible) {
                elem->visible = visible;
                m_textDirty = true;
                m_quadsDirty = true;
            }
        }
    }

    void UIRenderer::setElementEnabled(uint32_t id, bool enabled) {
        if (UIElement* elem = getElement(id)) {
            elem->enabled = enabled;
            if (!enabled) {
                elem->state = UIElementState::Disabled;
            }
        }
    }

    void UIRenderer::setElementPosition(uint32_t id, glm::vec2 position) {
        if (UIElement* elem = getElement(id)) {
            elem->position = position;
        }
    }

    uint32_t UIRenderer::getElementColor(const UIElement* element) const {
        switch (element->state) {
            case UIElementState::Hovered:
                return element->hoverColor;
            case UIElementState::Pressed:
                return element->pressedColor;
            case UIElementState::Disabled:
                return 0x808080FF; // Gray for disabled
            default:
                return element->backgroundColor;
        }
    }

    void UIRenderer::updateQuadBuffer() {
        m_quadVertices.clear();
        
        // Generate quads for button backgrounds
        for (const auto& elem : m_elements) {
            if (!elem->visible) continue;
            
            if (elem->type == UIElementType::Button) {
                const UIButton* button = static_cast<const UIButton*>(elem.get());
                
                // Calculate color with alpha based on state
                uint32_t bgColor;
                switch (elem->state) {
                    case UIElementState::Normal:
                        bgColor = 0x00000040; // Black with 25% opacity (64/255)
                        break;
                    case UIElementState::Hovered:
                        bgColor = 0x00000050; // Black with ~31% opacity (80/255)
                        break;
                    case UIElementState::Pressed:
                        bgColor = 0x00000055; // Black with ~33% opacity (85/255)
                        break;
                    case UIElementState::Disabled:
                        bgColor = 0x00000020; // Black with ~12% opacity (32/255)
                        break;
                    default:
                        bgColor = 0x00000040;
                        break;
                }
                
                // Create a quad (2 triangles, 6 vertices)
                float x1 = button->position.x;
                float y1 = button->position.y;
                float x2 = button->position.x + button->size.x;
                float y2 = button->position.y + button->size.y;
                
                // Triangle 1
                m_quadVertices.push_back({{x1, y1}, {0, 0}, bgColor});
                m_quadVertices.push_back({{x2, y1}, {1, 0}, bgColor});
                m_quadVertices.push_back({{x2, y2}, {1, 1}, bgColor});
                
                // Triangle 2
                m_quadVertices.push_back({{x1, y1}, {0, 0}, bgColor});
                m_quadVertices.push_back({{x2, y2}, {1, 1}, bgColor});
                m_quadVertices.push_back({{x1, y2}, {0, 1}, bgColor});
            }
        }
        
        // Upload to GPU if we have vertices
        if (!m_quadVertices.empty() && m_vertexBuffer != VK_NULL_HANDLE) {
            size_t bufferSize = sizeof(UIVertex) * m_quadVertices.size();
            
            if (bufferSize > m_vertexBufferSize) {
                // Need to recreate buffer
                vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
                vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
                createVertexBuffer(bufferSize * 2); // Allocate extra space
            }
            
            // Copy vertex data to buffer
            void* data;
            vkMapMemory(m_device, m_vertexBufferMemory, 0, bufferSize, 0, &data);
            memcpy(data, m_quadVertices.data(), bufferSize);
            vkUnmapMemory(m_device, m_vertexBufferMemory);
        }
    }
    
    void UIRenderer::renderQuads(VkCommandBuffer commandBuffer, const glm::mat4& projection) {
        if (m_quadVertices.empty() || m_pipeline == VK_NULL_HANDLE) {
            return;
        }
        
        // Update uniform buffer with projection matrix
        void* data;
        vkMapMemory(m_device, m_uniformBufferMemory, 0, sizeof(glm::mat4), 0, &data);
        memcpy(data, &projection, sizeof(glm::mat4));
        vkUnmapMemory(m_device, m_uniformBufferMemory);
        
        // Bind pipeline and descriptor sets
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                               m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
        
        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {m_vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        
        // Set viewport and scissor
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = 1280.0f; // TODO: Get actual window size
        viewport.height = 720.0f;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {1280, 720}; // TODO: Get actual window size
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        
        // Draw
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(m_quadVertices.size()), 1, 0, 0);
    }

} // namespace tremor::gfx