#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tremor {

struct UiMessageCommand {
    std::string text;
    float durationSeconds = 4.0f;
    uint32_t color = 0xFFD060FF;
};

struct UiMessageCommandParseResult {
    bool success = false;
    UiMessageCommand message;
    std::vector<std::string> warnings;
    std::string error;
};

UiMessageCommandParseResult parseUiMessageCommand(std::string_view payload);
bool enqueueUiMessageCommand(std::string_view payload, UiMessageCommandParseResult* outResult = nullptr);

}  // namespace tremor
