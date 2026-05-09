#include "flecs_interpreter.h"

#include "../Taffy/include/asset.h"
#include "../Taffy/include/taffy_streaming.h"
#include "logger.h"
#include "ui_message_commands.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
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

bool iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }

    return true;
}

bool shouldTreatAsScriptDependency(const Taffy::DependencyChunk::Entry& entry) {
    return entry.usage == Taffy::DependencyChunk::Usage::Code ||
           entry.usage == Taffy::DependencyChunk::Usage::Generic;
}

std::vector<std::string> splitWords(std::string_view text) {
    std::vector<std::string> words;
    std::istringstream stream{std::string(text)};
    std::string word;
    while (stream >> word) {
        words.push_back(std::move(word));
    }
    return words;
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

std::vector<std::string> splitPath(std::string_view path) {
    std::vector<std::string> segments;
    size_t start = 0;
    while (start < path.size()) {
        const size_t separator = path.find('.', start);
        const std::string_view segmentView = separator == std::string_view::npos
            ? path.substr(start)
            : path.substr(start, separator - start);
        const std::string segment = trimCopy(segmentView);
        if (!segment.empty()) {
            segments.push_back(segment);
        }

        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }

    return segments;
}

std::string stripVarPrefix(std::string_view path) {
    std::string result = trimCopy(path);
    if (startsWith(result, "var.")) {
        result.erase(0, 4);
    }
    return result;
}

const Value* resolveBlackboardPath(
    const std::unordered_map<std::string, Value>& blackboard,
    std::string_view path
) {
    const std::vector<std::string> segments = splitPath(path);
    if (segments.empty()) {
        return nullptr;
    }

    auto found = blackboard.find(segments.front());
    if (found == blackboard.end()) {
        return nullptr;
    }

    const Value* current = &found->second;
    for (size_t i = 1; i < segments.size(); ++i) {
        const ObjectValue* object = current->asObject();
        if (object == nullptr) {
            return nullptr;
        }

        auto field = object->fields.find(segments[i]);
        if (field == object->fields.end()) {
            return nullptr;
        }

        current = &field->second;
    }

    return current;
}

bool assignBlackboardPath(
    std::unordered_map<std::string, Value>& blackboard,
    std::string_view path,
    Value value,
    std::string* outError
) {
    const std::vector<std::string> segments = splitPath(path);
    if (segments.empty()) {
        if (outError != nullptr) {
            *outError = "assignment target path is empty";
        }
        return false;
    }

    if (segments.size() == 1) {
        blackboard[segments.front()] = std::move(value);
        return true;
    }

    Value& root = blackboard[segments.front()];
    if (root.isNull()) {
        root = Value::makeObject();
    }

    Value* current = &root;
    for (size_t i = 1; i + 1 < segments.size(); ++i) {
        ObjectValue* object = current->asObject();
        if (object == nullptr) {
            if (outError != nullptr) {
                *outError = std::format(
                    "path segment '{}' is not an object",
                    segments[i - 1]
                );
            }
            return false;
        }

        Value& child = object->fields[segments[i]];
        if (child.isNull()) {
            child = Value::makeObject();
        } else if (child.asObject() == nullptr) {
            if (outError != nullptr) {
                *outError = std::format(
                    "path segment '{}' is not an object",
                    segments[i]
                );
            }
            return false;
        }

        current = &child;
    }

    ObjectValue* object = current->asObject();
    if (object == nullptr) {
        if (outError != nullptr) {
            *outError = std::format(
                "path segment '{}' is not an object",
                segments[segments.size() - 2]
            );
        }
        return false;
    }

    object->fields[segments.back()] = std::move(value);
    return true;
}

std::optional<flecs::entity_t> parseEntityId(std::string_view text) {
    std::string value = trimCopy(text);
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

std::optional<flecs::entity> resolveEntityRef(
    flecs::world& world,
    const ValueMap& blackboard,
    std::string_view reference,
    std::string* outError
) {
    const std::string text = trimCopy(reference);
    if (text.empty()) {
        if (outError != nullptr) {
            *outError = "entity reference is empty";
        }
        return std::nullopt;
    }

    if (startsWith(text, "var.")) {
        const Value* value = resolveBlackboardPath(blackboard, text.substr(4));
        if (value == nullptr) {
            if (outError != nullptr) {
                *outError = std::format("blackboard entity reference '{}' is unset", text);
            }
            return std::nullopt;
        }

        const std::optional<flecs::entity_t> entityId = value->asEntityId();
        if (!entityId) {
            if (outError != nullptr) {
                *outError = std::format(
                    "blackboard value '{}' is not an entity, got {}",
                    text,
                    value->debugString()
                );
            }
            return std::nullopt;
        }

        flecs::entity entity(world, *entityId);
        if (!entity.is_alive()) {
            if (outError != nullptr) {
                *outError = std::format("entity '{}' is not alive", text);
            }
            return std::nullopt;
        }
        return entity;
    }

    if (const std::optional<flecs::entity_t> entityId = parseEntityId(text)) {
        flecs::entity entity(world, *entityId);
        if (!entity.is_alive()) {
            if (outError != nullptr) {
                *outError = std::format("entity id '{}' is not alive", text);
            }
            return std::nullopt;
        }
        return entity;
    }

    flecs::entity entity = world.lookup(text.c_str());
    if (!entity || !entity.is_alive()) {
        if (outError != nullptr) {
            *outError = std::format("entity '{}' was not found", text);
        }
        return std::nullopt;
    }
    return entity;
}

flecs::entity getOrCreateTag(flecs::world& world, std::string_view name) {
    const std::string tagName = trimCopy(name);
    return world.entity(tagName.c_str());
}

std::optional<Value> evaluateExpression(
    std::string_view expression,
    const InterpreterEvent* event,
    const ValueMap& blackboard,
    const ValueMap* locals,
    std::string* outError
);

bool valueEquals(const Value& left, const Value& right) {
    if (left.type() != right.type()) {
        return false;
    }

    switch (left.type()) {
        case ValueType::Null:
            return true;
        case ValueType::Number: {
            const auto lhs = left.asNumber();
            const auto rhs = right.asNumber();
            return lhs && rhs && *lhs == *rhs;
        }
        case ValueType::Bool: {
            const auto lhs = left.asBool();
            const auto rhs = right.asBool();
            return lhs && rhs && *lhs == *rhs;
        }
        case ValueType::String: {
            const auto lhs = left.asStringView();
            const auto rhs = right.asStringView();
            return lhs && rhs && *lhs == *rhs;
        }
        case ValueType::Object:
        case ValueType::Lambda:
            return left.storage == right.storage;
        case ValueType::Entity: {
            const auto lhs = left.asEntityId();
            const auto rhs = right.asEntityId();
            return lhs && rhs && *lhs == *rhs;
        }
    }

    return false;
}

bool coerceValueToBool(const Value& value) {
    switch (value.type()) {
        case ValueType::Null:
            return false;
        case ValueType::Number: {
            const auto number = value.asNumber();
            return number.has_value() && *number != 0.0;
        }
        case ValueType::Bool: {
            const auto boolean = value.asBool();
            return boolean.has_value() && *boolean;
        }
        case ValueType::String: {
            const auto text = value.asStringView();
            return text.has_value() && !text->empty();
        }
        case ValueType::Object:
        case ValueType::Lambda:
        case ValueType::Entity:
            return true;
    }

    return false;
}

class ExpressionParser {
public:
    ExpressionParser(
        std::string_view source,
        const InterpreterEvent* event,
        const ValueMap& blackboard,
        const ValueMap* locals = nullptr
    )
        : source_(source), event_(event), blackboard_(blackboard), locals_(locals) {
    }

    std::optional<Value> parse(std::string* outError) {
        std::optional<Value> value = parseOr();
        skipWhitespace();
        if (!value) {
            if (outError != nullptr) {
                *outError = error_;
            }
            return std::nullopt;
        }

        if (!atEnd()) {
            setError(std::format("unexpected trailing input near '{}'", remainingSource()));
            if (outError != nullptr) {
                *outError = error_;
            }
            return std::nullopt;
        }

        return value;
    }

private:
    std::optional<Value> parseOr() {
        std::optional<Value> left = parseAnd();
        while (left && consumeKeyword("or")) {
            std::optional<Value> right = parseAnd();
            if (!right) {
                return std::nullopt;
            }
            left = Value(coerceValueToBool(*left) || coerceValueToBool(*right));
        }
        return left;
    }

    std::optional<Value> parseAnd() {
        std::optional<Value> left = parseComparison();
        while (left && consumeKeyword("and")) {
            std::optional<Value> right = parseComparison();
            if (!right) {
                return std::nullopt;
            }
            left = Value(coerceValueToBool(*left) && coerceValueToBool(*right));
        }
        return left;
    }

    std::optional<Value> parseComparison() {
        std::optional<Value> left = parseAdditive();
        while (left) {
            std::string op;
            if (consumeOperator("==")) op = "==";
            else if (consumeOperator("!=")) op = "!=";
            else if (consumeOperator(">=")) op = ">=";
            else if (consumeOperator("<=")) op = "<=";
            else if (consumeOperator(">")) op = ">";
            else if (consumeOperator("<")) op = "<";
            else break;

            std::optional<Value> right = parseAdditive();
            if (!right) {
                return std::nullopt;
            }

            if (op == "==" || op == "!=") {
                const bool equal = valueEquals(*left, *right);
                left = Value(op == "==" ? equal : !equal);
                continue;
            }

            const auto lhs = left->asNumber();
            const auto rhs = right->asNumber();
            if (!lhs || !rhs) {
                setError(std::format("operator '{}' requires numeric operands", op));
                return std::nullopt;
            }

            if (op == ">") left = Value(*lhs > *rhs);
            else if (op == ">=") left = Value(*lhs >= *rhs);
            else if (op == "<") left = Value(*lhs < *rhs);
            else left = Value(*lhs <= *rhs);
        }

        return left;
    }

    std::optional<Value> parseAdditive() {
        std::optional<Value> left = parseMultiplicative();
        while (left) {
            std::string op;
            if (consumeOperator("+")) op = "+";
            else if (consumeOperator("-")) op = "-";
            else break;

            std::optional<Value> right = parseMultiplicative();
            if (!right) {
                return std::nullopt;
            }

            const auto lhs = left->asNumber();
            const auto rhs = right->asNumber();
            if (!lhs || !rhs) {
                setError(std::format("operator '{}' requires numeric operands", op));
                return std::nullopt;
            }

            left = Value(op == "+" ? *lhs + *rhs : *lhs - *rhs);
        }

        return left;
    }

    std::optional<Value> parseMultiplicative() {
        std::optional<Value> left = parseUnary();
        while (left) {
            std::string op;
            if (consumeOperator("*")) op = "*";
            else if (consumeOperator("/")) op = "/";
            else break;

            std::optional<Value> right = parseUnary();
            if (!right) {
                return std::nullopt;
            }

            const auto lhs = left->asNumber();
            const auto rhs = right->asNumber();
            if (!lhs || !rhs) {
                setError(std::format("operator '{}' requires numeric operands", op));
                return std::nullopt;
            }

            if (op == "/" && *rhs == 0.0) {
                setError("division by zero");
                return std::nullopt;
            }

            left = Value(op == "*" ? *lhs * *rhs : *lhs / *rhs);
        }

        return left;
    }

    std::optional<Value> parseUnary() {
        if (consumeKeyword("not")) {
            std::optional<Value> operand = parseUnary();
            if (!operand) {
                return std::nullopt;
            }
            return Value(!coerceValueToBool(*operand));
        }

        if (consumeOperator("-")) {
            std::optional<Value> operand = parseUnary();
            if (!operand) {
                return std::nullopt;
            }

            const auto number = operand->asNumber();
            if (!number) {
                setError("unary '-' requires a numeric operand");
                return std::nullopt;
            }

            return Value(-*number);
        }

        return parsePrimary();
    }

    std::optional<Value> parsePrimary() {
        skipWhitespace();

        if (consumeKeyword("fn")) {
            skipWhitespace();
            if (!consumeOperator("(")) {
                setError("expected '(' after fn");
                return std::nullopt;
            }

            auto lambda = std::make_shared<LambdaValue>();

            skipWhitespace();
            if (!consumeOperator(")")) {
                while (true) {
                    const std::string parameter = parseLambdaIdentifier();
                    if (parameter.empty()) {
                        if (error_.empty()) {
                            setError("expected lambda parameter name");
                        }
                        return std::nullopt;
                    }
                    lambda->parameters.push_back(parameter);

                    skipWhitespace();
                    if (consumeOperator(")")) {
                        break;
                    }

                    if (!consumeOperator(",")) {
                        setError("expected ',' or ')' in lambda parameter list");
                        return std::nullopt;
                    }
                }
            }

            skipWhitespace();
            if (!consumeOperator("{")) {
                setError("expected '{' to start lambda body");
                return std::nullopt;
            }

            const std::optional<std::string> bodySource = parseBalancedBlockBody();
            if (!bodySource) {
                if (error_.empty()) {
                    setError("unterminated lambda body");
                }
                return std::nullopt;
            }

            lambda->bodySource = trimCopy(*bodySource);
            lambda->debugName = std::format("fn/{}", lambda->parameters.size());
            return Value(std::move(lambda));
        }

        if (consumeOperator("(")) {
            std::optional<Value> nested = parseOr();
            if (!nested) {
                return std::nullopt;
            }
            if (!consumeOperator(")")) {
                setError("expected ')'");
                return std::nullopt;
            }
            return nested;
        }

        if (consumeOperator("{")) {
            Value objectValue = Value::makeObject();
            ObjectValue* object = objectValue.asObject();
            if (object == nullptr) {
                setError("failed to create object value");
                return std::nullopt;
            }

            skipWhitespace();
            if (consumeOperator("}")) {
                return objectValue;
            }

            while (true) {
                const std::string key = parseObjectKey();
                if (key.empty()) {
                    if (error_.empty()) {
                        setError("expected object field name");
                    }
                    return std::nullopt;
                }

                if (!consumeOperator(":")) {
                    setError("expected ':' after object field name");
                    return std::nullopt;
                }

                std::optional<Value> fieldValue = parseOr();
                if (!fieldValue) {
                    return std::nullopt;
                }

                object->fields[key] = *fieldValue;

                skipWhitespace();
                if (consumeOperator("}")) {
                    break;
                }

                if (!consumeOperator(",")) {
                    setError("expected ',' or '}' in object literal");
                    return std::nullopt;
                }
            }

            return objectValue;
        }

        const std::string token = parseToken();
        if (token.empty()) {
            setError("expected expression");
            return std::nullopt;
        }

        if (startsWith(token, "\"") && token.size() >= 2 && token.back() == '"') {
            return parseLiteralValue(token);
        }

        if (startsWith(token, "event.")) {
            if (event_ == nullptr) {
                return Value(nullptr);
            }

            const std::string fieldName = token.substr(6);
            const auto found = event_->fields.find(fieldName);
            if (found == event_->fields.end()) {
                return Value(nullptr);
            }
            return parseLiteralValue(found->second);
        }

        if (startsWith(token, "var.")) {
            const Value* value = resolveBlackboardPath(blackboard_, token.substr(4));
            if (value == nullptr) {
                return Value(nullptr);
            }
            return *value;
        }

        if (startsWith(token, "arg.")) {
            if (locals_ == nullptr) {
                return Value(nullptr);
            }

            const Value* value = resolveBlackboardPath(*locals_, token.substr(4));
            if (value == nullptr) {
                return Value(nullptr);
            }
            return *value;
        }

        return parseLiteralValue(token);
    }

    std::string parseLambdaIdentifier() {
        skipWhitespace();
        const std::string token = parseToken();
        if (token.empty()) {
            return {};
        }

        if (token.front() == '"' || token.find('.') != std::string::npos) {
            setError(std::format("invalid lambda parameter '{}'", token));
            return {};
        }

        return token;
    }

    std::optional<std::string> parseBalancedBlockBody() {
        const size_t start = position_;
        int depth = 1;

        while (!atEnd()) {
            const char ch = source_[position_];
            if (ch == '"') {
                ++position_;
                while (!atEnd()) {
                    if (source_[position_] == '\\' && position_ + 1 < source_.size()) {
                        position_ += 2;
                        continue;
                    }
                    if (source_[position_] == '"') {
                        ++position_;
                        break;
                    }
                    ++position_;
                }
                continue;
            }

            if (ch == '{') {
                ++depth;
            } else if (ch == '}') {
                --depth;
                if (depth == 0) {
                    const size_t end = position_;
                    ++position_;
                    return std::string(source_.substr(start, end - start));
                }
            }

            ++position_;
        }

        setError("unterminated block body");
        return std::nullopt;
    }

    std::string parseObjectKey() {
        skipWhitespace();
        if (atEnd()) {
            return {};
        }

        if (source_[position_] == '"') {
            const std::string token = parseToken();
            if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
                return token.substr(1, token.size() - 2);
            }
            return {};
        }

        return parseToken();
    }

    std::string parseToken() {
        skipWhitespace();
        if (atEnd()) {
            return {};
        }

        if (source_[position_] == '"') {
            const size_t start = position_++;
            while (!atEnd() && source_[position_] != '"') {
                if (source_[position_] == '\\' && position_ + 1 < source_.size()) {
                    position_ += 2;
                    continue;
                }
                ++position_;
            }
            if (atEnd()) {
                setError("unterminated string literal");
                return {};
            }
            ++position_;
            return std::string(source_.substr(start, position_ - start));
        }

        const size_t start = position_;
        while (!atEnd()) {
            const char ch = source_[position_];
            if (std::isspace(static_cast<unsigned char>(ch)) != 0 ||
                ch == '(' || ch == ')' || ch == '{' || ch == '}' || ch == ',' || ch == ':') {
                break;
            }
            if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '<' || ch == '>' ||
                ch == '=' || ch == '!') {
                break;
            }
            ++position_;
        }

        if (start == position_) {
            return {};
        }

        return std::string(source_.substr(start, position_ - start));
    }

    bool consumeOperator(std::string_view op) {
        skipWhitespace();
        if (source_.substr(position_, op.size()) == op) {
            position_ += op.size();
            return true;
        }
        return false;
    }

    bool consumeKeyword(std::string_view keyword) {
        skipWhitespace();
        if (!startsWith(source_.substr(position_), keyword)) {
            return false;
        }

        const size_t end = position_ + keyword.size();
        if (end < source_.size()) {
            const char next = source_[end];
            if (std::isalnum(static_cast<unsigned char>(next)) != 0 || next == '_' || next == '.') {
                return false;
            }
        }

        position_ = end;
        return true;
    }

    void skipWhitespace() {
        while (!atEnd() && std::isspace(static_cast<unsigned char>(source_[position_])) != 0) {
            ++position_;
        }
    }

    bool atEnd() const {
        return position_ >= source_.size();
    }

    std::string_view remainingSource() const {
        return source_.substr(position_);
    }

    void setError(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message);
        }
    }

    std::string_view source_;
    const InterpreterEvent* event_ = nullptr;
    const ValueMap& blackboard_;
    const ValueMap* locals_ = nullptr;
    size_t position_ = 0;
    std::string error_;
};

