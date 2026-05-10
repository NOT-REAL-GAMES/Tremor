#include "script_ecs_components.h"

#include "logger.h"

#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace tremor::ecs {
namespace {

std::vector<std::string> splitWords(std::string_view text) {
    std::vector<std::string> words;
    std::istringstream stream{std::string(text)};
    std::string word;
    while (stream >> word) {
        words.push_back(std::move(word));
    }
    return words;
}

std::vector<std::string_view> splitPath(std::string_view path) {
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        const size_t split = path.find('.');
        const std::string_view part = split == std::string_view::npos
            ? path
            : path.substr(0, split);
        if (part.empty()) {
            return {};
        }
        parts.push_back(part);
        if (split == std::string_view::npos) {
            break;
        }
        path.remove_prefix(split + 1);
    }
    return parts;
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

std::optional<flecs::entity_t> parseEntityId(std::string value) {
    if (value.empty()) {
        return std::nullopt;
    }
    if (value.front() == '#') {
        value.erase(0, 1);
    }

    try {
        size_t consumed = 0;
        const uint64_t parsed = std::stoull(value, &consumed);
        if (consumed == value.size()) {
            return static_cast<flecs::entity_t>(parsed);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::optional<flecs::entity> resolveEntity(
    tremor::script::FlecsInterpreterHost& interpreterHost,
    const tremor::script::CommandContext& context,
    std::string_view reference,
    std::string* outError
) {
    const std::string ref(reference);
    if (ref.empty()) {
        if (outError) *outError = "entity reference is empty";
        return std::nullopt;
    }

    if (ref.starts_with("var.")) {
        const std::optional<tremor::script::Value> value =
            interpreterHost.getBlackboardValue(ref.substr(4));
        if (!value) {
            if (outError) *outError = std::format("blackboard entity reference '{}' is unset", ref);
            return std::nullopt;
        }

        std::optional<flecs::entity_t> entityId = value->asEntityId();
        if (!entityId) {
            if (const auto text = value->asStringView()) {
                entityId = parseEntityId(std::string(*text));
                if (!entityId) {
                    flecs::entity named = context.world.lookup(std::string(*text).c_str());
                    if (named && named.is_alive()) {
                        return named;
                    }
                }
            } else if (const auto number = value->asNumber()) {
                entityId = static_cast<flecs::entity_t>(*number);
            }
        }

        if (!entityId) {
            if (outError) *outError = std::format("blackboard value '{}' is not an entity reference", ref);
            return std::nullopt;
        }

        flecs::entity entity(context.world, *entityId);
        if (!entity.is_alive()) {
            if (outError) *outError = std::format("entity '{}' is not alive", ref);
            return std::nullopt;
        }
        return entity;
    }

    if (const std::optional<flecs::entity_t> entityId = parseEntityId(ref)) {
        flecs::entity entity(context.world, *entityId);
        if (entity.is_alive()) {
            return entity;
        }
    }

    flecs::entity entity = context.world.lookup(ref.c_str());
    if (!entity || !entity.is_alive()) {
        if (outError) *outError = std::format("entity '{}' was not found", ref);
        return std::nullopt;
    }
    return entity;
}

tremor::script::Value makeVec3Value(float x, float y, float z) {
    tremor::script::Value value = tremor::script::Value::makeObject();
    tremor::script::ObjectValue* object = value.asObject();
    object->fields.emplace("x", tremor::script::Value(static_cast<double>(x)));
    object->fields.emplace("y", tremor::script::Value(static_cast<double>(y)));
    object->fields.emplace("z", tremor::script::Value(static_cast<double>(z)));
    return value;
}

bool setEntityField(
    tremor::script::FlecsInterpreterHost& interpreterHost,
    const tremor::script::CommandContext& context,
    std::string_view entityRef,
    std::string_view path,
    tremor::script::Value value
) {
    std::string error;
    std::optional<flecs::entity> entity = resolveEntity(interpreterHost, context, entityRef, &error);
    if (!entity) {
        Logger::get().error("ecs_set failed: {}", error);
        return false;
    }

    context.world.component<ScriptComponentData>();

    ScriptComponentData data;
    if (const ScriptComponentData* existing = entity->get<ScriptComponentData>()) {
        data = *existing;
    }

    if (!setScriptField(data, path, std::move(value))) {
        Logger::get().error("ecs_set failed: invalid field path '{}'", path);
        return false;
    }

    entity->set<ScriptComponentData>(std::move(data));
    return true;
}

} // namespace

bool setScriptField(ScriptComponentData& data, std::string_view path, tremor::script::Value value) {
    const std::vector<std::string_view> parts = splitPath(path);
    if (parts.empty()) {
        return false;
    }

    tremor::script::ValueMap* fields = &data.fields;
    for (size_t index = 0; index + 1 < parts.size(); ++index) {
        tremor::script::Value& child = (*fields)[std::string(parts[index])];
        if (child.asObject() == nullptr) {
            child = tremor::script::Value::makeObject();
        }
        fields = &child.asObject()->fields;
    }

    (*fields)[std::string(parts.back())] = std::move(value);
    return true;
}

const tremor::script::Value* getScriptField(const ScriptComponentData& data, std::string_view path) {
    const std::vector<std::string_view> parts = splitPath(path);
    if (parts.empty()) {
        return nullptr;
    }

    const tremor::script::ValueMap* fields = &data.fields;
    for (size_t index = 0; index < parts.size(); ++index) {
        const auto found = fields->find(std::string(parts[index]));
        if (found == fields->end()) {
            return nullptr;
        }
        if (index + 1 == parts.size()) {
            return &found->second;
        }

        const tremor::script::ObjectValue* object = found->second.asObject();
        if (object == nullptr) {
            return nullptr;
        }
        fields = &object->fields;
    }

    return nullptr;
}

std::optional<glm::vec3> readVec3Field(const ScriptComponentData& data, std::string_view path) {
    const tremor::script::Value* value = getScriptField(data, path);
    if (value == nullptr) {
        return std::nullopt;
    }

    const tremor::script::ObjectValue* object = value->asObject();
    if (object == nullptr) {
        return std::nullopt;
    }

    const auto readAxis = [object](std::string_view axis) -> std::optional<float> {
        const auto found = object->fields.find(std::string(axis));
        if (found == object->fields.end()) {
            return std::nullopt;
        }
        const std::optional<double> number = found->second.asNumber();
        if (!number) {
            return std::nullopt;
        }
        return static_cast<float>(*number);
    };

    const std::optional<float> x = readAxis("x");
    const std::optional<float> y = readAxis("y");
    const std::optional<float> z = readAxis("z");
    if (!x || !y || !z) {
        return std::nullopt;
    }
    return glm::vec3(*x, *y, *z);
}

std::optional<std::string_view> readStringField(const ScriptComponentData& data, std::string_view path) {
    const tremor::script::Value* value = getScriptField(data, path);
    if (value == nullptr) {
        return std::nullopt;
    }
    return value->asStringView();
}

void registerScriptComponentCommands(tremor::script::FlecsInterpreterHost& interpreterHost) {
    interpreterHost.registerCommand("ecs_set_number", [&interpreterHost](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitWords(argument);
        if (args.size() != 3) {
            Logger::get().error("ecs_set_number expects '<entity_ref> <field_path> <number>'");
            return false;
        }

        const std::optional<float> value = parseFloat(args[2]);
        if (!value) {
            Logger::get().error("ecs_set_number failed: invalid number '{}'", args[2]);
            return false;
        }

        return setEntityField(
            interpreterHost,
            context,
            args[0],
            args[1],
            tremor::script::Value(static_cast<double>(*value))
        );
    });

    interpreterHost.registerCommand("ecs_set_string", [&interpreterHost](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitWords(argument);
        if (args.size() < 3) {
            Logger::get().error("ecs_set_string expects '<entity_ref> <field_path> <text>'");
            return false;
        }

        const size_t valueOffset = argument.find(args[2]);
        if (valueOffset == std::string_view::npos) {
            Logger::get().error("ecs_set_string failed: invalid argument '{}'", argument);
            return false;
        }

        return setEntityField(
            interpreterHost,
            context,
            args[0],
            args[1],
            tremor::script::Value(std::string(argument.substr(valueOffset)))
        );
    });

    interpreterHost.registerCommand("ecs_set_bool", [&interpreterHost](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitWords(argument);
        if (args.size() != 3 || (args[2] != "true" && args[2] != "false")) {
            Logger::get().error("ecs_set_bool expects '<entity_ref> <field_path> <true|false>'");
            return false;
        }

        return setEntityField(
            interpreterHost,
            context,
            args[0],
            args[1],
            tremor::script::Value(args[2] == "true")
        );
    });

    interpreterHost.registerCommand("ecs_set_vec3", [&interpreterHost](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitWords(argument);
        if (args.size() != 5) {
            Logger::get().error("ecs_set_vec3 expects '<entity_ref> <field_path> <x> <y> <z>'");
            return false;
        }

        const std::optional<float> x = parseFloat(args[2]);
        const std::optional<float> y = parseFloat(args[3]);
        const std::optional<float> z = parseFloat(args[4]);
        if (!x || !y || !z) {
            Logger::get().error("ecs_set_vec3 failed: invalid vector '{}'", argument);
            return false;
        }

        return setEntityField(
            interpreterHost,
            context,
            args[0],
            args[1],
            makeVec3Value(*x, *y, *z)
        );
    });
}

} // namespace tremor::ecs
