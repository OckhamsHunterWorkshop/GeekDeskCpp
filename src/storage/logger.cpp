#include "storage/logger.hpp"

#include "core/utf8.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace geekdesk::storage {

namespace {

std::string_view LevelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INFO";
}

bool IsDigit(char ch) noexcept {
    return ch >= '0' && ch <= '9';
}

bool IsDailyLogFileName(std::string_view name) {
    // geekdesk-YYYY-MM-DD.log -> length 23
    if (name.size() != 23 || !name.starts_with("geekdesk-") || !name.ends_with(".log")) {
        return false;
    }
    const std::string_view datePart = name.substr(9, 10);
    for (std::size_t i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) {
            if (datePart[i] != '-') {
                return false;
            }
        } else if (!IsDigit(datePart[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

FileLogger::FileLogger(std::filesystem::path logsDir, int retentionDays)
    : logsDir_(std::move(logsDir)), retentionDays_(std::max(1, retentionDays)) {
    std::error_code ec;
    std::filesystem::create_directories(logsDir_, ec);
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char dateBuf[16]{};
    std::snprintf(dateBuf, sizeof(dateBuf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    std::lock_guard<std::mutex> lock(mutex_);
    RotateIfDateChangedLocked(dateBuf);
    PruneOldLogsLocked();
}

FileLogger::~FileLogger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fileHandle_ != nullptr) {
        auto h = static_cast<HANDLE>(fileHandle_);
        FlushFileBuffers(h);
        CloseHandle(h);
        fileHandle_ = nullptr;
    }
}

void FileLogger::Log(LogLevel level, std::string_view message) {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    char dateBuf[16]{};
    std::snprintf(dateBuf, sizeof(dateBuf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);

    char prefix[64]{};
    const std::string_view lvl = LevelName(level);
    const int n = std::snprintf(prefix, sizeof(prefix), "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%.*s] ",
                                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                                st.wMilliseconds, static_cast<int>(lvl.size()), lvl.data());

    std::string line;
    line.reserve(static_cast<std::size_t>(n > 0 ? n : 32) + message.size() + 2);
    if (n > 0) {
        line.append(prefix, static_cast<std::size_t>(n));
    }
    line.append(message);
    line.push_back('\n');

    std::lock_guard<std::mutex> lock(mutex_);
    RotateIfDateChangedLocked(dateBuf);
    if (fileHandle_ == nullptr) {
        return;
    }
    auto h = static_cast<HANDLE>(fileHandle_);
    DWORD written = 0;
    WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

void FileLogger::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fileHandle_ != nullptr) {
        FlushFileBuffers(static_cast<HANDLE>(fileHandle_));
    }
}

std::filesystem::path FileLogger::CurrentLogFilePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activePath_;
}

void FileLogger::RotateIfDateChangedLocked(std::string_view dateStamp) {
    if (activeDate_ == dateStamp && fileHandle_ != nullptr) {
        return;
    }
    if (fileHandle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(fileHandle_));
        fileHandle_ = nullptr;
    }
    activeDate_.assign(dateStamp);
    activePath_ = logsDir_ / core::utf8::PathFromUtf8("geekdesk-" + activeDate_ + ".log");

    HANDLE h = CreateFileW(activePath_.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        fileHandle_ = h;
    }
}

void FileLogger::PruneOldLogsLocked() {
    std::error_code ec;
    if (!std::filesystem::exists(logsDir_, ec) || !std::filesystem::is_directory(logsDir_, ec)) {
        return;
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    const auto cutoff = now - std::chrono::hours(24LL * retentionDays_);
    for (const auto& entry : std::filesystem::directory_iterator(logsDir_, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string name = core::utf8::PathToUtf8(entry.path().filename());
        if (!IsDailyLogFileName(name)) {
            continue;
        }
        const auto mtime = entry.last_write_time(ec);
        if (!ec && mtime < cutoff) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

} // namespace geekdesk::storage
