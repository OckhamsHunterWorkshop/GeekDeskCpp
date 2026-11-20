#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace geekdesk::storage {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

/// Thread-safe daily rolling UTF-8 file logger writing to `<logsDir>/geekdesk-YYYY-MM-DD.log`.
class FileLogger {
public:
    FileLogger(std::filesystem::path logsDir, int retentionDays = 14);
    ~FileLogger();

    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

    void Log(LogLevel level, std::string_view message);
    void Info(std::string_view message) {
        Log(LogLevel::Info, message);
    }
    void Warn(std::string_view message) {
        Log(LogLevel::Warn, message);
    }
    void Error(std::string_view message) {
        Log(LogLevel::Error, message);
    }

    void Flush();

    [[nodiscard]] std::filesystem::path CurrentLogFilePath() const;

private:
    void RotateIfDateChangedLocked(std::string_view dateStamp);
    void PruneOldLogsLocked();

    std::filesystem::path logsDir_;
    int retentionDays_ = 14;
    mutable std::mutex mutex_;
    std::string activeDate_;
    std::filesystem::path activePath_;
    void* fileHandle_ = nullptr;
};

} // namespace geekdesk::storage
