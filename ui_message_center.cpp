#include "ui_message_center.h"

#include <algorithm>
#include <utility>

namespace tremor {

UiMessageCenter& UiMessageCenter::instance() {
    static UiMessageCenter center;
    return center;
}

void UiMessageCenter::enqueue(std::string text, float durationSeconds, uint32_t color) {
    if (text.empty()) {
        return;
    }

    std::scoped_lock lock(mutex_);
    if (messages_.size() >= MaxQueuedMessages) {
        messages_.pop_front();
    }

    messages_.push_back({
        std::move(text),
        std::max(0.01f, durationSeconds),
        color
    });
    dirty_ = true;
}

void UiMessageCenter::update(float deltaTimeSeconds) {
    std::scoped_lock lock(mutex_);

    bool removedExpiredMessages = false;
    for (UiMessage& message : messages_) {
        if (message.remainingTimeSeconds > 0.0f) {
            message.remainingTimeSeconds = std::max(0.0f, message.remainingTimeSeconds - deltaTimeSeconds);
        }
    }

    while (!messages_.empty() && messages_.front().remainingTimeSeconds <= 0.0f) {
        messages_.pop_front();
        removedExpiredMessages = true;
    }

    if (removedExpiredMessages) {
        dirty_ = true;
    }
}

std::vector<UiMessage> UiMessageCenter::snapshotVisibleMessages() const {
    std::scoped_lock lock(mutex_);

    const size_t visibleCount = std::min(messages_.size(), MaxVisibleMessages);
    std::vector<UiMessage> visibleMessages;
    visibleMessages.reserve(visibleCount);

    const size_t firstVisibleIndex = messages_.size() - visibleCount;
    for (size_t i = firstVisibleIndex; i < messages_.size(); ++i) {
        visibleMessages.push_back(messages_[i]);
    }

    return visibleMessages;
}

bool UiMessageCenter::consumeDirty() {
    std::scoped_lock lock(mutex_);
    const bool dirty = dirty_;
    dirty_ = false;
    return dirty;
}

}  // namespace tremor
