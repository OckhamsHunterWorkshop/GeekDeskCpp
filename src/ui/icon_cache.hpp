#pragma once

#include "core/models.hpp"
#include "platform/com_ptr.hpp"
#include "ui/icon_crop.hpp"

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace geekdesk::ui {

/// Asynchronous STA icon loader with transparent moat cropping, PNG disk cache,
/// and bounded LRU in-memory BGRA cache.
class IconCache {
public:
    IconCache() = default;
    ~IconCache();

    IconCache(const IconCache&) = delete;
    IconCache& operator=(const IconCache&) = delete;

    void Initialize(std::filesystem::path dataRoot, HWND notifyHwnd, UINT notifyMsg);
    void Shutdown();

    void SetDataRoot(std::filesystem::path dataRoot);
    void ClearMemoryCache();
    void WaitUntilIdle(int timeoutMs = 2000) const;

    /// Build stable cache key for a LaunchItem.
    [[nodiscard]] static std::string BuildMemoryKey(const core::LaunchItem& item);

    /// Return cached BGRA icon if present in memory, or enqueue background extraction.
    [[nodiscard]] bool TryGetOrRequest(const core::LaunchItem& item, BgraImage& outImage);

    /// Synchronously load and decode an image file (custom wallpaper / icon) into PBGRA32.
    [[nodiscard]] static bool LoadImageFileToPbgra(const std::filesystem::path& filePath,
                                                     int maxDecodeWidth,
                                                     BgraImage& outImage);

private:
    struct IconRequest {
        std::string memoryKey;
        core::LaunchItem item;
        std::filesystem::path dataRoot;
    };

    void WorkerLoop();
    void PutInMemoryCache(const std::string& key, BgraImage image);
    [[nodiscard]] static std::optional<std::filesystem::path> ResolveDiskCachePath(
        const std::filesystem::path& dataRoot,
        const core::LaunchItem& item);

    static constexpr std::size_t kMaxMemoryEntries = 96;
    static constexpr std::size_t kMaxQueueCapacity = 512;

    HWND notifyHwnd_ = nullptr;
    UINT notifyMsg_ = 0;

    std::mutex mutex_;
    std::condition_variable cv_;
    mutable std::condition_variable idleCv_;
    std::filesystem::path dataRoot_;
    bool stopRequested_ = false;
    bool initialized_ = false;
    bool workerBusy_ = false;

    std::deque<IconRequest> queue_;
    std::unordered_set<std::string> inFlight_;

    struct CacheEntry {
        BgraImage image;
        std::list<std::string>::iterator lruIt;
    };
    std::list<std::string> lruList_;
    std::unordered_map<std::string, CacheEntry> memoryCache_;

    std::thread worker_;
};

} // namespace geekdesk::ui
