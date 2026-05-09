#pragma once

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace Tremor {

class Logger {
public:
    enum class Level {
        Debug,
        Info,
        Warning,
        Error,
        Critical
    };

    struct Config {
        bool enableConsole = true;
        bool enableFileOutput = false;
        std::string logFilePath = "tremor.log";
        Level minLevel = Level::Info;
        bool useColors = true;
        bool showTimestamps = true;
        bool showSourceLocation = false;
    };

    static Logger& get() {
        static Logger instance;
        return instance;
    }

    static std::shared_ptr<Logger> create(const Config& config) {
        return std::make_shared<Logger>(config);
    }

    static std::shared_ptr<Logger> create() {
        return std::make_shared<Logger>(Config{});
    }

    explicit Logger(const Config& config)
        : m_config(config) {
        if (m_config.enableFileOutput) {
            m_logFile.open(m_config.logFilePath, std::ios::out | std::ios::app);
            if (!m_logFile) {
                std::cerr << "Failed to open log file: " << m_config.logFilePath << std::endl;
            }
        }
    }

    Logger() : Logger(Config{}) {}

    ~Logger() {
        if (m_logFile.is_open()) {
            m_logFile.close();
        }
    }

    template<typename... Args>
    void log(Level level, std::format_string<Args...> fmt, Args&&... args) {
        if (level < m_config.minLevel) {
            return;
        }

        try {
            std::string message = std::format(fmt, std::forward<Args>(args)...);
            logMessage(level, message);
        } catch (const std::format_error& e) {
            logMessage(Level::Error, std::format("Format error: {}", e.what()));
        }
    }

    template<typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        log(Level::Debug, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        log(Level::Info, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warning(std::format_string<Args...> fmt, Args&&... args) {
        log(Level::Warning, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        warning(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        log(Level::Error, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void critical(std::format_string<Args...> fmt, Args&&... args) {
        log(Level::Critical, fmt, std::forward<Args>(args)...);
    }

    void setLevel(Level level) {
        m_config.minLevel = level;
    }

    const Config& getConfig() const {
        return m_config;
    }

private:
    Config m_config;
    std::ofstream m_logFile;
    std::mutex m_mutex;

    std::string_view levelToString(Level level) const {
        switch (level) {
        case Level::Debug:    return "DEBUG";
        case Level::Info:     return "INFO";
        case Level::Warning:  return "WARNING";
        case Level::Error:    return "ERROR";
        case Level::Critical: return "CRITICAL";
        default:              return "UNKNOWN";
        }
    }

    std::string_view levelToColor(Level level) const {
        if (!m_config.useColors) {
            return "";
        }

        switch (level) {
        case Level::Debug:    return "\033[37m";
        case Level::Info:     return "\033[32m";
        case Level::Warning:  return "\033[33m";
        case Level::Error:    return "\033[31m";
        case Level::Critical: return "\033[35m";
        default:              return "\033[0m";
        }
    }

    std::string_view resetColor() const {
        return m_config.useColors ? "\033[0m" : "";
    }

    std::string formatTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &now_time_t);
#else
        localtime_r(&now_time_t, &tm_buf);
#endif

        return std::format(
            "{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
            tm_buf.tm_year + 1900,
            tm_buf.tm_mon + 1,
            tm_buf.tm_mday,
            tm_buf.tm_hour,
            tm_buf.tm_min,
            tm_buf.tm_sec,
            now_ms.count()
        );
    }

    void logMessage(
        Level level,
        std::string_view message,
        const std::source_location& location = std::source_location::current()
    ) {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::string fullMessage;
        if (m_config.showTimestamps) {
            fullMessage += std::format("[{}] ", formatTimestamp());
        }

        fullMessage += std::format(
            "{}{}{} ",
            levelToColor(level),
            levelToString(level),
            resetColor()
        );

        if (m_config.showSourceLocation) {
            fullMessage += std::format(
                "{}:{}:{}: ",
                std::filesystem::path(location.file_name()).filename().string(),
                location.line(),
                location.column()
            );
        }

        fullMessage += message;

        if (m_config.enableConsole) {
            std::cout << fullMessage << std::endl;
        }

        if (m_config.enableFileOutput && m_logFile.is_open()) {
            m_logFile << fullMessage << std::endl;
            m_logFile.flush();
        }
    }
};

}  // namespace Tremor

using Logger = Tremor::Logger;

#define TREMOR_LOG_DEBUG(...) ::Tremor::Logger::get().debug(__VA_ARGS__)
#define TREMOR_LOG_INFO(...) ::Tremor::Logger::get().info(__VA_ARGS__)
#define TREMOR_LOG_WARNING(...) ::Tremor::Logger::get().warning(__VA_ARGS__)
#define TREMOR_LOG_ERROR(...) ::Tremor::Logger::get().error(__VA_ARGS__)
#define TREMOR_LOG_CRITICAL(...) ::Tremor::Logger::get().critical(__VA_ARGS__)
