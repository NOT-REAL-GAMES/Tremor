#pragma once

#include "flecs_interpreter.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace tremor::render {

struct RenderableRecord {
    uint64_t entity = 0;
    std::string assetPath;
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
};

struct RenderMeshPass {
    std::string tagName;
    std::string assetField = "render.mesh";
    std::string positionField = "transform.position";
    std::string scaleField = "transform.scale";
    glm::vec3 scaleMultiplier{1.0f};
    glm::vec3 offset{0.0f};
};

class RenderInteropAdapter {
public:
    using RenderableCallback = std::function<void(const RenderableRecord&)>;

    virtual ~RenderInteropAdapter() = default;

    virtual void forEachRenderableByTag(
        const RenderMeshPass& pass,
        const RenderableCallback& callback
    ) = 0;

    virtual void renderMeshAsset(std::string_view assetPath, const glm::mat4& model) = 0;
};

class RenderInteropRegistry {
public:
    void clear();
    void addMeshPass(RenderMeshPass pass);
    void render(RenderInteropAdapter& adapter) const;

    bool empty() const { return meshPasses_.empty(); }
    const std::vector<RenderMeshPass>& meshPasses() const { return meshPasses_; }

private:
    std::vector<RenderMeshPass> meshPasses_;
};

void registerRenderInteropCommands(
    tremor::script::FlecsInterpreterHost& interpreterHost,
    RenderInteropRegistry& registry
);

} // namespace tremor::render
