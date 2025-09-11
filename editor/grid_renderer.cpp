#include "grid_renderer.h"
#include "../main.h"
#include "../vk.h"
#include <array>
#include <cstring>

namespace tremor::editor {

    // =============================================================================
    // GridRenderer Implementation
    // =============================================================================

    GridRenderer::GridRenderer(VkDevice device, VkPhysicalDevice physicalDevice,
                              VkCommandPool commandPool, VkQueue graphicsQueue)
        : m_device(device), m_physicalDevice(physicalDevice),
          m_commandPool(commandPool), m_graphicsQueue(graphicsQueue),
          m_pipeline(VK_NULL_HANDLE), m_pipelineLayout(VK_NULL_HANDLE),
          m_descriptorSetLayout(VK_NULL_HANDLE), m_descriptorPool(VK_NULL_HANDLE),
          m_descriptorSet(VK_NULL_HANDLE), m_vertexBuffer(VK_NULL_HANDLE),
          m_vertexBufferMemory(VK_NULL_HANDLE), m_uniformBuffer(VK_NULL_HANDLE),
          m_uniformBufferMemory(VK_NULL_HANDLE), m_vertexShader(VK_NULL_HANDLE),
          m_fragmentShader(VK_NULL_HANDLE), m_vertexCount(0) {
    }

    GridRenderer::~GridRenderer() {
        if (m_device != VK_NULL_HANDLE) {
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
            if (m_descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
            }
            if (m_descriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
            }
            if (m_pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(m_device, m_pipeline, nullptr);
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

    bool GridRenderer::initialize(VkRenderPass renderPass, VkFormat colorFormat,
                                 VkSampleCountFlagBits sampleCount) {
        m_sampleCount = sampleCount;

        Logger::get().info("Initializing GridRenderer");

        if (!createShaders()) {
            Logger::get().error("Failed to create grid shaders");
            return false;
        }

        if (!createDescriptorSets()) {
            Logger::get().error("Failed to create grid descriptor sets");
            return false;
        }

        if (!createPipeline(renderPass, colorFormat)) {
            Logger::get().error("Failed to create grid pipeline");
            return false;
        }

        if (!createVertexBuffer()) {
            Logger::get().error("Failed to create grid vertex buffer");
            return false;
        }

        if (!createUniformBuffer()) {
            Logger::get().error("Failed to create grid uniform buffer");
            return false;
        }

        Logger::get().info("GridRenderer initialized successfully");
        return true;
    }

    void GridRenderer::render(VkCommandBuffer commandBuffer, const glm::mat4& viewMatrix, 
                             const glm::mat4& projMatrix, VkExtent2D viewportExtent, VkExtent2D scissorExtent) {
        if (m_vertexCount == 0) return;
        
        // Check if pipeline is valid before rendering
        if (m_pipeline == VK_NULL_HANDLE) {
            // Pipeline not created yet (shader compilation not implemented)
            return;
        }

        // Update uniform buffer with MVP matrix
        glm::mat4 mvpMatrix = projMatrix * viewMatrix;
        updateUniformBuffer(mvpMatrix);

        // Bind pipeline
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        // Set viewport - this is required when using VK_DYNAMIC_STATE_VIEWPORT
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(viewportExtent.width);
        viewport.height = static_cast<float>(viewportExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        // Set scissor - this is required when using VK_DYNAMIC_STATE_SCISSOR
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = scissorExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // Bind descriptor sets
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {m_vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Draw grid lines
        vkCmdDraw(commandBuffer, m_vertexCount, 1, 0, 0);
    }

    bool GridRenderer::createShaders() {
        // Simple vertex shader for grid lines
        const std::string vertexShaderSource = R"(
            #version 450

            layout(binding = 0) uniform UniformBufferObject {
                mat4 mvp;
            } ubo;

            layout(location = 0) in vec3 inPosition;
            layout(location = 1) in vec3 inColor;

            layout(location = 0) out vec3 fragColor;

            void main() {
                gl_Position = ubo.mvp * vec4(inPosition, 1.0);
                fragColor = inColor;
            }
        )";

        // Simple fragment shader for grid lines
        const std::string fragmentShaderSource = R"(
            #version 450

            layout(location = 0) in vec3 fragColor;
            layout(location = 0) out vec4 outColor;

            void main() {
                outColor = vec4(fragColor, 1.0);
            }
        )";

        // Use ShaderCompiler to compile GLSL to SPIR-V
        tremor::gfx::ShaderCompiler compiler;
        
        // Compile vertex shader
        auto vertexSpirv = compiler.compileToSpv(
            vertexShaderSource,
            tremor::gfx::ShaderType::Vertex,
            "grid_vertex.glsl",
            0);
        
        if (vertexSpirv.empty()) {
            Logger::get().error("Failed to compile grid vertex shader");
            return false;
        }

        // Compile fragment shader
        auto fragmentSpirv = compiler.compileToSpv(
            fragmentShaderSource,
            tremor::gfx::ShaderType::Fragment,
            "grid_fragment.glsl",
            0);
        
        if (fragmentSpirv.empty()) {
            Logger::get().error("Failed to compile grid fragment shader");
            return false;
        }

        // Create vertex shader module
        VkShaderModuleCreateInfo vertexCreateInfo{};
        vertexCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertexCreateInfo.codeSize = vertexSpirv.size() * sizeof(uint32_t);
        vertexCreateInfo.pCode = vertexSpirv.data();

        if (vkCreateShaderModule(m_device, &vertexCreateInfo, nullptr, &m_vertexShader) != VK_SUCCESS) {
            Logger::get().error("Failed to create grid vertex shader module");
            return false;
        }

        // Create fragment shader module
        VkShaderModuleCreateInfo fragmentCreateInfo{};
        fragmentCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fragmentCreateInfo.codeSize = fragmentSpirv.size() * sizeof(uint32_t);
        fragmentCreateInfo.pCode = fragmentSpirv.data();

        if (vkCreateShaderModule(m_device, &fragmentCreateInfo, nullptr, &m_fragmentShader) != VK_SUCCESS) {
            Logger::get().error("Failed to create grid fragment shader module");
            vkDestroyShaderModule(m_device, m_vertexShader, nullptr);
            m_vertexShader = VK_NULL_HANDLE;
            return false;
        }

        Logger::get().info("Grid shaders compiled successfully");
        return true;
    }

    bool GridRenderer::createPipeline(VkRenderPass renderPass, VkFormat colorFormat) {
        // Vertex input description
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(GridVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

        // Position attribute
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(GridVertex, position);

        // Color attribute
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(GridVertex, color);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        // Input assembly - lines
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport and scissor
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Rasterization
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

        // Depth stencil
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE; // Don't write to depth buffer
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        // Color blending
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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

        // Dynamic state
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // Create pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;

        if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            Logger::get().error("Failed to create grid pipeline layout");
            return false;
        }

        // Check if shaders were created successfully
        if (m_vertexShader == VK_NULL_HANDLE || m_fragmentShader == VK_NULL_HANDLE) {
            Logger::get().error("Shaders not created - cannot create pipeline");
            return false;
        }

        // Shader stages with compiled SPIR-V modules
        VkPipelineShaderStageCreateInfo shaderStages[2] = {};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = m_vertexShader;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = m_fragmentShader;
        shaderStages[1].pName = "main";

        // Create graphics pipeline
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
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        // Create the graphics pipeline
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            Logger::get().error("Failed to create grid graphics pipeline");
            return false;
        }

        Logger::get().info("Grid pipeline created successfully");
        return true;
    }

    bool GridRenderer::createDescriptorSets() {
        // Descriptor set layout
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
            Logger::get().error("Failed to create grid descriptor set layout");
            return false;
        }

        // Descriptor pool
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
            Logger::get().error("Failed to create grid descriptor pool");
            return false;
        }

        // Allocate descriptor set
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;

        if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
            Logger::get().error("Failed to allocate grid descriptor set");
            return false;
        }