std::optional<Value> evaluateExpression(
    std::string_view expression,
    const InterpreterEvent* event,
    const ValueMap& blackboard,
    const ValueMap* locals,
    std::string* outError
) {
    ExpressionParser parser(expression, event, blackboard, locals);
    return parser.parse(outError);
}

std::optional<std::string> interpolateCommandArgument(
    std::string_view argument,
    const InterpreterEvent* event,
    const ValueMap& blackboard,
    std::string* outError
) {
    std::string result;
    size_t cursor = 0;

    while (cursor < argument.size()) {
        const size_t exprStart = argument.find("${", cursor);
        if (exprStart == std::string_view::npos) {
            result.append(argument.substr(cursor));
            break;
        }

        result.append(argument.substr(cursor, exprStart - cursor));
        const size_t exprBodyStart = exprStart + 2;
        const size_t exprEnd = argument.find('}', exprBodyStart);
        if (exprEnd == std::string_view::npos) {
            if (outError != nullptr) {
                *outError = "unterminated '${...}' interpolation";
            }
            return std::nullopt;
        }

        const std::string expression = trimCopy(argument.substr(exprBodyStart, exprEnd - exprBodyStart));
        if (expression.empty()) {
            if (outError != nullptr) {
                *outError = "empty interpolation expression";
            }
            return std::nullopt;
        }

        std::string expressionError;
        std::optional<Value> value = evaluateExpression(expression, event, blackboard, nullptr, &expressionError);
        if (!value) {
            if (outError != nullptr) {
                *outError = std::format(
                    "failed to evaluate interpolation expression '{}': {}",
                    expression,
                    expressionError
                );
            }
            return std::nullopt;
        }

        result.append(value->toString());
        cursor = exprEnd + 1;
    }

    return result;
}

}  // namespace

