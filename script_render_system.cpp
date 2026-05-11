#include "script_render_system.h"

#include "script_ecs_components.h"
#include "vk.h"

#include <glm/gtc/matrix_transform.hpp>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace tremor::render {
namespace {

std::vector<std::string> splitCommandWords(std::string_view text) {
    std::vector<std::string> words;
    std::istringstream stream{std::string(text)};
    std::string word;
    while (stream >> word) {
        words.push_back(std::move(word));
    }
    return words;
}

std::optional<float> parseFloat(std::string_view text) {
    try {
        size_t consumed = 0;
        const float value = std::stof(std::string(text), &consumed);
        if (consumed == text.size()) {
            return value;
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::optional<glm::vec3> parseVec3Args(const std::vector<std::string>& args, size_t offset) {
    if (args.size() < offset + 3) {
        return std::nullopt;
    }

    const std::optional<float> x = parseFloat(args[offset]);
    const std::optional<float> y = parseFloat(args[offset + 1]);
    const std::optional<float> z = parseFloat(args[offset + 2]);
    if (!x || !y || !z) {
        return std::nullopt;
    }
    return glm::vec3(*x, *y, *z);
}

std::optional<glm::vec3> findScriptCameraOrigin(
    flecs::world& world,
    const ScriptRenderCamera& camera
) {
    const flecs::entity tag = world.lookup(camera.targetTag.c_str());
    if (!tag || !tag.is_alive()) {
        return std::nullopt;
    }

    std::optional<glm::vec3> origin;
    world.each([&](flecs::entity entity, const tremor::ecs::ScriptComponentData& data) {
        if (origin || !entity.has(tag)) {
            return;
        }

        origin = tremor::ecs::readVec3Field(data, camera.targetPositionField);
    });
    return origin;
}

class ScriptRenderAdapter final : public RenderInteropAdapter {
public:
    explicit ScriptRenderAdapter(const ScriptRenderContext& context)
        : context_(context) {
    }

    void forEachRenderableByTag(
        const RenderMeshPass& pass,
        const RenderableCallback& callback
    ) override {
        const flecs::entity tag = context_.world.lookup(pass.tagName.c_str());
        if (!tag || !tag.is_alive()) {
            return;
        }

        context_.world.each([&](flecs::entity entity, const tremor::ecs::ScriptComponentData& data) {
            if (!entity.has(tag)) {
                return;
            }

            const std::optional<std::string_view> asset =
                tremor::ecs::readStringField(data, pass.assetField);
            const std::optional<glm::vec3> position =
                tremor::ecs::readVec3Field(data, pass.positionField);
            if (!asset || !position) {
                return;
            }

            const std::optional<glm::vec3> scale =
                tremor::ecs::readVec3Field(data, pass.scaleField);

            callback({
                .entity = static_cast<uint64_t>(entity.id()),
                .assetPath = std::string(*asset),
                .position = *position - context_.origin,
                .scale = scale.value_or(glm::vec3(1.0f)),
            });
        });
    }

    void renderMeshAsset(std::string_view assetPath, const glm::mat4& model) override {
        context_.overlayManager.renderMeshAsset(
            std::string(assetPath),
            context_.commandBuffer,
            context_.viewProjection * model
        );
    }

private:
    const ScriptRenderContext& context_;
};

} // namespace

void renderScriptEntities(
    const RenderInteropRegistry& registry,
    const ScriptRenderContext& context
) {
    ScriptRenderAdapter adapter(context);
    registry.render(adapter);
}

bool renderScriptFrame(
    const RenderInteropRegistry& registry,
    const ScriptRenderCamera& camera,
    flecs::world& world,
    void* renderBackend
) {
    if (registry.empty()) {
        return false;
    }

    auto* vulkanBackend = static_cast<tremor::gfx::VulkanBackend*>(renderBackend);
    if (vulkanBackend == nullptr || vulkanBackend->getOverlayManager() == nullptr) {
        return false;
    }

    const glm::vec3 origin = findScriptCameraOrigin(world, camera).value_or(glm::vec3(0.0f));
    const VkExtent2D extent = vulkanBackend->getSwapchainExtent();
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const glm::mat4 view = glm::lookAt(
        camera.cameraOffset,
        camera.lookTarget,
        camera.up
    );
    const glm::mat4 projection = glm::perspectiveZO(
        glm::radians(camera.fovDegrees),
        aspectRatio,
        camera.nearPlane,
        camera.farPlane
    );

    renderScriptEntities(registry, {
        .world = world,
        .overlayManager = *vulkanBackend->getOverlayManager(),
        .commandBuffer = vulkanBackend->getCurrentCommandBuffer(),
        .viewProjection = projection * view,
        .origin = origin,
    });

    return true;
}

void registerScriptRenderFrameCommands(
    tremor::script::FlecsInterpreterHost& interpreterHost,
    RenderInteropRegistry& registry,
    ScriptRenderCamera& camera
) {
    registerRenderInteropCommands(interpreterHost, registry);

    interpreterHost.registerCommand("render_camera_follow", [&camera](
        const tremor::script::CommandContext&,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 1 && args.size() != 2) {
            return false;
        }

        camera.targetTag = args[0];
        if (args.size() == 2) {
            camera.targetPositionField = args[1];
        }
        return true;
    });

    interpreterHost.registerCommand("render_camera_offset", [&camera](
        const tremor::script::CommandContext&,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        const std::optional<glm::vec3> offset = parseVec3Args(args, 0);
        if (args.size() != 3 || !offset) {
            return false;
        }

        camera.cameraOffset = *offset;
        return true;
    });

    interpreterHost.registerCommand("render_camera_look_at", [&camera](
        const tremor::script::CommandContext&,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        const std::optional<glm::vec3> target = parseVec3Args(args, 0);
        if (args.size() != 3 || !target) {
            return false;
        }

        camera.lookTarget = *target;
        return true;
    });

    interpreterHost.registerCommand("render_camera_perspective", [&camera](
        const tremor::script::CommandContext&,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 3) {
            return false;
        }

        const std::optional<float> fov = parseFloat(args[0]);
        const std::optional<float> nearPlane = parseFloat(args[1]);
        const std::optional<float> farPlane = parseFloat(args[2]);
        if (!fov || !nearPlane || !farPlane) {
            return false;
        }

        camera.fovDegrees = *fov;
        camera.nearPlane = *nearPlane;
        camera.farPlane = *farPlane;
        return true;
    });
}

} // namespace tremor::render
