#include "vk_ui_bridge.h"

#include "vk.h"
#include "vk_editor_bridge.h"
#include "sdf_text_renderer.h"
#include "ui_message_center.h"
#include "ui_renderer.h"
#include "logger.h"

#include <algorithm>
#include <chrono>

namespace tremor::gfx {

    void VulkanUiBridge::initializeRuntimeUi(VulkanBackend& backend) {
        if (!backend.m_uiRenderer) {
            return;
        }

        backend.m_modelEditorButtonId = backend.m_uiRenderer->addButton(
            "Model Editor", glm::vec2(20, 200), glm::vec2(160, 40),
            [&backend]() {
                Logger::get().info("🔧 Model Editor button clicked!");
                if (backend.m_editorIntegration) {
                    const bool isEnabled = backend.m_editorIntegration->isEditorEnabled();
                    backend.m_editorIntegration->setEditorEnabled(!isEnabled);
                    Logger::get().info("Model Editor {}", !isEnabled ? "enabled" : "disabled");
                }
            });

        backend.m_exitButtonId = backend.m_uiRenderer->addButton(
            "Exit", glm::vec2(20, 250), glm::vec2(160, 40),
            []() {
                Logger::get().info("❌ Exit button clicked!");
                SDL_Event quit_event;
                quit_event.type = SDL_QUIT;
                SDL_PushEvent(&quit_event);
            });

        if (backend.m_textRenderer) {
            if (!backend.m_textRenderer->loadFont("assets/fonts/test_font.taf")) {
                Logger::get().warning("Failed to load test font - UI will render without text");
            }

            backend.m_uiRenderer->addLabel("NOT REAL GAMES", glm::vec2(24, 36), 0xFF0050FF);
            backend.m_meshShaderStatusLabelId = backend.m_uiRenderer->addLabel(
                "Mesh Shaders: Off",
                glm::vec2(1020.0f, 36.0f),
                0xFF4040FF,
                0.7f
            );
            initializeMessageOverlay(backend);
        }
    }

    void VulkanUiBridge::initializeMessageOverlay(VulkanBackend& backend) {
        if (!backend.m_uiRenderer || !backend.m_uiMessageLabelIds.empty()) {
            return;
        }

        for (size_t i = 0; i < tremor::UiMessageCenter::MaxVisibleMessages; ++i) {
            const float verticalOffset = static_cast<float>(tremor::UiMessageCenter::MaxVisibleMessages - 1 - i) * 34.0f;
            uint32_t labelId = backend.m_uiRenderer->addLabel(
                "",
                glm::vec2(24.0f, 680.0f - verticalOffset),
                0xFFD060FF,
                0.55f
            );
            backend.m_uiRenderer->setElementVisible(labelId, false);
            backend.m_uiMessageLabelIds.push_back(labelId);
        }
    }

    void VulkanUiBridge::updateMessageOverlay(VulkanBackend& backend) {
        if (!backend.m_uiRenderer || backend.m_uiMessageLabelIds.empty()) {
            return;
        }

        static auto lastUpdateTime = std::chrono::high_resolution_clock::now();
        const auto currentTime = std::chrono::high_resolution_clock::now();
        const float deltaTimeSeconds = std::chrono::duration<float>(currentTime - lastUpdateTime).count();
        lastUpdateTime = currentTime;

        auto& messageCenter = tremor::UiMessageCenter::instance();
        messageCenter.update(deltaTimeSeconds);

        if (!messageCenter.consumeDirty()) {
            return;
        }

        const std::vector<tremor::UiMessage> visibleMessages = messageCenter.snapshotVisibleMessages();
        const size_t hiddenLabelCount = backend.m_uiMessageLabelIds.size() - visibleMessages.size();

        for (size_t labelIndex = 0; labelIndex < backend.m_uiMessageLabelIds.size(); ++labelIndex) {
            const uint32_t labelId = backend.m_uiMessageLabelIds[labelIndex];
            if (labelIndex < hiddenLabelCount) {
                backend.m_uiRenderer->setElementText(labelId, "");
                backend.m_uiRenderer->setElementVisible(labelId, false);
                continue;
            }

            const tremor::UiMessage& message = visibleMessages[labelIndex - hiddenLabelCount];
            backend.m_uiRenderer->setElementText(labelId, message.text);
            backend.m_uiRenderer->setElementTextColor(labelId, message.color);
            backend.m_uiRenderer->setElementVisible(labelId, true);
        }
    }

    void VulkanUiBridge::updateMeshShaderStatusLabel(VulkanBackend& backend, bool meshShadersActive) {
        if (!backend.m_uiRenderer || backend.m_meshShaderStatusLabelId == 0) {
            return;
        }

        backend.m_uiRenderer->setElementText(
            backend.m_meshShaderStatusLabelId,
            meshShadersActive ? "Mesh Shaders: On" : "Mesh Shaders: Off"
        );
        backend.m_uiRenderer->setElementTextColor(
            backend.m_meshShaderStatusLabelId,
            meshShadersActive ? 0x30FF60FF : 0xFF4040FF
        );

        if (backend.vkSwapchain) {
            const float width = static_cast<float>(backend.vkSwapchain->extent().width);
            const float labelX = std::max(20.0f, width - 260.0f);
            backend.m_uiRenderer->setElementPosition(backend.m_meshShaderStatusLabelId, glm::vec2(labelX, 36.0f));
        }
    }

} // namespace tremor::gfx