ValueType Value::type() const {
    if (std::holds_alternative<std::monostate>(storage)) {
        return ValueType::Null;
    }
    if (std::holds_alternative<double>(storage)) {
        return ValueType::Number;
    }
    if (std::holds_alternative<bool>(storage)) {
        return ValueType::Bool;
    }
    if (std::holds_alternative<std::string>(storage)) {
        return ValueType::String;
    }
    if (std::holds_alternative<ObjectPtr>(storage)) {
        return ValueType::Object;
    }
    if (std::holds_alternative<LambdaPtr>(storage)) {
        return ValueType::Lambda;
    }
    return ValueType::Entity;
}

bool Value::isNull() const {
    return std::holds_alternative<std::monostate>(storage);
}

std::optional<double> Value::asNumber() const {
    if (const double* number = std::get_if<double>(&storage)) {
        return *number;
    }
    return std::nullopt;
}

std::optional<bool> Value::asBool() const {
    if (const bool* boolean = std::get_if<bool>(&storage)) {
        return *boolean;
    }
    return std::nullopt;
}

std::optional<std::string_view> Value::asStringView() const {
    if (const std::string* text = std::get_if<std::string>(&storage)) {
        return *text;
    }
    return std::nullopt;
}

const ObjectValue* Value::asObject() const {
    if (const ObjectPtr* object = std::get_if<ObjectPtr>(&storage)) {
        return object->get();
    }
    return nullptr;
}

