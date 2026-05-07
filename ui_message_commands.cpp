#include "ui_message_commands.h"

#include "ui_message_center.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace tremor {

namespace {

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

std::vector<std::string> splitPayload(std::string_view payload) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= payload.size()) {
        size_t separator = payload.find('|', start);
        if (separator == std::string_view::npos) {
            parts.push_back(trimCopy(payload.substr(start)));
            break;
        }

        parts.push_back(trimCopy(payload.substr(start, separator - start)));
        start = separator + 1;
    }

    return parts;
}

std::optional<float> tryParseDuration(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }

    try {
        const float duration = std::stof(value);
        if (duration <= 0.0f) {
            return std::nullopt;
        }
        return duration;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<uint32_t> tryParseColor(std::string value) {
    value = trimCopy(value);
    if (value.empty()) {
        return std::nullopt;
    }

    if (!value.empty() && value[0] == '#') {
        value.erase(0, 1);
    }

    if (value.size() != 8) {
        return std::nullopt;
    }

    for (char ch : value) {
        if (std::isxdigit(static_cast<unsigned char>(ch)) == 0) {
            return std::nullopt;
        }
    }

    try {
        return static_cast<uint32_t>(std::stoul(value, nullptr, 16));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

UiMessageCommandParseResult parseUiMessageCommand(std::string_view payload) {
    UiMessageCommandParseResult result;
    const std::vector<std::string> parts = splitPayload(payload);
    if (parts.empty() || parts[0].empty()) {
        result.error = "received an empty message";
        return result;
    }

    result.message.text = parts[0];

    if (parts.size() >= 2 && !parts[1].empty()) {
        const std::optional<float> parsedDuration = tryParseDuration(parts[1]);
        if (parsedDuration) {
            result.message.durationSeconds = *parsedDuration;
        } else {
            result.warnings.push_back("could not parse duration '" + parts[1] + "', using default");
        }
    }

    if (parts.size() >= 3 && !parts[2].empty()) {
        const std::optional<uint32_t> parsedColor = tryParseColor(parts[2]);
        if (parsedColor) {
            result.message.color = *parsedColor;
        } else {
            result.warnings.push_back("could not parse color '" + parts[2] + "', using default");
        }
    }

    result.success = true;
    return result;
}

bool enqueueUiMessageCommand(std::string_view payload, UiMessageCommandParseResult* outResult) {
    UiMessageCommandParseResult parsed = parseUiMessageCommand(payload);
    if (outResult != nullptr) {
        *outResult = parsed;
    }

    if (!parsed.success) {
        return false;
    }

    UiMessageCenter::instance().enqueue(
        parsed.message.text,
        parsed.message.durationSeconds,
        parsed.message.color
    );
    return true;
}

}  // namespace tremor
