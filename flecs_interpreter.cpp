#include "flecs_interpreter.h"

#include "../Taffy/include/asset.h"
#include "../Taffy/include/taffy_streaming.h"
#include "main.h"
#include "ui_message_commands.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace tremor::script {

namespace {

constexpr uint32_t kDependencyFlagOptional = 1u << 0;
constexpr uint32_t kDependencyFlagRelativeToPackage = 1u << 1;

std::string trimCopy(std::string_view value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string_view skipPrefix(std::string_view value, std::string_view prefix) {
    if (!startsWith(value, prefix)) {
        return value;
    }
    return value.substr(prefix.size());
}

std::string stemOrFallback(const std::string& origin, std::string_view fallback) {
    std::filesystem::path originPath(origin);
    if (originPath.has_stem()) {
        return originPath.stem().string();
    }
    return std::string(fallback);
}

std::optional<float> parseFloat(std::string_view value) {
    std::string buffer = trimCopy(value);
    if (buffer.empty()) {
        return std::nullopt;
    }

    try {
        return std::stof(buffer);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool shouldTreatAsScriptDependency(const Taffy::DependencyChunk::Entry& entry) {
    return entry.usage == Taffy::DependencyChunk::Usage::Code ||
           entry.usage == Taffy::DependencyChunk::Usage::Generic;
}

std::filesystem::path resolveDependencyPath(
    const std::filesystem::path& packagePath,
    const Taffy::DependencyChunk::Entry& entry
) {
    std::filesystem::path dependencyPath(entry.path);
    if ((entry.flags & kDependencyFlagRelativeToPackage) != 0u) {
        return packagePath.parent_path() / dependencyPath;
    }
    return dependencyPath;
}

}  // namespace

FlecsInterpreterHost::FlecsInterpreterHost(flecs::world& world)
    : world_(world) {
    registerCommand("set_blackboard", [this](const CommandContext&, std::string_view argument) {
        std::string text = trimCopy(argument);
        const size_t split = text.find(' ');
        if (split == std::string::npos) {
            recordError(std::format("set_blackboard expects '<key> <value>', got '{}'", text));
            return false;
        }

        std::string key = trimCopy(text.substr(0, split));
        std::string value = trimCopy(text.substr(split + 1));
        if (key.empty()) {
            recordError("set_blackboard requires a non-empty key");
            return false;
        }

        blackboard_[key] = value;
        return true;
    });

    registerCommand("log_blackboard", [this](const CommandContext&, std::string_view argument) {
        std::string key = trimCopy(argument);
        const auto found = blackboard_.find(key);
        if (found == blackboard_.end()) {
            Logger::get().info("Interpreter blackboard '{}' is unset", key);
            return true;
        }

        Logger::get().info("Interpreter blackboard '{}': {}", key, found->second);
        return true;
    });

    registerCommand("emit_ui_message", [](const CommandContext&, std::string_view argument) {
        UiMessageCommandParseResult result;
        if (!enqueueUiMessageCommand(argument, &result)) {
            Logger::get().warning("Interpreter emit_ui_message {}", result.error);
            return false;
        }

        for (const std::string& warning : result.warnings) {
            Logger::get().warning("Interpreter emit_ui_message {}", warning);
        }

        Logger::get().info(
            "Interpreter UI message: '{}' (duration {:.2f}s, color 0x{:08X})",
            result.message.text,
            result.message.durationSeconds,
            result.message.color
        );
        return true;
    });
}

bool FlecsInterpreterHost::loadProgramFromText(std::string_view source, std::string_view origin) {
    ScriptProgram program;
    program.origin = std::string(origin);
    program.name = stemOrFallback(program.origin, "script_program");

    ScriptRule* currentRule = nullptr;
    bool sawProgramDirective = false;

    std::istringstream stream{std::string(source)};
    std::string rawLine;
    size_t lineNumber = 0;
    while (std::getline(stream, rawLine)) {
        ++lineNumber;

        const size_t commentOffset = rawLine.find('#');
        if (commentOffset != std::string::npos) {
            rawLine.erase(commentOffset);
        }

        std::string line = trimCopy(rawLine);
        if (line.empty()) {
            continue;
        }

        std::string_view view(line);
        if (startsWith(view, "program ")) {
            program.name = trimCopy(skipPrefix(view, "program "));
            sawProgramDirective = true;
            continue;
        }

        if (startsWith(view, "rule ")) {
            program.rules.push_back({});
            currentRule = &program.rules.back();
            currentRule->name = trimCopy(skipPrefix(view, "rule "));
            continue;
        }

        if (currentRule == nullptr) {
            recordError(std::format(
                "{}:{}: encountered '{}' before any rule declaration",
                program.origin,
                lineNumber,
                line
            ));
            continue;
        }

        if (view == "on_load") {
            currentRule->trigger = ScriptRule::Trigger::OnLoad;
            continue;
        }

        if (startsWith(view, "on_event ")) {
            currentRule->trigger = ScriptRule::Trigger::OnEvent;
            currentRule->eventName = trimCopy(skipPrefix(view, "on_event "));
            continue;
        }

        if (startsWith(view, "on_tick ")) {
            currentRule->trigger = ScriptRule::Trigger::OnTick;
            std::optional<float> interval = parseFloat(skipPrefix(view, "on_tick "));
            if (!interval || *interval <= 0.0f) {
                recordError(std::format(
                    "{}:{}: invalid tick interval '{}'",
                    program.origin,
                    lineNumber,
                    line
                ));
                continue;
            }
            currentRule->tickIntervalSeconds = *interval;
            continue;
        }

        if (startsWith(view, "cooldown ")) {
            std::optional<float> cooldown = parseFloat(skipPrefix(view, "cooldown "));
            if (!cooldown || *cooldown < 0.0f) {
                recordError(std::format(
                    "{}:{}: invalid cooldown '{}'",
                    program.origin,
                    lineNumber,
                    line
                ));
                continue;
            }
            currentRule->cooldownSeconds = *cooldown;
            continue;
        }

        if (startsWith(view, "action log ")) {
            currentRule->actions.push_back({
                ScriptAction::Type::Log,
                "log",
                trimCopy(skipPrefix(view, "action log "))
            });
            continue;
        }

        if (startsWith(view, "action emit ")) {
            currentRule->actions.push_back({
                ScriptAction::Type::EmitEvent,
                trimCopy(skipPrefix(view, "action emit ")),
                {}
            });
            continue;
        }

        if (startsWith(view, "action command ")) {
            std::string payload = trimCopy(skipPrefix(view, "action command "));
            const size_t split = payload.find(' ');
            std::string commandName = split == std::string::npos ? payload : trimCopy(payload.substr(0, split));
            std::string commandArgument = split == std::string::npos ? std::string() : trimCopy(payload.substr(split + 1));
            currentRule->actions.push_back({
                ScriptAction::Type::Command,
                std::move(commandName),
                std::move(commandArgument)
            });
            continue;
        }

        recordError(std::format(
            "{}:{}: unrecognized directive '{}'",
            program.origin,
            lineNumber,
            line
        ));
    }

    if (program.rules.empty()) {
        if (!sawProgramDirective) {
            recordError(std::format("No rules found in script '{}'", program.origin));
        }
        return false;
    }

    for (ScriptRule& rule : program.rules) {
        if (rule.name.empty()) {
            rule.name = "unnamed_rule";
        }
        if (rule.trigger == ScriptRule::Trigger::OnLoad) {
            rule.pendingOnLoad = true;
        }
    }

    Logger::get().info(
        "Loaded interpreter program '{}' from '{}' with {} rules",
        program.name,
        program.origin,
        program.rules.size()
    );
    programs_.push_back(std::move(program));
    return true;
}

bool FlecsInterpreterHost::loadProgramFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        recordError(std::format("Failed to open interpreter script '{}'", path.string()));
        return false;
    }

    std::string source{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    };
    return loadProgramFromText(source, path.string());
}

bool FlecsInterpreterHost::loadProgramsFromPackage(const std::filesystem::path& packagePath) {
    Taffy::StreamingTaffyLoader loader;
    if (!loader.open(packagePath.string())) {
        recordError(std::format("Failed to open Taffy package '{}'", packagePath.string()));
        return false;
    }

    bool loadedAnything = false;

    std::vector<uint8_t> embeddedScript = loader.loadChunk(Taffy::ChunkType::SCPT);
    if (!embeddedScript.empty()) {
        const std::string scriptText(
            reinterpret_cast<const char*>(embeddedScript.data()),
            embeddedScript.size()
        );
        loadedAnything = loadProgramFromText(scriptText, packagePath.string() + "#SCPT") || loadedAnything;
    }

    for (const Taffy::DependencyChunk::Entry& entry : loader.loadDependencies()) {
        if (!shouldTreatAsScriptDependency(entry)) {
            continue;
        }

        const std::filesystem::path resolvedPath = resolveDependencyPath(packagePath, entry);
        const bool optional = (entry.flags & kDependencyFlagOptional) != 0u;

        if (!std::filesystem::exists(resolvedPath)) {
            if (optional) {
                Logger::get().warning(
                    "Optional interpreter dependency '{}' is missing",
                    resolvedPath.string()
                );
                continue;
            }

            recordError(std::format(
                "Required interpreter dependency '{}' is missing",
                resolvedPath.string()
            ));
            continue;
        }

        switch (entry.reference_type) {
            case Taffy::DependencyChunk::ReferenceType::ExternalTaf:
                loadedAnything = loadProgramsFromPackage(resolvedPath) || loadedAnything;
                break;
            case Taffy::DependencyChunk::ReferenceType::ExternalFile:
                loadedAnything = loadProgramFromFile(resolvedPath) || loadedAnything;
                break;
            default:
                Logger::get().warning(
                    "Skipping unsupported interpreter dependency reference type for '{}'",
                    resolvedPath.string()
                );
                break;
        }
    }

    return loadedAnything;
}

void FlecsInterpreterHost::update(float deltaTime) {
    for (ScriptProgram& program : programs_) {
        for (ScriptRule& rule : program.rules) {
            if (rule.cooldownRemainingSeconds > 0.0f) {
                rule.cooldownRemainingSeconds = std::max(0.0f, rule.cooldownRemainingSeconds - deltaTime);
            }

            if (rule.pendingOnLoad) {
                executeRule(rule, nullptr);
                rule.pendingOnLoad = false;
            }

            if (rule.trigger != ScriptRule::Trigger::OnTick || rule.tickIntervalSeconds <= 0.0f) {
                continue;
            }

            rule.tickAccumulatorSeconds += deltaTime;
            while (rule.tickAccumulatorSeconds >= rule.tickIntervalSeconds) {
                rule.tickAccumulatorSeconds -= rule.tickIntervalSeconds;
                executeRule(rule, nullptr);
            }
        }
    }

    size_t eventIndex = 0;
    while (eventIndex < queuedEvents_.size()) {
        const InterpreterEvent event = queuedEvents_[eventIndex++];
        for (ScriptProgram& program : programs_) {
            for (ScriptRule& rule : program.rules) {
                if (rule.trigger != ScriptRule::Trigger::OnEvent || rule.eventName != event.name) {
                    continue;
                }

                executeRule(rule, &event);
            }
        }
    }

    queuedEvents_.clear();
}

void FlecsInterpreterHost::emitEvent(std::string name) {
    queuedEvents_.push_back({std::move(name), {}});
}

void FlecsInterpreterHost::emitEvent(InterpreterEvent event) {
    queuedEvents_.push_back(std::move(event));
}

void FlecsInterpreterHost::registerCommand(std::string name, CommandCallback callback) {
    commands_[std::move(name)] = std::move(callback);
}

bool FlecsInterpreterHost::hasErrors() const {
    return !errors_.empty();
}

bool FlecsInterpreterHost::hasPrograms() const {
    return !programs_.empty();
}

size_t FlecsInterpreterHost::getProgramCount() const {
    return programs_.size();
}

size_t FlecsInterpreterHost::getQueuedEventCount() const {
    return queuedEvents_.size();
}

const std::vector<ScriptProgram>& FlecsInterpreterHost::getPrograms() const {
    return programs_;
}

const std::vector<std::string>& FlecsInterpreterHost::getErrors() const {
    return errors_;
}

bool FlecsInterpreterHost::executeRule(ScriptRule& rule, const InterpreterEvent* event) {
    if (rule.cooldownRemainingSeconds > 0.0f) {
        return false;
    }

    bool executedAnyAction = false;
    for (const ScriptAction& action : rule.actions) {
        executedAnyAction = executeAction(action, event) || executedAnyAction;
    }

    if (executedAnyAction && rule.cooldownSeconds > 0.0f) {
        rule.cooldownRemainingSeconds = rule.cooldownSeconds;
    }

    return executedAnyAction;
}

bool FlecsInterpreterHost::executeAction(const ScriptAction& action, const InterpreterEvent* event) {
    switch (action.type) {
        case ScriptAction::Type::Log:
            Logger::get().info("Interpreter: {}", action.argument);
            return true;

        case ScriptAction::Type::EmitEvent:
            emitEvent(action.verb);
            return true;

        case ScriptAction::Type::Command: {
            const auto found = commands_.find(action.verb);
            if (found == commands_.end()) {
                recordError(std::format("Unknown interpreter command '{}'", action.verb));
                return false;
            }

            CommandContext context{
                .world = world_,
                .event = event,
                .blackboard = blackboard_,
            };
            return found->second(context, action.argument);
        }
    }

    return false;
}

void FlecsInterpreterHost::recordError(std::string message) {
    Logger::get().error("Interpreter: {}", message);
    errors_.push_back(std::move(message));
}

}  // namespace tremor::script
