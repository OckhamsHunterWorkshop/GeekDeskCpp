#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace geekdesk::platform {

struct ResolvedShortcut {
    bool ok = false;
    std::string targetPath;
    std::string arguments;
    std::string workingDirectory;
    std::string iconPath;
    int iconIndex = 0;
    bool isUrl = false;
};

/// Resolve a Windows .lnk shortcut using IShellLinkW + IPersistFile, or a .url InternetShortcut.
[[nodiscard]] ResolvedShortcut ResolveShortcutFile(const std::filesystem::path& shortcutPath);

} // namespace geekdesk::platform
