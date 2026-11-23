#pragma once

#include "core/desktop_archive_policy.hpp"

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace geekdesk::platform {

struct ShellMoveBatchResult {
    std::vector<std::filesystem::path> finalPaths;
    std::size_t movedCount = 0;
    std::size_t fallbackCount = 0;
};

struct RestoreDesktopResult {
    bool ok = false;
    std::filesystem::path restoredPath;
    std::string error;
};

/// Resolve the current user's Desktop (including OneDrive redirection) and Public Desktop paths.
[[nodiscard]] core::DesktopKnownFolders GetSystemDesktopFolders();

/// Move a batch of files/folders into destDir using Windows IFileOperation
/// (supports undo, auto-rename on collision, progress UI, and UAC elevation for Public Desktop).
/// Any item whose move fails or is canceled retains its original path in finalPaths.
[[nodiscard]] ShellMoveBatchResult MovePathsWithShellOperation(
    HWND ownerHwnd,
    const std::vector<std::filesystem::path>& sourcePaths,
    const std::filesystem::path& destDir);

/// Move an archived file/shortcut/folder back to the current user's Desktop.
[[nodiscard]] RestoreDesktopResult RestoreArchivedItemToDesktop(
    HWND ownerHwnd,
    const std::filesystem::path& archivedPath);

/// Prompt the user to pick a Desktop archive folder and validate safety constraints
/// (disallows Desktop/subfolders/parent and GeekDeskCpp dataRoot; informs if leaving OneDrive).
[[nodiscard]] std::optional<std::filesystem::path> PickAndValidateDesktopArchiveDir(
    HWND ownerHwnd,
    const std::filesystem::path& appDataRoot,
    bool englishUi);

} // namespace geekdesk::platform
