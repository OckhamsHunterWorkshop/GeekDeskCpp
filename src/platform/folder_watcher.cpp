#include "platform/folder_watcher.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <memory>

namespace geekdesk::platform {

namespace {

constexpr DWORD kWatchFilter =
    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE;

struct WatchEntry {
    std::string groupId;
    std::filesystem::path folderPath;
    HANDLE dirHandle = INVALID_HANDLE_VALUE;
    HANDLE eventHandle = nullptr;
    OVERLAPPED overlapped{};
    std::array<std::uint8_t, 8192> buffer{};
    bool pendingRead = false;
    bool pendingDebounce = false;
    std::chrono::steady_clock::time_point debounceDueAt;

    ~WatchEntry() {
        if (dirHandle != INVALID_HANDLE_VALUE) {
            CancelIo(dirHandle);
            CloseHandle(dirHandle);
            dirHandle = INVALID_HANDLE_VALUE;
        }
        if (eventHandle != nullptr) {
            CloseHandle(eventHandle);
            eventHandle = nullptr;
        }
    }
};

bool IssueRead(WatchEntry& entry) {
    if (entry.dirHandle == INVALID_HANDLE_VALUE || entry.eventHandle == nullptr) {
        return false;
    }
    ResetEvent(entry.eventHandle);
    entry.overlapped = OVERLAPPED{};
    entry.overlapped.hEvent = entry.eventHandle;

    DWORD bytesReturned = 0;
    const BOOL ok = ReadDirectoryChangesW(entry.dirHandle, entry.buffer.data(),
                                          static_cast<DWORD>(entry.buffer.size()), FALSE,
                                          kWatchFilter, &bytesReturned, &entry.overlapped, nullptr);
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        entry.pendingRead = false;
        return false;
    }
    entry.pendingRead = true;
    return true;
}

} // namespace

FolderWatcher::~FolderWatcher() {
    Stop();
}

void FolderWatcher::Start(std::vector<WatchedFolderSpec> folders,
                          ChangeCallback callback,
                          std::chrono::milliseconds debounce) {
    Stop();
    if (folders.empty() || !callback) {
        return;
    }

    HANDLE stopEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEv == nullptr) {
        return;
    }
    HANDLE readyEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    stopEvent_ = stopEv;
    running_ = true;
    worker_ = std::thread(&FolderWatcher::WorkerLoop, this, std::move(folders),
                          std::move(callback), debounce, static_cast<void*>(readyEv));
    if (readyEv != nullptr) {
        WaitForSingleObject(readyEv, 500);
        CloseHandle(readyEv);
    }
}

void FolderWatcher::Stop() {
    if (!running_) {
        return;
    }
    if (stopEvent_ != nullptr) {
        SetEvent(static_cast<HANDLE>(stopEvent_));
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    if (stopEvent_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }
    running_ = false;
}

void FolderWatcher::WorkerLoop(std::vector<WatchedFolderSpec> folders,
                               ChangeCallback callback,
                               std::chrono::milliseconds debounce,
                               void* readyEvent) {
    const std::size_t maxWatches = MAXIMUM_WAIT_OBJECTS - 1;
    std::vector<std::unique_ptr<WatchEntry>> entries;
    entries.reserve(std::min(folders.size(), maxWatches));

    for (const auto& spec : folders) {
        if (entries.size() >= maxWatches) {
            break;
        }
        std::error_code ec;
        if (spec.folderPath.empty() || !std::filesystem::is_directory(spec.folderPath, ec)) {
            continue;
        }
        HANDLE hDir = CreateFileW(
            spec.folderPath.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (hDir == INVALID_HANDLE_VALUE) {
            continue;
        }
        HANDLE hEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (hEv == nullptr) {
            CloseHandle(hDir);
            continue;
        }
        auto entry = std::make_unique<WatchEntry>();
        entry->groupId = spec.groupId;
        entry->folderPath = spec.folderPath;
        entry->dirHandle = hDir;
        entry->eventHandle = hEv;
        if (!IssueRead(*entry)) {
            continue;
        }
        entries.push_back(std::move(entry));
    }

    if (readyEvent != nullptr) {
        SetEvent(static_cast<HANDLE>(readyEvent));
    }

    auto stopEv = static_cast<HANDLE>(stopEvent_);
    if (entries.empty()) {
        WaitForSingleObject(stopEv, INFINITE);
        return;
    }

    while (true) {
        std::vector<HANDLE> waitHandles;
        std::vector<std::size_t> handleToEntry;
        waitHandles.reserve(entries.size() + 1);
        handleToEntry.reserve(entries.size() + 1);

        waitHandles.push_back(stopEv);
        handleToEntry.push_back(static_cast<std::size_t>(-1));

        bool hasDueTimer = false;
        auto nextDue = std::chrono::steady_clock::time_point::max();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i]->pendingRead) {
                waitHandles.push_back(entries[i]->eventHandle);
                handleToEntry.push_back(i);
            }
            if (entries[i]->pendingDebounce) {
                hasDueTimer = true;
                nextDue = std::min(nextDue, entries[i]->debounceDueAt);
            }
        }

        DWORD timeoutMs = INFINITE;
        if (hasDueTimer) {
            const auto now = std::chrono::steady_clock::now();
            if (nextDue <= now) {
                timeoutMs = 0;
            } else {
                const auto ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(nextDue - now).count();
                timeoutMs = static_cast<DWORD>(std::clamp<long long>(ms, 1, 60000));
            }
        }

        const DWORD waitRes = WaitForMultipleObjects(static_cast<DWORD>(waitHandles.size()),
                                                     waitHandles.data(), FALSE, timeoutMs);
        if (waitRes == WAIT_OBJECT_0) {
            break; // Stop event signaled
        }

        const auto now = std::chrono::steady_clock::now();
        if (waitRes > WAIT_OBJECT_0 &&
            waitRes < WAIT_OBJECT_0 + static_cast<DWORD>(waitHandles.size())) {
            const std::size_t idx = handleToEntry[waitRes - WAIT_OBJECT_0];
            if (idx < entries.size()) {
                WatchEntry& e = *entries[idx];
                DWORD bytesTransferred = 0;
                const BOOL got = GetOverlappedResult(e.dirHandle, &e.overlapped, &bytesTransferred,
                                                     FALSE);
                (void)bytesTransferred;
                e.pendingDebounce = true;
                e.debounceDueAt = now + debounce;
                if (!got || !IssueRead(e)) {
                    // Directory was removed or invalidated; stop reading this handle
                    e.pendingRead = false;
                }
            }
        }

        for (auto& e : entries) {
            if (e->pendingDebounce && now >= e->debounceDueAt) {
                e->pendingDebounce = false;
                callback(e->groupId);
            }
        }
    }
}

} // namespace geekdesk::platform
