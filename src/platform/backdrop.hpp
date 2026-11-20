#pragma once

#include "core/models.hpp"

#include <windows.h>

#include <filesystem>

namespace geekdesk::platform {

/// Check Windows Registry whether AppsUseLightTheme == 0 (dark mode).
[[nodiscard]] bool IsSystemDarkTheme();

/// Resolve ThemeMode (System / Light / Dark) into a concrete boolean dark flag.
[[nodiscard]] bool ResolveIsDark(core::ThemeMode mode);

/// Query the current user desktop wallpaper file path (if available on disk).
[[nodiscard]] std::filesystem::path GetDesktopWallpaperPath();

/// Configure DWM dark mode and disable conflicting DWM window frame/corner decorations so
/// layered Direct2D compositing handles 0..24 px rounded corners consistently on Win10 & Win11.
/// Returns true when a translucent/tinted glass backdrop mode (Acrylic/Mica/MicaAlt) is requested.
[[nodiscard]] bool ApplyWindowBackdrop(HWND hwnd,
                                       core::BackdropMode mode,
                                       bool isDark,
                                       int cornerRadius);

} // namespace geekdesk::platform