        return true;
    }

    bool GridRenderer::createVertexBuffer() {
        // Generate grid vertices
        std::vector<GridVertex> vertices;
        generateGridVertices(vertices);
        
        if (vertices.empty()) {
            Logger::get().warning("No grid vertices generated");
            return true;
        }

        m_vertexCount = static_cast<uint32_t>(vertices.size());
        VkDeviceSize bufferSize = sizeof(GridVertex) * vertices.size();

        // Create vertex buffer
        if (!createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         m_vertexBuffer, m_vertexBufferMemory)) {
            Logger::get().error("Failed to create grid vertex buffer");
            return false;
        }

        // Copy vertex data
        void* data;
        vkMapMemory(m_device, m_vertexBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), bufferSize);
        vkUnmapMemory(m_device, m_vertexBufferMemory);

        Logger::get().info("Created grid vertex buffer with {} vertices", m_vertexCount);
        return true;
    }

    bool GridRenderer::createUniformBuffer() {
        VkDeviceSize bufferSize = sizeof(glm::mat4);

        if (!createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         m_uniformBuffer, m_uniformBufferMemory)) {
            Logger::get().error("Failed to create grid uniform buffer");
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

    void GridRenderer::generateGridVertices(std::vector<GridVertex>& vertices) {
        vertices.clear();

        // Generate grid lines in X and Z directions
        int numLines = static_cast<int>(2 * m_gridSize / m_gridSpacing) + 1;

        for (int i = 0; i < numLines; ++i) {
            float offset = -m_gridSize + i * m_gridSpacing;
            bool isMajorLine = (i % m_majorLineInterval) == 0;
            glm::vec3 color = isMajorLine ? m_majorGridColor : m_gridColor;

            // Lines parallel to X axis (varying Z)
            vertices.push_back({{-m_gridSize, 0.0f, offset}, color});
            vertices.push_back({{m_gridSize, 0.0f, offset}, color});

            // Lines parallel to Z axis (varying X)
            vertices.push_back({{offset, 0.0f, -m_gridSize}, color});
            vertices.push_back({{offset, 0.0f, m_gridSize}, color});
        }

        Logger::get().info("Generated {} grid vertices", vertices.size());
    }

    void GridRenderer::updateUniformBuffer(const glm::mat4& mvpMatrix) {
        void* data;
        vkMapMemory(m_device, m_uniformBufferMemory, 0, sizeof(glm::mat4), 0, &data);
        memcpy(data, &mvpMatrix, sizeof(glm::mat4));
        vkUnmapMemory(m_device, m_uniformBufferMemory);
    }

    bool GridRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
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

    uint32_t GridRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && 
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        // Fallback
        return 0;
    }

} // namespace tremor::editor