#include "Source/Foundation/TremorTrace/tremor_profiler.h"

#include <algorithm>

namespace tremor::trace {

namespace {
constexpr double kAverageBlend = 0.1;
constexpr size_t kMaxDisplayRecords = 8;
}

Profiler& Profiler::instance() {
    static Profiler profiler;
    return profiler;
}

void Profiler::beginFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    activeFrameRecords_.clear();
    frameStart_ = std::chrono::steady_clock::now();
    frameActive_ = true;
}

void Profiler::endFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!frameActive_) {
        return;
    }

    const auto frameEnd = std::chrono::steady_clock::now();
    const double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart_).count();

    snapshot_.frameMs = frameMs;
    if (!frameStatsInitialized_) {
        snapshot_.avgFrameMs = frameMs;
        snapshot_.maxFrameMs = frameMs;
        frameStatsInitialized_ = true;
    } else {
        snapshot_.avgFrameMs += (frameMs - snapshot_.avgFrameMs) * kAverageBlend;
        snapshot_.maxFrameMs = std::max(snapshot_.maxFrameMs, frameMs);
    }

    for (const auto& [name, active] : activeFrameRecords_) {
        HistoryRecord& history = historyRecords_[name];
        history.lastMs = active.totalMs;
        history.callCount = active.callCount;
        if (!history.initialized) {
            history.avgMs = active.totalMs;
            history.maxMs = active.totalMs;
            history.initialized = true;
        } else {
            history.avgMs += (active.totalMs - history.avgMs) * kAverageBlend;
            history.maxMs = std::max(history.maxMs, active.totalMs);
        }
    }

    std::vector<ProfileDisplayRecord> records;
    records.reserve(activeFrameRecords_.size());
    for (const auto& [name, active] : activeFrameRecords_) {
        const HistoryRecord& history = historyRecords_.at(name);
        records.push_back(ProfileDisplayRecord{
            name,
            history.lastMs,
            history.avgMs,
            history.maxMs,
            history.callCount
        });
    }

    std::sort(records.begin(), records.end(), [](const ProfileDisplayRecord& a, const ProfileDisplayRecord& b) {
        return a.lastMs > b.lastMs;
    });

    if (records.size() > kMaxDisplayRecords) {
        records.resize(kMaxDisplayRecords);
    }

    snapshot_.topRecords = std::move(records);
    frameActive_ = false;
}

void Profiler::addSample(std::string_view name, double milliseconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!frameActive_) {
        return;
    }

    ActiveRecord& record = activeFrameRecords_[std::string(name)];
    record.totalMs += milliseconds;
    record.callCount += 1;
}

ProfileFrameSnapshot Profiler::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

ScopedCpuZone::ScopedCpuZone(std::string_view name)
    : name_(name),
      start_(std::chrono::steady_clock::now()) {}

ScopedCpuZone::~ScopedCpuZone() {
    const auto end = std::chrono::steady_clock::now();
    const double milliseconds = std::chrono::duration<double, std::milli>(end - start_).count();
    Profiler::instance().addSample(name_, milliseconds);
}

} // namespace tremor::trace
