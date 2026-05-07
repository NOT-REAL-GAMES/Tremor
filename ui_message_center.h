#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace tremor {

struct UiMessage {
    std::string text;
    float remainingTimeSeconds = 0.0f;
    uint32_t color = 0xFFD060FF;
};

class UiMessageCenter {
public:
    static constexpr size_t MaxQueuedMessages = 6;
    static constexpr size_t MaxVisibleMessages = 4;

    static UiMessageCenter& instance();

    void enqueue(std::string text, float durationSeconds = 4.0f, uint32_t color = 0xFFD060FF);
    void update(float deltaTimeSeconds);
    std::vector<UiMessage> snapshotVisibleMessages() const;
    bool consumeDirty();

private:
    UiMessageCenter() = default;

    mutable std::mutex mutex_;
    std::deque<UiMessage> messages_;
    bool dirty_ = false;
};

}  // namespace tremor
