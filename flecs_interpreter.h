#pragma once

#include <flecs.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace tremor::script {

enum class ValueType : uint8_t {
    Null,
    Number,
    Bool,
    String,
    Object,
    Lambda,
};

struct ObjectValue;
struct LambdaValue;

struct Value {
    using ObjectPtr = std::shared_ptr<ObjectValue>;
    using LambdaPtr = std::shared_ptr<LambdaValue>;
    using Storage = std::variant<std::monostate, double, bool, std::string, ObjectPtr, LambdaPtr>;

    Storage storage{};

    Value() = default;
    Value(std::nullptr_t) : storage(std::monostate{}) {}
    Value(double number) : storage(number) {}
    Value(bool boolean) : storage(boolean) {}
    Value(std::string text) : storage(std::move(text)) {}
    Value(const char* text) : storage(std::string(text)) {}
    Value(ObjectPtr object) : storage(std::move(object)) {}
    Value(LambdaPtr lambda) : storage(std::move(lambda)) {}

    ValueType type() const;
    bool isNull() const;
    std::optional<double> asNumber() const;
    std::optional<bool> asBool() const;
    std::optional<std::string_view> asStringView() const;
    const ObjectValue* asObject() const;
    ObjectValue* asObject();
    const LambdaValue* asLambda() const;
    LambdaValue* asLambda();
    std::string toString() const;
    std::string debugString() const;

    static Value makeObject();
    static Value makeLambda(std::string debugName = {});
};

Value parseLiteralValue(std::string_view text);

struct ObjectValue {
    std::unordered_map<std::string, Value> fields;
};

struct LambdaValue {
    std::string debugName;
    std::vector<std::string> parameters;
    std::string bodySource;
};

struct InterpreterEvent {
    std::string name;
    std::unordered_map<std::string, std::string> fields;
};

struct ScriptAction {
    enum class Type : uint8_t {
        Log,
        EmitEvent,
        Command,
        SetBlackboard,
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
    std::string conditionExpression;
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
    std::unordered_map<std::string, Value>& blackboard;
};

using ValueMap = std::unordered_map<std::string, Value>;

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
    bool hasBoundHostCallback(std::string_view name) const;
    bool bindHostCallback(std::string name, std::string_view blackboardPath);
    std::optional<Value> invokeHostCallback(
        std::string_view name,
        const ValueMap& arguments = {},
        std::string* outError = nullptr
    );
    bool setBlackboardValue(std::string_view path, Value value, std::string* outError = nullptr);
    std::optional<Value> getBlackboardValue(std::string_view path) const;

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
    ValueMap blackboard_;
    std::unordered_map<std::string, std::string> hostCallbackBindings_;
    std::unordered_map<std::string, CommandCallback> commands_;
};

}  // namespace tremor::script
