#include "vk_backend_controls.h"

#include "vk.h"
#include "vk_editor_bridge.h"
#include "ui_renderer.h"
#include "sequencer_ui.h"
#include "logger.h"

#include <utility>

namespace tremor::gfx {

    void VulkanBackendControls::handleInput(VulkanBackend& backend, const SDL_Event& event) {
        if (event.type == SDL_WINDOWEVENT &&
            (event.window.event == SDL_WINDOWEVENT_RESIZED ||
             event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
            backend.m_framebufferResized = true;
        }

        if (backend.m_uiRenderer) {
            backend.m_uiRenderer->updateInput(event);
        }

        if (backend.m_editorIntegration) {
            backend.m_editorIntegration->handleInput(event);
        }
    }

    void VulkanBackendControls::setMainMenuVisible(VulkanBackend& backend, bool visible) {
        if (!backend.m_uiRenderer) {
            return;
        }

        backend.m_uiRenderer->setElementVisible(backend.m_toggleOverlayButtonId, visible);
        backend.m_uiRenderer->setElementVisible(backend.m_modelEditorButtonId, visible);
        backend.m_uiRenderer->setElementVisible(backend.m_exitButtonId, visible);
    }

    void VulkanBackendControls::setSequencerCallback(VulkanBackend& backend, SequencerCallback callback) {
        backend.m_sequencerCallback = std::move(callback);

        if (backend.m_sequencerUI) {
            backend.m_sequencerUI->onStepTriggered([&backend](int step) {
                Logger::get().info("🎵 Sequencer step {} triggered!", step);
                if (backend.m_sequencerCallback) {
                    backend.m_sequencerCallback(step);
                }
            });
        }
    }

} // namespace tremor::gfx
