#include "render_interop.h"

#include "logger.h"

#include <glm/gtc/matrix_transform.hpp>

#include <optional>
#include <sstream>
#include <string>

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

} // namespace

void RenderInteropRegistry::clear() {
    meshPasses_.clear();
}

void RenderInteropRegistry::addMeshPass(RenderMeshPass pass) {
    meshPasses_.push_back(std::move(pass));
}

void RenderInteropRegistry::render(RenderInteropAdapter& adapter) const {
    for (const RenderMeshPass& pass : meshPasses_) {
        adapter.forEachRenderableByTag(pass, [&](const RenderableRecord& record) {
            const glm::vec3 position = record.position + pass.offset;
            const glm::vec3 scale = record.scale * pass.scaleMultiplier;
            const glm::mat4 model = glm::scale(
                glm::translate(glm::mat4(1.0f), position),
                scale
            );
            adapter.renderMeshAsset(record.assetPath, model);
        });
    }
}

void registerRenderInteropCommands(
    tremor::script::FlecsInterpreterHost& interpreterHost,
    RenderInteropRegistry& registry
) {
    interpreterHost.registerCommand("render_passes_clear", [&registry](
        const tremor::script::CommandContext&,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (!args.empty()) {
            Logger::get().error("render_passes_clear expects no arguments");
            return false;
        }

        registry.clear();
        Logger::get().info("Interpreter cleared render passes");
        return true;
    });

    interpreterHost.registerCommand("render_mesh_pass", [&registry](
        const tremor::script::CommandContext&,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.empty() || (args.size() != 1 && args.size() != 2 && args.size() != 3 &&
                args.size() != 4 && args.size() != 7 && args.size() != 10)) {
            Logger::get().error(
                "render_mesh_pass expects '<tag> [asset_field] [position_field] [scale_field] [scale_x scale_y scale_z] [offset_x offset_y offset_z]'"
            );
            return false;
        }

        RenderMeshPass pass;
        pass.tagName = args[0];
        if (args.size() >= 2) {
            pass.assetField = args[1];
        }
        if (args.size() >= 3) {
            pass.positionField = args[2];
        }
        if (args.size() >= 4) {
            pass.scaleField = args[3];
        }

        if (args.size() >= 7) {
            const std::optional<glm::vec3> scale = parseVec3Args(args, 4);
            if (!scale) {
                Logger::get().error("render_mesh_pass failed: invalid scale '{}'", argument);
                return false;
            }
            pass.scaleMultiplier = *scale;
        }

        if (args.size() == 10) {
            const std::optional<glm::vec3> offset = parseVec3Args(args, 7);
            if (!offset) {
                Logger::get().error("render_mesh_pass failed: invalid offset '{}'", argument);
                return false;
            }
            pass.offset = *offset;
        }

        Logger::get().info(
            "Interpreter registered render mesh pass tag='{}' asset_field='{}'",
            pass.tagName,
            pass.assetField
        );
        registry.addMeshPass(std::move(pass));
        return true;
    });
}

} // namespace tremor::render
