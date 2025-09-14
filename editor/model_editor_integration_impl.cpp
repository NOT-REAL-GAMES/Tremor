#include "model_editor_integration.h"
#include "model_editor_ui.h"
#include "../main.h"

namespace tremor::editor {

    // =============================================================================
    // ModelEditorIntegration Implementation
    // =============================================================================

    ModelEditorIntegration::ModelEditorIntegration(tremor::gfx::VulkanBackend& backend)
        : m_backend(backend) {
        Logger::get().info("Creating ModelEditorIntegration");
    }

    ModelEditorIntegration::~ModelEditorIntegration() {
        shutdown();
    }

    bool ModelEditorIntegration::initialize() {
        if (m_initialized) {
            Logger::get().warning("ModelEditorIntegration already initialized");
            return true;
        }

        Logger::get().info("*** STARTING ModelEditorIntegration::initialize() ***");

        // Create command pool for editor operations
        if (!createCommandPool()) {
            Logger::get().error("Failed to create command pool for model editor");
            return false;
        }

        // Create render pass for editor rendering
        if (!createRenderPass()) {
            Logger::get().error("Failed to create render pass for model editor");
            return false;
        }

        // Use the main VulkanBackend's UIRenderer instead of creating our own
        // This ensures UI elements render to the same framebuffer as the main UI
        m_uiRenderer = m_backend.getUIRenderer();

        // Create model editor
        m_modelEditor = std::make_unique<ModelEditor>(
            m_backend.getDevice(),
            m_backend.getPhysicalDevice(),
            m_commandPool,
            m_backend.getGraphicsQueue(),
            *m_uiRenderer
        );

        if (!m_modelEditor->initialize(m_renderPass, VK_FORMAT_B8G8R8A8_SRGB)) {
            Logger::get().error("Failed to initialize model editor");
            return false;
        }

        // Set initial viewport size
        // Set viewport size from actual swapchain extent
        VkExtent2D extent = m_backend.getSwapchainExtent();
        m_modelEditor->setViewportSize(glm::vec2(extent.width, extent.height));
        m_modelEditor->setScissorSize(glm::vec2(extent.width, extent.height));
        m_initialized = true;
        Logger::get().info("ModelEditorIntegration initialized successfully");
        
        // Log controls for first-time users
        logEditorControls();
        
        return true;
    }

    void ModelEditorIntegration::shutdown() {
        if (!m_initialized) return;

        Logger::get().info("Shutting down ModelEditorIntegration");

        m_modelEditor.reset();
        
        if (m_commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_backend.getDevice(), m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }

        if (m_renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_backend.getDevice(), m_renderPass, nullptr);
            m_renderPass = VK_NULL_HANDLE;
        }

