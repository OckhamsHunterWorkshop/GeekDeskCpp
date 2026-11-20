#pragma once

#include <windows.h>

namespace geekdesk::platform {

/// Return true if another non-shell window appears to occupy fullscreen mode
/// (D3D fullscreen game, presentation mode, or borderless fullscreen window).
[[nodiscard]] bool IsForegroundFullScreen(HWND ownHwnd);

} // namespace geekdesk::platform
