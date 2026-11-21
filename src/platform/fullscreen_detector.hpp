#pragma once

#include <windows.h>

#include <string>

namespace geekdesk::platform {

/// Return true if another non-shell window appears to occupy fullscreen mode
/// (D3D fullscreen game, presentation mode, or borderless fullscreen window).
[[nodiscard]] bool IsForegroundFullScreen(HWND ownHwnd, std::string* outReason = nullptr);

} // namespace geekdesk::platform
