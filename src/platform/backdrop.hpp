#pragma once

#include "core/models.hpp"

#include <windows.h>

namespace geekdesk::platform {

/// Check Windows Registry whether AppsUseLightTheme == 0 (dark mode).
[[nodiscard]] bool IsSystemDarkTheme();

/// Resolve ThemeMode (System / Light / Dark) into a concrete boolean dark flag.
[[nodiscard]] bool ResolveIsDark(core::ThemeMode mode);

/// Configure DWM dark mode, corner rounding, extended frame, and Windows 11 system backdrop.
/// Returns true only if native DWM system backdrop (Acrylic/Mica/MicaAlt) succeeded.
[[nodiscard]] bool ApplyWindowBackdrop(HWND hwnd,
                                       core::BackdropMode mode,
                                       bool isDark,
                                       int cornerRadius);

} // namespace geekdesk::platform
