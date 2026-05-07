#pragma once

#include <flecs.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tremor::script {

struct InterpreterEvent {
    std::string name;
    std::unordered_map<std::string, std::string> fields;
};

struct ScriptAction {
    enum class Type : uint8_t {
        Log,
        EmitEvent,
        Command,
    };

    Type type = Type::Log;
    std::string verb;
    std::string argument;
};

struct ScriptRule {
    enum class Trigger : uint8_t {
        OnLoad,
        OnEvent,
        OnTick,
    };

    std::string name;
    Trigger trigger = Trigger::OnLoad;
    std::string eventName;
    float tickIntervalSeconds = 0.0f;
    float cooldownSeconds = 0.0f;
    float tickAccumulatorSeconds = 0.0f;
    float cooldownRemainingSeconds = 0.0f;
    bool pendingOnLoad = false;
    std::vector<ScriptAction> actions;
};

struct ScriptProgram {
    std::string name;
    std::string origin;
    std::vector<ScriptRule> rules;
};

struct CommandContext {
    flecs::world& world;
    const InterpreterEvent* event = nullptr;
    std::unordered_map<std::string, std::string>& blackboard;
};

class FlecsInterpreterHost {
public:
    using CommandCallback = std::function<bool(const CommandContext&, std::string_view argument)>;

    explicit FlecsInterpreterHost(flecs::world& world);

    bool loadProgramFromText(std::string_view source, std::string_view origin);
    bool loadProgramFromFile(const std::filesystem::path& path);
    bool loadProgramsFromPackage(const std::filesystem::path& packagePath);

    void update(float deltaTime);
    void emitEvent(std::string name);
    void emitEvent(InterpreterEvent event);

    void registerCommand(std::string name, CommandCallback callback);

    bool hasErrors() const;
    bool hasPrograms() const;
    size_t getProgramCount() const;
    size_t getQueuedEventCount() const;

    const std::vector<ScriptProgram>& getPrograms() const;
    const std::vector<std::string>& getErrors() const;

private:
    bool executeRule(ScriptRule& rule, const InterpreterEvent* event);
    bool executeAction(const ScriptAction& action, const InterpreterEvent* event);
    void recordError(std::string message);

    flecs::world& world_;
    std::vector<ScriptProgram> programs_;
    std::vector<std::string> errors_;
    std::vector<InterpreterEvent> queuedEvents_;
    std::unordered_map<std::string, std::string> blackboard_;
    std::unordered_map<std::string, CommandCallback> commands_;
};

}  // namespace tremor::script
