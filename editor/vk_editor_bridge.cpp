#include "../Source/Runtime/TremorRenderer/vk_editor_bridge.h"

#include "grid_renderer.h"
#include "model_editor_integration.h"

namespace tremor::gfx {

    namespace {

        class ModelEditorBridge final : public VulkanEditorBridge {
        public:
            explicit ModelEditorBridge(VulkanBackend& backend)
                : integration_(std::make_unique<tremor::editor::ModelEditorIntegration>(backend)) {}

            bool initialize() override {
                return integration_ && integration_->initialize();
            }

            void update(float deltaTime) override {
                if (integration_) {
                    integration_->update(deltaTime);
                }
            }

            void render() override {
                if (integration_) {
                    integration_->render();
                }
            }

            void handleInput(const SDL_Event& event) override {
                if (integration_) {
                    integration_->handleInput(event);
                }
            }

            bool isEditorEnabled() const override {
                return integration_ && integration_->isEditorEnabled();
            }

            void setEditorEnabled(bool enabled) override {
                if (integration_) {
                    integration_->setEditorEnabled(enabled);
                }
            }

            void onUiRendered() override {
                tremor::editor::GridRenderer::setGlobalRenderingBlocked(false);
            }

        private:
            std::unique_ptr<tremor::editor::ModelEditorIntegration> integration_;
        };

    } // namespace

    std::unique_ptr<VulkanEditorBridge> createVulkanEditorBridge(VulkanBackend& backend) {
        return std::make_unique<ModelEditorBridge>(backend);
    }

} // namespace tremor::gfx
