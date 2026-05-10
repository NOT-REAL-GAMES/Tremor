#pragma once

#include "main.h"
#include "render_interop.h"

#include <flecs.h>

#include <glm/glm.hpp>

#include <string>

namespace tremor::gfx {
class TaffyOverlayManager;
}

namespace tremor::render {

struct ScriptRenderCamera {
    std::string targetTag = "Player";
    std::string targetPositionField = "transform.position";
    glm::vec3 cameraOffset{0.0f, -10.0f, 12.0f};
    glm::vec3 lookTarget{0.0f, 2.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovDegrees = 45.0f;
    float nearPlane = 100000.0f;
    float farPlane = 0.1f;
};

struct ScriptRenderContext {
    flecs::world& world;
    tremor::gfx::TaffyOverlayManager& overlayManager;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    glm::mat4 viewProjection{1.0f};
    glm::vec3 origin{0.0f};
};

void renderScriptEntities(
    const RenderInteropRegistry& registry,
    const ScriptRenderContext& context
);

bool renderScriptFrame(
    const RenderInteropRegistry& registry,
    const ScriptRenderCamera& camera,
    flecs::world& world,
    void* renderBackend
);

void registerScriptRenderFrameCommands(
    tremor::script::FlecsInterpreterHost& interpreterHost,
    RenderInteropRegistry& registry,
    ScriptRenderCamera& camera
);

} // namespace tremor::render
