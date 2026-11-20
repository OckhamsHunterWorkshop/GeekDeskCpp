#include "storage/atomic_writer.hpp"

#include "core/utf8.hpp"

#include <windows.h>

#include <atomic>
#include <limits>
#include <vector>

namespace geekdesk::storage {

namespace {

std::atomic<std::uint64_t> gTempSequence{0};

std::string FormatWin32Error(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer),
        0, nullptr);
    std::string msg = "Win32 error " + std::to_string(code);
    if (len > 0 && buffer != nullptr) {
        std::wstring_view wsv(buffer, len);
        while (!wsv.empty() && (wsv.back() == L'\r' || wsv.back() == L'\n' || wsv.back() == L' ')) {
            wsv.remove_suffix(1);
        }
        msg += " (" + core::utf8::WideToUtf8(wsv) + ")";
        LocalFree(buffer);
    }
    return msg;
}

} // namespace

WriteResult WriteFileAtomic(const std::filesystem::path& targetPath, std::string_view utf8Bytes) {
    WriteResult result;
    if (targetPath.empty()) {
        result.error = "Target path is empty";
        return result;
    }

    const std::filesystem::path parent = targetPath.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            result.error = "Failed to create parent directory: " + ec.message();
            return result;
        }
    }

    const std::uint64_t seq = gTempSequence.fetch_add(1, std::memory_order_relaxed);
    const std::wstring tempName = targetPath.filename().wstring() + L".tmp." +
                                  std::to_wstring(GetCurrentProcessId()) + L"." +
                                  std::to_wstring(seq);
    const std::filesystem::path tempPath = parent.empty() ? std::filesystem::path(tempName)
                                                          : (parent / tempName);

    HANDLE hFile = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        result.error = "CreateFileW failed for temp file: " + FormatWin32Error(GetLastError());
        return result;
    }

    bool writeOk = true;
    std::size_t totalWritten = 0;
    while (totalWritten < utf8Bytes.size()) {
        const std::size_t remaining = utf8Bytes.size() - totalWritten;
        const DWORD chunkSize = static_cast<DWORD>(
            std::min<std::size_t>(remaining, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(hFile, utf8Bytes.data() + totalWritten, chunkSize, &written, nullptr) ||
            written == 0) {
            result.error = "WriteFile failed: " + FormatWin32Error(GetLastError());
            writeOk = false;
            break;
        }
        totalWritten += written;
    }

    if (writeOk && !FlushFileBuffers(hFile)) {
        result.error = "FlushFileBuffers failed: " + FormatWin32Error(GetLastError());
        writeOk = false;
    }
    CloseHandle(hFile);

    if (!writeOk) {
        DeleteFileW(tempPath.c_str());
        return result;
    }

    DWORD lastErr = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (MoveFileExW(tempPath.c_str(), targetPath.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            result.ok = true;
            return result;
        }
        lastErr = GetLastError();
        if (lastErr != ERROR_ACCESS_DENIED && lastErr != ERROR_SHARING_VIOLATION) {
            break;
        }
        Sleep(10);
    }

    DeleteFileW(tempPath.c_str());
    result.error = "MoveFileExW failed: " + FormatWin32Error(lastErr);
    return result;
}

bool ReadFileUtf8(const std::filesystem::path& filePath,
                  std::string& outBytes,
                  std::string& outError) {
    outBytes.clear();
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        outError = "CreateFileW failed: " + FormatWin32Error(GetLastError());
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize)) {
        outError = "GetFileSizeEx failed: " + FormatWin32Error(GetLastError());
        CloseHandle(hFile);
        return false;
    }
    if (fileSize.QuadPart < 0 ||
        static_cast<std::uint64_t>(fileSize.QuadPart) >
            static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
        outError = "File size out of range";
        CloseHandle(hFile);
        return false;
    }

    const auto totalBytes = static_cast<std::size_t>(fileSize.QuadPart);
    outBytes.resize(totalBytes);

    std::size_t totalRead = 0;
    while (totalRead < totalBytes) {
        const std::size_t remaining = totalBytes - totalRead;
        const DWORD chunkSize =
            static_cast<DWORD>(std::min<std::size_t>(remaining, 1u << 20));
        DWORD readNow = 0;
        if (!ReadFile(hFile, outBytes.data() + totalRead, chunkSize, &readNow, nullptr)) {
            outError = "ReadFile failed: " + FormatWin32Error(GetLastError());
            outBytes.clear();
            CloseHandle(hFile);
            return false;
        }
        if (readNow == 0) {
            break;
        }
        totalRead += readNow;
    }
    outBytes.resize(totalRead);
    CloseHandle(hFile);
    return true;
}

DebouncedFileWriter::DebouncedFileWriter(ErrorCallback onError)
    : onError_(std::move(onError)), worker_(&DebouncedFileWriter::WorkerLoop, this) {}

DebouncedFileWriter::~DebouncedFileWriter() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Flush any remaining debounced items immediately by pulling due timestamps to now.
        const auto now = std::chrono::steady_clock::now();
        for (auto& [key, entry] : pending_) {
            entry.dueAt = now;
        }
        stopRequested_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void DebouncedFileWriter::Schedule(std::filesystem::path targetPath,
                                   std::string utf8Payload,
                                   std::chrono::milliseconds delay) {
    const std::wstring key = targetPath.wstring();
    const auto dueAt = std::chrono::steady_clock::now() + delay;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_[key] = PendingEntry{std::move(targetPath), std::move(utf8Payload), dueAt};
    }
    cv_.notify_all();
}

void DebouncedFileWriter::Flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    for (auto& [key, entry] : pending_) {
        entry.dueAt = now;
    }
    cv_.notify_all();
    cv_.wait(lock, [this]() { return pending_.empty() && !writingNow_; });
}

std::uint64_t DebouncedFileWriter::CompletedWritesCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return completedWrites_;
}

void DebouncedFileWriter::WorkerLoop() {
    while (true) {
        std::vector<PendingEntry> ready;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (true) {
                if (pending_.empty()) {
                    if (stopRequested_) {
                        return;
                    }
                    cv_.wait(lock);
                    continue;
                }

                auto nextDue = std::chrono::steady_clock::time_point::max();
                const auto now = std::chrono::steady_clock::now();
                for (auto it = pending_.begin(); it != pending_.end();) {
                    if (it->second.dueAt <= now) {
                        ready.push_back(std::move(it->second));
                        it = pending_.erase(it);
                    } else {
                        if (it->second.dueAt < nextDue) {
                            nextDue = it->second.dueAt;
                        }
                        ++it;
                    }
                }

                if (!ready.empty()) {
                    writingNow_ = true;
                    break;
                }

                cv_.wait_until(lock, nextDue);
            }
        }

        for (const auto& entry : ready) {
            const WriteResult wr = WriteFileAtomic(entry.path, entry.payload);
            if (!wr.ok && onError_) {
                onError_(entry.path, wr.error);
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            completedWrites_ += ready.size();
            writingNow_ = false;
        }
        cv_.notify_all();
    }
}

} // namespace geekdesk::storage
