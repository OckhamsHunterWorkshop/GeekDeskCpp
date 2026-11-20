#pragma once

#include "core/models.hpp"

#include <filesystem>
#include <string>

namespace geekdesk::platform {

struct LaunchResult {
    bool ok = false;
    std::string error;
};

/// Launch a LaunchItem via Windows ShellExecuteExW (supporting files, .lnk, folders, URLs,
/// shell items, arguments, working directory, and UAC runas elevation).
[[nodiscard]] LaunchResult LaunchCatalogItem(const core::LaunchItem& item);

/// Open a local folder in Windows Explorer.
[[nodiscard]] LaunchResult OpenDirectoryInExplorer(const std::filesystem::path& dir);

} // namespace geekdesk::platform
