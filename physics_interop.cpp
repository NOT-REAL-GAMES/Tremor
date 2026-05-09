#include "physics_interop.h"

#include "logger.h"

#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace tremor::physics {
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

std::optional<glm::vec3> parseVec3Args(
    const std::vector<std::string>& args,
    size_t offset
) {
    if (args.size() < offset + 3) {
        return std::nullopt;
    }

    const auto x = parseFloat(args[offset]);
    const auto y = parseFloat(args[offset + 1]);
    const auto z = parseFloat(args[offset + 2]);
    if (!x || !y || !z) {
        return std::nullopt;
    }
    return glm::vec3(*x, *y, *z);
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

std::optional<flecs::entity> resolveScriptEntity(
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
            if (outError) {
                *outError = std::format("blackboard value '{}' is not an entity reference", ref);
            }
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

void removeBodyIfPresent(PhysicsInteropAdapter& adapter, flecs::entity entity) {
    if (const ScriptPhysicsBody* body = entity.get<ScriptPhysicsBody>()) {
        if (body->handle != 0) {
            adapter.removeBody(body->handle);
        }
        entity.remove<ScriptPhysicsBody>();
    }
}

bool attachBody(
    PhysicsInteropAdapter& adapter,
    flecs::entity entity,
    PhysicsBodyHandle handle,
    bool isKinematic
) {
    removeBodyIfPresent(adapter, entity);
    entity.set<ScriptPhysicsBody>({handle, isKinematic});
    return true;
}

const ScriptPhysicsBody* getPhysicsBodyOrLog(flecs::entity entity, std::string_view commandName) {
    const ScriptPhysicsBody* body = entity.get<ScriptPhysicsBody>();
    if (!body || body->handle == 0) {
        Logger::get().error("{} failed: entity has no ScriptPhysicsBody", commandName);
        return nullptr;
    }
    return body;
}

} // namespace

void registerPhysicsInteropCommands(
    tremor::script::FlecsInterpreterHost& interpreterHost,
    PhysicsInteropAdapter& adapter
) {
    interpreterHost.registerCommand("physics_create_dynamic_body", [&interpreterHost, &adapter](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        context.world.component<ScriptPhysicsBody>();
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 6 && args.size() != 7) {
            Logger::get().error(
                "physics_create_dynamic_body expects '<entity_ref> x y z radius height [layer]'"
            );
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveScriptEntity(interpreterHost, context, args[0], &error);
        const std::optional<glm::vec3> position = parseVec3Args(args, 1);
        const std::optional<float> radius = parseFloat(args[4]);
        const std::optional<float> height = parseFloat(args[5]);
        const std::string_view layer = args.size() == 7 ? std::string_view(args[6]) : std::string_view("default");

        if (!entity || !position || !radius || !height) {
            Logger::get().error("physics_create_dynamic_body failed: invalid argument '{}'", argument);
            if (!error.empty()) {
                Logger::get().error("  {}", error);
            }
            return false;
        }

        const std::optional<PhysicsBodyHandle> handle =
            adapter.createDynamicCapsule(*position, *radius, *height, layer);
        if (!handle || *handle == 0) {
            Logger::get().error("physics_create_dynamic_body failed: adapter did not create a body");
            return false;
        }

        attachBody(adapter, *entity, *handle, false);
        return true;
    });

    interpreterHost.registerCommand("physics_create_kinematic_box", [&interpreterHost, &adapter](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        context.world.component<ScriptPhysicsBody>();
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 7 && args.size() != 8) {
            Logger::get().error(
                "physics_create_kinematic_box expects '<entity_ref> x y z half_x half_y half_z [layer]'"
            );
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveScriptEntity(interpreterHost, context, args[0], &error);
        const std::optional<glm::vec3> position = parseVec3Args(args, 1);
        const std::optional<glm::vec3> halfExtents = parseVec3Args(args, 4);
        const std::string_view layer = args.size() == 8 ? std::string_view(args[7]) : std::string_view("default");

        if (!entity || !position || !halfExtents) {
            Logger::get().error("physics_create_kinematic_box failed: invalid argument '{}'", argument);
            if (!error.empty()) {
                Logger::get().error("  {}", error);
            }
            return false;
        }

        const std::optional<PhysicsBodyHandle> handle =
            adapter.createKinematicBox(*position, *halfExtents, layer);
        if (!handle || *handle == 0) {
            Logger::get().error("physics_create_kinematic_box failed: adapter did not create a body");
            return false;
        }

        attachBody(adapter, *entity, *handle, true);
        return true;
    });

    interpreterHost.registerCommand("physics_create_static_box", [&interpreterHost, &adapter](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        context.world.component<ScriptPhysicsBody>();
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 7 && args.size() != 8) {
            Logger::get().error(
                "physics_create_static_box expects '<entity_ref> x y z half_x half_y half_z [layer]'"
            );
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveScriptEntity(interpreterHost, context, args[0], &error);
        const std::optional<glm::vec3> position = parseVec3Args(args, 1);
        const std::optional<glm::vec3> halfExtents = parseVec3Args(args, 4);
        const std::string_view layer = args.size() == 8 ? std::string_view(args[7]) : std::string_view("default");

        if (!entity || !position || !halfExtents) {
            Logger::get().error("physics_create_static_box failed: invalid argument '{}'", argument);
            if (!error.empty()) {
                Logger::get().error("  {}", error);
            }
            return false;
        }

        const std::optional<PhysicsBodyHandle> handle =
            adapter.createStaticBox(*position, *halfExtents, layer);
        if (!handle || *handle == 0) {
            Logger::get().error("physics_create_static_box failed: adapter did not create a body");
            return false;
        }

        attachBody(adapter, *entity, *handle, true);
        return true;
    });

    interpreterHost.registerCommand("physics_remove_body", [&interpreterHost, &adapter](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        std::string error;
        std::optional<flecs::entity> entity = resolveScriptEntity(interpreterHost, context, argument, &error);
        if (!entity) {
            Logger::get().error("physics_remove_body failed: {}", error);
            return false;
        }

        removeBodyIfPresent(adapter, *entity);
        return true;
    });

    interpreterHost.registerCommand("physics_set_position", [&interpreterHost, &adapter](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 4) {
            Logger::get().error("physics_set_position expects '<entity_ref> x y z'");
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveScriptEntity(interpreterHost, context, args[0], &error);
        const std::optional<glm::vec3> position = parseVec3Args(args, 1);
        if (!entity || !position) {
            Logger::get().error("physics_set_position failed: invalid argument '{}'", argument);
            return false;
        }

        const ScriptPhysicsBody* body = getPhysicsBodyOrLog(*entity, "physics_set_position");
        return body && adapter.setPosition(body->handle, *position);
    });

    interpreterHost.registerCommand("physics_set_velocity", [&interpreterHost, &adapter](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 4) {
            Logger::get().error("physics_set_velocity expects '<entity_ref> x y z'");
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveScriptEntity(interpreterHost, context, args[0], &error);
        const std::optional<glm::vec3> velocity = parseVec3Args(args, 1);
        if (!entity || !velocity) {
            Logger::get().error("physics_set_velocity failed: invalid argument '{}'", argument);
            return false;
        }

        const ScriptPhysicsBody* body = getPhysicsBodyOrLog(*entity, "physics_set_velocity");
        return body && adapter.setVelocity(body->handle, *velocity);
    });

    interpreterHost.registerCommand("physics_add_impulse", [&interpreterHost, &adapter](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 4) {
            Logger::get().error("physics_add_impulse expects '<entity_ref> x y z'");
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveScriptEntity(interpreterHost, context, args[0], &error);
        const std::optional<glm::vec3> impulse = parseVec3Args(args, 1);
        if (!entity || !impulse) {
            Logger::get().error("physics_add_impulse failed: invalid argument '{}'", argument);
            return false;
        }

        const ScriptPhysicsBody* body = getPhysicsBodyOrLog(*entity, "physics_add_impulse");
        return body && adapter.addImpulse(body->handle, *impulse);
    });

    interpreterHost.registerCommand("physics_add_force", [&interpreterHost, &adapter](
        const tremor::script::CommandContext& context,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 4) {
            Logger::get().error("physics_add_force expects '<entity_ref> x y z'");
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveScriptEntity(interpreterHost, context, args[0], &error);
        const std::optional<glm::vec3> force = parseVec3Args(args, 1);
        if (!entity || !force) {
            Logger::get().error("physics_add_force failed: invalid argument '{}'", argument);
            return false;
        }

        const ScriptPhysicsBody* body = getPhysicsBodyOrLog(*entity, "physics_add_force");
        return body && adapter.addForce(body->handle, *force);
    });
}

void registerPhysicsLayerConfigCommands(
    tremor::script::FlecsInterpreterHost& interpreterHost,
    PhysicsLayerConfigBuilder& layerConfig
) {
    interpreterHost.registerCommand("physics_layers_clear", [&layerConfig](
        const tremor::script::CommandContext&,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (!args.empty()) {
            Logger::get().error("physics_layers_clear expects no arguments");
            return false;
        }

        layerConfig.clear();
        Logger::get().info("Interpreter cleared physics layer config");
        return true;
    });

    interpreterHost.registerCommand("physics_define_layer", [&layerConfig](
        const tremor::script::CommandContext&,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 1 && args.size() != 2) {
            Logger::get().error("physics_define_layer expects '<name> [broadphase_name]'");
            return false;
        }

        if (!layerConfig.defineLayer(args[0], args.size() == 2 ? std::string_view(args[1]) : std::string_view())) {
            Logger::get().error("physics_define_layer failed for '{}'", argument);
            return false;
        }

        Logger::get().info("Interpreter defined physics layer '{}'", args[0]);
        return true;
    });

    interpreterHost.registerCommand("physics_allow_collision", [&layerConfig](
        const tremor::script::CommandContext&,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 2) {
            Logger::get().error("physics_allow_collision expects '<left_layer> <right_layer>'");
            return false;
        }

        if (!layerConfig.allowCollision(args[0], args[1])) {
            Logger::get().error("physics_allow_collision failed for '{}'", argument);
            return false;
        }

        Logger::get().info("Interpreter allowed physics collision '{} <-> {}'", args[0], args[1]);
        return true;
    });

    interpreterHost.registerCommand("physics_block_collision", [&layerConfig](
        const tremor::script::CommandContext&,
        std::string_view argument
    ) {
        const std::vector<std::string> args = splitCommandWords(argument);
        if (args.size() != 2) {
            Logger::get().error("physics_block_collision expects '<left_layer> <right_layer>'");
            return false;
        }

        if (!layerConfig.blockCollision(args[0], args[1])) {
            Logger::get().error("physics_block_collision failed for '{}'", argument);
            return false;
        }

        Logger::get().info("Interpreter blocked physics collision '{} <-> {}'", args[0], args[1]);
        return true;
    });
}

} // namespace tremor::physics