ObjectValue* Value::asObject() {
    if (ObjectPtr* object = std::get_if<ObjectPtr>(&storage)) {
        return object->get();
    }
    return nullptr;
}

const LambdaValue* Value::asLambda() const {
    if (const LambdaPtr* lambda = std::get_if<LambdaPtr>(&storage)) {
        return lambda->get();
    }
    return nullptr;
}

LambdaValue* Value::asLambda() {
    if (LambdaPtr* lambda = std::get_if<LambdaPtr>(&storage)) {
        return lambda->get();
    }
    return nullptr;
}

std::optional<flecs::entity_t> Value::asEntityId() const {
    if (const flecs::entity_t* entity = std::get_if<flecs::entity_t>(&storage)) {
        return *entity;
    }
    return std::nullopt;
}

std::string Value::toString() const {
    if (const double* number = std::get_if<double>(&storage)) {
        return std::format("{}", *number);
    }
    if (const bool* boolean = std::get_if<bool>(&storage)) {
        return *boolean ? "true" : "false";
    }
    if (const std::string* text = std::get_if<std::string>(&storage)) {
        return *text;
    }
    if (const ObjectValue* object = asObject()) {
        return std::format("[object:{} fields]", object->fields.size());
    }
    if (const LambdaValue* lambda = asLambda()) {
        const std::string debugName = lambda->debugName.empty() ? "lambda" : lambda->debugName;
        if (!lambda->bodySource.empty()) {
            return std::format(
                "[lambda:{} params={} body=\"{}\"]",
                debugName,
                lambda->parameters.size(),
                lambda->bodySource
            );
        }
        return std::format("[lambda:{} params={}]", debugName, lambda->parameters.size());
    }
    if (const auto entity = asEntityId()) {
        return std::format("#{}", static_cast<uint64_t>(*entity));
    }
    return "null";
}