        m_initialized = false;
        Logger::get().info("ModelEditorIntegration shutdown complete");
    }

    void ModelEditorIntegration::update(float deltaTime) {
        if (!m_initialized || !m_editorEnabled || !m_modelEditor) return;

        m_modelEditor->update(deltaTime);
    }

    void ModelEditorIntegration::render() {
        if (!m_initialized) {
            //Logger::get().debug("ModelEditorIntegration::render() - not initialized");
            return;
        }
        if (!m_editorEnabled) {
            //Logger::get().debug("ModelEditorIntegration::render() - editor not enabled");
            return;
        }
        if (!m_modelEditor) {
            Logger::get().error("ModelEditorIntegration::render() - modelEditor is null!");
            return;
        }
        Logger::get().debug("*** ModelEditorIntegration::render() - rendering model editor ***");

        // Get current command buffer from VulkanBackend
        VkCommandBuffer commandBuffer = m_backend.getCurrentCommandBuffer();
        
        // Create projection matrix using actual swapchain extent
        VkExtent2D extent = m_backend.getSwapchainExtent();
        float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 1000.0f);
        
        // Update viewport and scissor size to current swapchain extent (in case of window resize)
        m_modelEditor->setViewportSize(glm::vec2(extent.width, extent.height));
        m_modelEditor->setScissorSize(glm::vec2(extent.width, extent.height));
        
        // Render the model editor (grid, viewport, etc.) but UI is handled by main UIRenderer
        m_modelEditor->render(commandBuffer, projection);

    }

    void ModelEditorIntegration::handleInput(const SDL_Event& event) {
        // Always handle F1 to toggle editor
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F1) {
            toggleEditor();
            return;
        }

        // Only handle other input if editor is enabled
        if (!m_initialized || !m_editorEnabled || !m_modelEditor) return;

        m_modelEditor->handleInput(event);
    }

    void ModelEditorIntegration::toggleEditor() {
        if (!m_initialized) {
            Logger::get().warning("Cannot toggle editor - not initialized");
            return;
        }

        setEditorEnabled(!m_editorEnabled);
    }

    void ModelEditorIntegration::setEditorEnabled(bool enabled) {
        if (!m_initialized) {
            Logger::get().warning("Cannot set editor enabled - not initialized");
            return;
        }

        Logger::get().info("*** ModelEditorIntegration::setEditorEnabled({}) called ***", enabled);
        m_editorEnabled = enabled;
        Logger::get().info("*** Model Editor {} ***", m_editorEnabled ? "ENABLED" : "DISABLED");
        
        // Update UI visibility
        if (m_modelEditor) {
            // Access the UI component and show/hide panels
            auto* ui = m_modelEditor->getUI();
            if (ui) {
                Logger::get().info("Setting model editor UI panels visible: {}", m_editorEnabled);
                ui->setToolsPanelVisible(m_editorEnabled);
                ui->setPropertiesPanelVisible(m_editorEnabled);
                ui->setFilePanelVisible(m_editorEnabled);
            } else {
                Logger::get().error("ModelEditor UI is null!");
            }
        } else {
            Logger::get().error("ModelEditor is null!");
        }
        
        // Hide main menu when editor is enabled, show when disabled
        m_backend.setMainMenuVisible(!m_editorEnabled);
        
        if (m_editorEnabled) {
            logEditorControls();
        }
    }

    void ModelEditorIntegration::setGridRenderingEnabled(bool enabled) {
        if (m_modelEditor && m_modelEditor->getViewport()) {
            m_modelEditor->getViewport()->setGridRenderingEnabled(enabled);
        }
    }

    bool ModelEditorIntegration::createCommandPool() {
        // Find graphics queue family
        // TODO: This should come from VulkanBackend, but for now we'll create our own
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = 0; // TODO: Get actual graphics queue family index

        if (vkCreateCommandPool(m_backend.getDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
            Logger::get().error("Failed to create command pool for model editor");
            return false;
        }

        Logger::get().debug("Created command pool for model editor");
        return true;
    }

    bool ModelEditorIntegration::createRenderPass() {
        // Create a simple render pass for editor rendering
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_B8G8R8A8_SRGB;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Load existing content
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = VK_FORMAT_D32_SFLOAT;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Load existing depth
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(m_backend.getDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
            Logger::get().error("Failed to create render pass for model editor");
            return false;
        }

        Logger::get().debug("Created render pass for model editor");
        return true;
    }

    void ModelEditorIntegration::logEditorControls() {
        Logger::get().info("=== Model Editor Controls ===");
        Logger::get().info("F1: Toggle editor on/off");
        Logger::get().info("Esc: Select mode / Clear selection");
        Logger::get().info("G: Move/translate mode");
        Logger::get().info("R: Rotate mode");
        Logger::get().info("S: Scale mode");
        Logger::get().info("Ctrl+N: New model");
        Logger::get().info("Ctrl+O: Open model");
        Logger::get().info("Ctrl+S: Save model");
        Logger::get().info("--- Viewport Navigation ---");
        Logger::get().info("Alt+Left Drag: Orbit camera");
        Logger::get().info("Shift+Middle Drag: Pan camera");
        Logger::get().info("Mouse Wheel: Zoom in/out");
        Logger::get().info("--- Selection ---");
        Logger::get().info("Left Click: Select mesh");
        Logger::get().info("Shift+Left Click: Select vertex");
        Logger::get().info("=============================");
    }

} // namespace tremor::editor