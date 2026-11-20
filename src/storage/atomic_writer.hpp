#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace geekdesk::storage {

struct WriteResult {
    bool ok = false;
    std::string error;
};

/// Atomically write UTF-8 bytes to targetPath using a temporary file, FlushFileBuffers,
/// and MoveFileExW with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH.
[[nodiscard]] WriteResult WriteFileAtomic(const std::filesystem::path& targetPath,
                                          std::string_view utf8Bytes);

/// Read an entire file into a UTF-8 byte buffer using Win32 CreateFileW (share read/write/delete).
[[nodiscard]] bool ReadFileUtf8(const std::filesystem::path& filePath,
                                std::string& outBytes,
                                std::string& outError);

/// Single-background-thread debounced writer that coalesces repeated saves per path
/// and flushes cleanly on explicit Flush() or destruction.
class DebouncedFileWriter {
public:
    using ErrorCallback = std::function<void(const std::filesystem::path&, const std::string&)>;

    explicit DebouncedFileWriter(ErrorCallback onError = {});
    ~DebouncedFileWriter();

    DebouncedFileWriter(const DebouncedFileWriter&) = delete;
    DebouncedFileWriter& operator=(const DebouncedFileWriter&) = delete;

    /// Queue or replace the latest pending payload for targetPath after delay.
    void Schedule(std::filesystem::path targetPath,
                  std::string utf8Payload,
                  std::chrono::milliseconds delay = std::chrono::milliseconds(250));

    /// Block until all currently scheduled writes have completed to disk.
    void Flush();

    [[nodiscard]] std::uint64_t CompletedWritesCount() const noexcept;

private:
    struct PendingEntry {
        std::filesystem::path path;
        std::string payload;
        std::chrono::steady_clock::time_point dueAt;
    };

    void WorkerLoop();

    ErrorCallback onError_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::wstring, PendingEntry> pending_;
    bool stopRequested_ = false;
    bool writingNow_ = false;
    std::uint64_t completedWrites_ = 0;
    std::thread worker_;
};

} // namespace geekdesk::storage
