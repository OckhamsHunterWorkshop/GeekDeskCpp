#pragma once

#include "core/models.hpp"

#include <cstddef>

namespace geekdesk::core {

struct FolderSyncResult {
    std::size_t added = 0;
    std::size_t removed = 0;
    std::size_t preserved = 0;
    bool folderExists = false;
};

/// Synchronize a LinkedFolder CategoryGroup against its directory on disk.
/// Preserves existing item metadata (id, launchCount, lastLaunchedAt, customIconFile, arguments)
/// matched by case-insensitive relativePath, drops deleted entries, and adds new entries.
[[nodiscard]] FolderSyncResult SyncLinkedFolderGroup(CategoryGroup& group);

} // namespace geekdesk::core
