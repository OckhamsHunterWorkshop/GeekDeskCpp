#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace geekdesk::platform {

struct WatchedFolderSpec {
    std::string groupId;
    std::filesystem::path folderPath;
};

/// Real-time Win32 overlapped ReadDirectoryChangesW watcher for LinkedFolder groups.
/// Coalesces burst filesystem writes per group before invoking ChangeCallback.
class FolderWatcher {
public:
    using ChangeCallback = std::function<void(const std::string& groupId)>;

    FolderWatcher() = default;
    ~FolderWatcher();

    FolderWatcher(const FolderWatcher&) = delete;
    FolderWatcher& operator=(const FolderWatcher&) = delete;

    void Start(std::vector<WatchedFolderSpec> folders,
               ChangeCallback callback,
               std::chrono::milliseconds debounce = std::chrono::milliseconds(400));

    void Stop();

private:
    void WorkerLoop(std::vector<WatchedFolderSpec> folders,
                    ChangeCallback callback,
                    std::chrono::milliseconds debounce,
                    void* readyEvent);

    void* stopEvent_ = nullptr;
    std::thread worker_;
    bool running_ = false;
};

} // namespace geekdesk::platform
