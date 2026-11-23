#pragma once

#include "core/models.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace geekdesk::core {

struct DesktopKnownFolders {
    std::filesystem::path userDesktop;
    std::filesystem::path publicDesktop;
};

enum class ArchiveDirValidation {
    Ok,
    Empty,
    InsideDesktop,
    InsideAppData,
};

/// Return true if candidate equals root or resides strictly beneath root (case-insensitive, segment-safe).
[[nodiscard]] bool IsPathEqualOrUnder(const std::filesystem::path& candidate,
                                        const std::filesystem::path& root);

/// Return true iff itemPath is a direct child of userDesktop or publicDesktop.
[[nodiscard]] bool IsDirectChildOfDesktop(const std::filesystem::path& itemPath,
                                          const DesktopKnownFolders& desktops);

/// Validate that archiveDir does not overlap Desktop or GeekDeskCpp's data directory.
[[nodiscard]] ArchiveDirValidation ValidateArchiveDirectory(const std::filesystem::path& archiveDir,
                                                            const DesktopKnownFolders& desktops,
                                                            const std::filesystem::path& appDataRoot);

/// Detect an ancestor OneDrive folder root in a filesystem path, if present.
[[nodiscard]] std::optional<std::filesystem::path> DetectOneDriveRoot(
    const std::filesystem::path& path);

/// Return true when userDesktop is inside OneDrive while archiveDir is outside that OneDrive root.
[[nodiscard]] bool WouldLeaveOneDrive(const std::filesystem::path& userDesktop,
                                      const std::filesystem::path& archiveDir);

/// Sanitize a group name into a safe single Windows folder name.
[[nodiscard]] std::wstring SanitizeGroupFolderName(std::string_view groupName);

/// Return the archived physical path (lnkPath or targetPath) inside archiveRoot, if any.
[[nodiscard]] std::optional<std::filesystem::path> FindArchivedPhysicalPath(
    const LaunchItem& item,
    const std::filesystem::path& archiveRoot);

} // namespace geekdesk::core
