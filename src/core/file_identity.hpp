#pragma once

#include "core/models.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace geekdesk::core {

enum class FileIdRecoveryStatus {
    Recovered,
    InRecycleBin,
    NotFound,
};

struct FileIdRecoveryResult {
    FileIdRecoveryStatus status = FileIdRecoveryStatus::NotFound;
    std::filesystem::path recoveredPath;
};

/// Capture the NTFS volume serial + 64-bit File ID ("VVVVVVVV:HHHHHHHHLLLLLLLL") for a file or folder.
/// Opens the object with zero-access backup semantics (no file lock, no admin required).
[[nodiscard]] std::optional<std::string> CaptureFileIdentity(const std::filesystem::path& path);

/// Return true if any path segment is "$Recycle.Bin" or "RECYCLER" (case-insensitive).
[[nodiscard]] bool IsPathInRecycleBin(const std::filesystem::path& path);

/// Attempt on-demand path recovery on the same volume via OpenFileById + GetFinalPathNameByHandleW.
/// Explicitly rejects targets sitting inside the Recycle Bin (returns InRecycleBin).
[[nodiscard]] FileIdRecoveryResult TryRecoverPathByFileId(
    std::string_view encodedFileId,
    const std::filesystem::path& originalPathHint);

/// Return true if currentName matches oldPath's filename or stem (so renamed files auto-update display names,
/// while custom user nicknames remain untouched).
[[nodiscard]] bool ShouldAutoUpdateDisplayName(std::string_view currentName,
                                               const std::filesystem::path& oldPath);

/// Derive the default LaunchItem display name from a local file/folder path.
[[nodiscard]] std::string DeriveDisplayNameFromPath(const std::filesystem::path& newPath,
                                                    ItemKind kind);

} // namespace geekdesk::core
