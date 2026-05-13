#include "vk_ui_bridge.h"

#include "vk.h"
#include "vk_editor_bridge.h"
#include "sdf_text_renderer.h"
#include "ui_message_center.h"
#include "ui_renderer.h"
#include "logger.h"
#include "tremor_profiler.h"

#include <algorithm>
#include <chrono>
#include <format>

namespace tremor::gfx {

    namespace {
        constexpr size_t kProfilerVisibleLines = 7;
        constexpr float kProfilerLineHeight = 26.0f;
        constexpr float kProfilerScale = 0.42f;
    }

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
            initializeProfilerOverlay(backend);
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

    void VulkanUiBridge::initializeProfilerOverlay(VulkanBackend& backend) {
        if (!backend.m_uiRenderer || !backend.m_profilerLabelIds.empty()) {
            return;
        }

        for (size_t i = 0; i < kProfilerVisibleLines; ++i) {
            const uint32_t labelId = backend.m_uiRenderer->addLabel(
                "",
                glm::vec2(24.0f, 24.0f + static_cast<float>(i) * kProfilerLineHeight),
                i == 0 ? 0x80E0FFFF : 0xD0D0FFFF,
                kProfilerScale
            );
            backend.m_profilerLabelIds.push_back(labelId);
        }

        setProfilerOverlayVisible(backend, backend.m_profilerOverlayVisible);
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

    void VulkanUiBridge::updateProfilerOverlay(VulkanBackend& backend) {
        if (!backend.m_uiRenderer || backend.m_profilerLabelIds.empty()) {
            return;
        }

        if (!backend.m_profilerOverlayVisible) {
            return;
        }

        const auto snapshot = tremor::trace::Profiler::instance().snapshot();
        const double fps = snapshot.frameMs > 0.0 ? 1000.0 / snapshot.frameMs : 0.0;
        std::vector<std::string> lines;
        lines.reserve(kProfilerVisibleLines);
        lines.push_back(std::format(
            "CPU {:.2f} ms  {:.0f} FPS  avg {:.2f}  max {:.2f}",
            snapshot.frameMs,
            fps,
            snapshot.avgFrameMs,
            snapshot.maxFrameMs
        ));

        for (const auto& record : snapshot.topRecords) {
            lines.push_back(std::format(
                "{:<18} {:>5.2f} ms  avg {:>5.2f}",
                record.name.substr(0, 18),
                record.lastMs,
                record.avgMs
            ));
            if (lines.size() >= kProfilerVisibleLines) {
                break;
            }
        }

        if (backend.vkSwapchain) {
            const float width = static_cast<float>(backend.vkSwapchain->extent().width);
            const float baseX = std::max(24.0f, width - 420.0f);
            for (size_t i = 0; i < backend.m_profilerLabelIds.size(); ++i) {
                backend.m_uiRenderer->setElementPosition(
                    backend.m_profilerLabelIds[i],
                    glm::vec2(baseX, 72.0f + static_cast<float>(i) * kProfilerLineHeight)
                );
            }
        }

        for (size_t i = 0; i < backend.m_profilerLabelIds.size(); ++i) {
            const uint32_t labelId = backend.m_profilerLabelIds[i];
            if (i < lines.size()) {
                backend.m_uiRenderer->setElementText(labelId, lines[i]);
                backend.m_uiRenderer->setElementVisible(labelId, true);
            } else {
                backend.m_uiRenderer->setElementText(labelId, "");
                backend.m_uiRenderer->setElementVisible(labelId, false);
            }
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

    void VulkanUiBridge::setProfilerOverlayVisible(VulkanBackend& backend, bool visible) {
        backend.m_profilerOverlayVisible = visible;
        if (!backend.m_uiRenderer) {
            return;
        }

        for (uint32_t labelId : backend.m_profilerLabelIds) {
            backend.m_uiRenderer->setElementVisible(labelId, visible);
        }
    }

} // namespace tremor::gfx
