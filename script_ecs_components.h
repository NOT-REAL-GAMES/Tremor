#pragma once

#include "flecs_interpreter.h"

#include <optional>
#include <string_view>

#include <glm/glm.hpp>

namespace tremor::ecs {

struct ScriptComponentData {
    tremor::script::ValueMap fields;
};

void registerScriptComponentCommands(tremor::script::FlecsInterpreterHost& interpreterHost);

bool setScriptField(ScriptComponentData& data, std::string_view path, tremor::script::Value value);
const tremor::script::Value* getScriptField(const ScriptComponentData& data, std::string_view path);

std::optional<glm::vec3> readVec3Field(const ScriptComponentData& data, std::string_view path);
std::optional<std::string_view> readStringField(const ScriptComponentData& data, std::string_view path);

} // namespace tremor::ecs