std::string Value::debugString() const {
    switch (type()) {
        case ValueType::Null:
            return "null";
        case ValueType::Number:
            return std::format("number({})", toString());
        case ValueType::Bool:
            return std::format("bool({})", toString());
        case ValueType::String:
            return std::format("string(\"{}\")", toString());
        case ValueType::Object:
            return std::format("object({})", toString());
        case ValueType::Lambda:
            return std::format("lambda({})", toString());
        case ValueType::Entity:
            return std::format("entity({})", toString());
    }

    return "unknown";
}

Value Value::makeObject() {
    return Value(std::make_shared<ObjectValue>());
}

Value Value::makeLambda(std::string debugName) {
    auto lambda = std::make_shared<LambdaValue>();
    lambda->debugName = std::move(debugName);
    return Value(std::move(lambda));
}

Value parseLiteralValue(std::string_view text) {
    const std::string trimmed = trimCopy(text);
    if (trimmed.empty()) {
        return Value(std::string{});
    }

    if (iequals(trimmed, "null")) {
        return Value(nullptr);
    }

    if (iequals(trimmed, "true")) {
        return Value(true);
    }

    if (iequals(trimmed, "false")) {
        return Value(false);
    }

    try {
        size_t consumed = 0;
        const double number = std::stod(trimmed, &consumed);
        if (consumed == trimmed.size()) {
            return Value(number);
        }
    } catch (const std::exception&) {
    }

    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return Value(trimmed.substr(1, trimmed.size() - 2));
    }

    return Value(trimmed);
}

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
        const std::string valueText = trimCopy(text.substr(split + 1));
        if (key.empty()) {
            recordError("set_blackboard requires a non-empty key");
            return false;
        }

        std::string expressionError;
        std::optional<Value> value = evaluateExpression(valueText, nullptr, blackboard_, nullptr, &expressionError);
        if (!value) {
            recordError(std::format(
                "set_blackboard failed to evaluate '{}': {}",
                valueText,
                expressionError
            ));
            return false;
        }

        std::string assignmentError;
        if (!assignBlackboardPath(blackboard_, key, *value, &assignmentError)) {
            recordError(std::format(
                "set_blackboard failed to assign '{}': {}",
                key,
                assignmentError
            ));
            return false;
        }
        Logger::get().info(
            "Interpreter blackboard '{}' set to {}",
            key,
            value->debugString()
        );
        return true;
    });

    registerCommand("log_blackboard", [this](const CommandContext&, std::string_view argument) {
        std::string key = trimCopy(argument);
        const Value* value = resolveBlackboardPath(blackboard_, key);
        if (value == nullptr) {
            Logger::get().info("Interpreter blackboard '{}' is unset", key);
            return true;
        }

        Logger::get().info("Interpreter blackboard '{}': {}", key, value->debugString());
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

    registerCommand("bind_host_callback", [this](const CommandContext&, std::string_view argument) {
        std::string text = trimCopy(argument);
        const size_t split = text.find(' ');
        if (split == std::string::npos) {
            recordError(std::format("bind_host_callback expects '<name> <blackboard_path>', got '{}'", text));
            return false;
        }

        std::string callbackName = trimCopy(text.substr(0, split));
        std::string callbackPath = trimCopy(text.substr(split + 1));
        if (startsWith(callbackPath, "var.")) {
            callbackPath = callbackPath.substr(4);
        }

        return bindHostCallback(std::move(callbackName), callbackPath);
    });

    registerCommand("ecs_create_entity", [this](const CommandContext&, std::string_view argument) {
        const std::vector<std::string> args = splitWords(argument);
        if (args.empty() || args.size() > 2) {
            recordError(std::format(
                "ecs_create_entity expects '<blackboard_path> [entity_name]', got '{}'",
                trimCopy(argument)
            ));
            return false;
        }

        flecs::entity entity = args.size() == 2
            ? world_.entity(args[1].c_str())
            : world_.entity();

        std::string assignmentError;
        const std::string targetPath = stripVarPrefix(args[0]);
        if (!assignBlackboardPath(blackboard_, targetPath, Value(entity.id()), &assignmentError)) {
            recordError(std::format(
                "ecs_create_entity failed to assign '{}': {}",
                targetPath,
                assignmentError
            ));
            return false;
        }

        Logger::get().info(
            "Interpreter ECS created entity '{}' as {}",
            args.size() == 2 ? args[1] : std::string("<unnamed>"),
            static_cast<uint64_t>(entity.id())
        );
        return true;
    });

    registerCommand("ecs_find_entity", [this](const CommandContext&, std::string_view argument) {
        const std::vector<std::string> args = splitWords(argument);
        if (args.size() != 2) {
            recordError(std::format(
                "ecs_find_entity expects '<blackboard_path> <entity_name>', got '{}'",
                trimCopy(argument)
            ));
            return false;
        }

        const std::string targetPath = stripVarPrefix(args[0]);
        flecs::entity entity = world_.lookup(args[1].c_str());
        Value value = entity && entity.is_alive()
            ? Value(entity.id())
            : Value(nullptr);

        std::string assignmentError;
        if (!assignBlackboardPath(blackboard_, targetPath, std::move(value), &assignmentError)) {
            recordError(std::format(
                "ecs_find_entity failed to assign '{}': {}",
                targetPath,
                assignmentError
            ));
            return false;
        }

        Logger::get().info(
            "Interpreter ECS lookup '{}' -> {}",
            args[1],
            entity && entity.is_alive() ? std::format("#{}", static_cast<uint64_t>(entity.id())) : "null"
        );
        return true;
    });

    registerCommand("ecs_destroy_entity", [this](const CommandContext&, std::string_view argument) {
        std::string error;
        std::optional<flecs::entity> entity = resolveEntityRef(world_, blackboard_, argument, &error);
        if (!entity) {
            recordError(std::format("ecs_destroy_entity failed: {}", error));
            return false;
        }

        const flecs::entity_t id = entity->id();
        entity->destruct();
        Logger::get().info("Interpreter ECS destroyed entity #{}", static_cast<uint64_t>(id));
        return true;
    });

    registerCommand("ecs_add_tag", [this](const CommandContext&, std::string_view argument) {
        const std::vector<std::string> args = splitWords(argument);
        if (args.size() != 2) {
            recordError(std::format(
                "ecs_add_tag expects '<entity_ref> <tag_name>', got '{}'",
                trimCopy(argument)
            ));
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveEntityRef(world_, blackboard_, args[0], &error);
        if (!entity) {
            recordError(std::format("ecs_add_tag failed: {}", error));
            return false;
        }

        flecs::entity tag = getOrCreateTag(world_, args[1]);
        entity->add(tag);
        Logger::get().info(
            "Interpreter ECS added tag '{}' to #{}",
            args[1],
            static_cast<uint64_t>(entity->id())
        );
        return true;
    });

    registerCommand("ecs_remove_tag", [this](const CommandContext&, std::string_view argument) {
        const std::vector<std::string> args = splitWords(argument);
        if (args.size() != 2) {
            recordError(std::format(
                "ecs_remove_tag expects '<entity_ref> <tag_name>', got '{}'",
                trimCopy(argument)
            ));
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveEntityRef(world_, blackboard_, args[0], &error);
        if (!entity) {
            recordError(std::format("ecs_remove_tag failed: {}", error));
            return false;
        }

        flecs::entity tag = world_.lookup(args[1].c_str());
        if (!tag) {
            Logger::get().info("Interpreter ECS tag '{}' is unset; remove skipped", args[1]);
            return true;
        }

        entity->remove(tag);
        Logger::get().info(
            "Interpreter ECS removed tag '{}' from #{}",
            args[1],
            static_cast<uint64_t>(entity->id())
        );
        return true;
    });

    registerCommand("ecs_has_tag", [this](const CommandContext&, std::string_view argument) {
        const std::vector<std::string> args = splitWords(argument);
        if (args.size() != 3) {
            recordError(std::format(
                "ecs_has_tag expects '<blackboard_path> <entity_ref> <tag_name>', got '{}'",
                trimCopy(argument)
            ));
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveEntityRef(world_, blackboard_, args[1], &error);
        if (!entity) {
            recordError(std::format("ecs_has_tag failed: {}", error));
            return false;
        }

        flecs::entity tag = world_.lookup(args[2].c_str());
        const bool hasTag = tag && entity->has(tag);

        std::string assignmentError;
        const std::string targetPath = stripVarPrefix(args[0]);
        if (!assignBlackboardPath(blackboard_, targetPath, Value(hasTag), &assignmentError)) {
            recordError(std::format(
                "ecs_has_tag failed to assign '{}': {}",
                targetPath,
                assignmentError
            ));
            return false;
        }

        Logger::get().info(
            "Interpreter ECS has_tag #{} {} -> {}",
            static_cast<uint64_t>(entity->id()),
            args[2],
            hasTag ? "true" : "false"
        );
        return true;
    });

    registerCommand("ecs_store_entity_id", [this](const CommandContext&, std::string_view argument) {
        const std::vector<std::string> args = splitWords(argument);
        if (args.size() != 2) {
            recordError(std::format(
                "ecs_store_entity_id expects '<blackboard_path> <entity_ref>', got '{}'",
                trimCopy(argument)
            ));
            return false;
        }

        std::string error;
        std::optional<flecs::entity> entity = resolveEntityRef(world_, blackboard_, args[1], &error);
        if (!entity) {
            recordError(std::format("ecs_store_entity_id failed: {}", error));
            return false;
        }

        std::string assignmentError;
        const std::string targetPath = stripVarPrefix(args[0]);
        if (!assignBlackboardPath(
                blackboard_,
                targetPath,
                Value(std::format("{}", static_cast<uint64_t>(entity->id()))),
                &assignmentError
            )) {
            recordError(std::format(
                "ecs_store_entity_id failed to assign '{}': {}",
                targetPath,
                assignmentError
            ));
            return false;
        }

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

        if (startsWith(view, "if ")) {
            currentRule->conditionExpression = trimCopy(skipPrefix(view, "if "));
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

        if (startsWith(view, "action set ")) {
            std::string payload = trimCopy(skipPrefix(view, "action set "));
            const size_t split = payload.find(' ');
            if (split == std::string::npos) {
                recordError(std::format(
                    "{}:{}: action set expects '<identifier> <expression>'",
                    program.origin,
                    lineNumber
                ));
                continue;
            }

            std::string key = trimCopy(payload.substr(0, split));
            std::string expression = trimCopy(payload.substr(split + 1));
            if (key.empty() || expression.empty()) {
                recordError(std::format(
                    "{}:{}: action set expects a non-empty identifier and expression",
                    program.origin,
                    lineNumber
                ));
                continue;
            }

            currentRule->actions.push_back({
                ScriptAction::Type::SetBlackboard,
                std::move(key),
                std::move(expression)
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

bool FlecsInterpreterHost::hasBoundHostCallback(std::string_view name) const {
    return hostCallbackBindings_.contains(std::string(name));
}

bool FlecsInterpreterHost::bindHostCallback(std::string name, std::string_view blackboardPath) {
    if (name.empty()) {
        recordError("Host callback name must not be empty");
        return false;
    }

    const Value* value = resolveBlackboardPath(blackboard_, blackboardPath);
    if (value == nullptr) {
        recordError(std::format(
            "Cannot bind host callback '{}': blackboard path '{}' is unset",
            name,
            blackboardPath
        ));
        return false;
    }

    if (value->asLambda() == nullptr) {
        recordError(std::format(
            "Cannot bind host callback '{}': blackboard path '{}' is not a lambda",
            name,
            blackboardPath
        ));
        return false;
    }

    hostCallbackBindings_[name] = std::string(blackboardPath);
    Logger::get().info(
        "Interpreter bound host callback '{}' to '{}'",
        name,
        blackboardPath
    );
    return true;
}

std::optional<Value> FlecsInterpreterHost::invokeHostCallback(
    std::string_view name,
    const ValueMap& arguments,
    std::string* outError
) {
    const auto binding = hostCallbackBindings_.find(std::string(name));
    if (binding == hostCallbackBindings_.end()) {
        if (outError != nullptr) {
            *outError = std::format("host callback '{}' is not bound", name);
        }
        return std::nullopt;
    }

    const Value* value = resolveBlackboardPath(blackboard_, binding->second);
    if (value == nullptr) {
        if (outError != nullptr) {
            *outError = std::format(
                "bound callback '{}' points to missing path '{}'",
                name,
                binding->second
            );
        }
        return std::nullopt;
    }

    const LambdaValue* lambda = value->asLambda();
    if (lambda == nullptr) {
        if (outError != nullptr) {
            *outError = std::format(
                "bound callback '{}' no longer resolves to a lambda",
                name
            );
        }
        return std::nullopt;
    }

    ValueMap localValues = arguments;
    std::string expressionError;
    std::optional<Value> result = evaluateExpression(
        lambda->bodySource,
        nullptr,
        blackboard_,
        &localValues,
        &expressionError
    );
    if (!result) {
        if (outError != nullptr) {
            *outError = std::format(
                "lambda callback '{}' failed: {}",
                name,
                expressionError
            );
        }
        return std::nullopt;
    }

    Logger::get().info(
        "Interpreter invoked host callback '{}' -> {}",
        name,
        result->debugString()
    );
    return result;
}

bool FlecsInterpreterHost::setBlackboardValue(std::string_view path, Value value, std::string* outError) {
    std::string assignmentError;
    if (!assignBlackboardPath(blackboard_, path, std::move(value), &assignmentError)) {
        if (outError != nullptr) {
            *outError = assignmentError;
        }
        return false;
    }

    return true;
}

std::optional<Value> FlecsInterpreterHost::getBlackboardValue(std::string_view path) const {
    const Value* value = resolveBlackboardPath(blackboard_, path);
    if (value == nullptr) {
        return std::nullopt;
    }

    return *value;
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

    if (!rule.conditionExpression.empty()) {
        std::string conditionError;
        std::optional<Value> conditionValue = evaluateExpression(
            rule.conditionExpression,
            event,
            blackboard_,
            nullptr,
            &conditionError
        );
        if (!conditionValue) {
            recordError(std::format(
                "Rule '{}' condition failed to evaluate: {}",
                rule.name,
                conditionError
            ));
            return false;
        }

        if (!coerceValueToBool(*conditionValue)) {
            return false;
        }
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

            std::string interpolatedArgument;
            std::string interpolationError;
            if (action.argument.find("${") != std::string::npos) {
                std::optional<std::string> resolvedArgument = interpolateCommandArgument(
                    action.argument,
                    event,
                    blackboard_,
                    &interpolationError
                );
                if (!resolvedArgument) {
                    recordError(std::format(
                        "Failed to interpolate command argument for '{}': {}",
                        action.verb,
                        interpolationError
                    ));
                    return false;
                }
                interpolatedArgument = std::move(*resolvedArgument);
            } else {
                interpolatedArgument = action.argument;
            }

            CommandContext context{
                .world = world_,
                .event = event,
                .blackboard = blackboard_,
            };
            return found->second(context, interpolatedArgument);
        }

        case ScriptAction::Type::SetBlackboard: {
            std::string expressionError;
            std::optional<Value> value = evaluateExpression(
                action.argument,
                event,
                blackboard_,
                nullptr,
                &expressionError
            );
            if (!value) {
                recordError(std::format(
                    "Failed to evaluate set expression for '{}': {}",
                    action.verb,
                    expressionError
                ));
                return false;
            }

            std::string assignmentError;
            if (!assignBlackboardPath(blackboard_, action.verb, *value, &assignmentError)) {
                recordError(std::format(
                    "Failed to assign blackboard path '{}': {}",
                    action.verb,
                    assignmentError
                ));
                return false;
            }

            Logger::get().info(
                "Interpreter blackboard '{}' set to {}",
                action.verb,
                value->debugString()
            );
            return true;
        }
    }

    return false;
}

void FlecsInterpreterHost::recordError(std::string message) {
    Logger::get().error("Interpreter: {}", message);
    errors_.push_back(std::move(message));
}

}  // namespace tremor::script
