#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tremor::trace {

struct ProfileDisplayRecord {
    std::string name;
    double lastMs = 0.0;
    double avgMs = 0.0;
    double maxMs = 0.0;
    uint32_t callCount = 0;
};

struct ProfileFrameSnapshot {
    double frameMs = 0.0;
    double avgFrameMs = 0.0;
    double maxFrameMs = 0.0;
    std::vector<ProfileDisplayRecord> topRecords;
};

class Profiler {
public:
    static Profiler& instance();

    void beginFrame();
    void endFrame();
    void addSample(std::string_view name, double milliseconds);

    ProfileFrameSnapshot snapshot() const;

private:
    struct ActiveRecord {
        double totalMs = 0.0;
        uint32_t callCount = 0;
    };

    struct HistoryRecord {
        double lastMs = 0.0;
        double avgMs = 0.0;
        double maxMs = 0.0;
        uint32_t callCount = 0;
        bool initialized = false;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, ActiveRecord> activeFrameRecords_;
    std::unordered_map<std::string, HistoryRecord> historyRecords_;
    ProfileFrameSnapshot snapshot_;
    std::chrono::steady_clock::time_point frameStart_{};
    bool frameActive_ = false;
    bool frameStatsInitialized_ = false;
};

class ScopedCpuZone {
public:
    explicit ScopedCpuZone(std::string_view name);
    ~ScopedCpuZone();

    ScopedCpuZone(const ScopedCpuZone&) = delete;
    ScopedCpuZone& operator=(const ScopedCpuZone&) = delete;

private:
    std::string_view name_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace tremor::trace

#define TREMOR_PROFILE_SCOPE_CONCAT_IMPL(a, b) a##b
#define TREMOR_PROFILE_SCOPE_CONCAT(a, b) TREMOR_PROFILE_SCOPE_CONCAT_IMPL(a, b)
#define TREMOR_PROFILE_SCOPE(name) ::tremor::trace::ScopedCpuZone TREMOR_PROFILE_SCOPE_CONCAT(_tremorProfileScope_, __LINE__)(name)
#define TREMOR_PROFILE_FUNCTION() TREMOR_PROFILE_SCOPE(__FUNCTION__)
