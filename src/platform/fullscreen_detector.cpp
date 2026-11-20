#include "platform/fullscreen_detector.hpp"

#include <shellapi.h>

#include <string>

namespace geekdesk::platform {

namespace {

bool IsShellDesktopOrTray(HWND hwnd) {
    if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) {
        return true;
    }
    wchar_t cls[64]{};
    const int len = GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    if (len <= 0) {
        return false;
    }
    const std::wstring_view name(cls, static_cast<std::size_t>(len));
    return name == L"Progman" || name == L"WorkerW" || name == L"Shell_TrayWnd" ||
           name == L"Shell_SecondaryTrayWnd";
}

} // namespace

bool IsForegroundFullScreen(HWND ownHwnd) {
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        if (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN ||
            state == QUNS_PRESENTATION_MODE) {
            return true;
        }
    }

    const HWND fg = GetForegroundWindow();
    if (fg == nullptr || fg == ownHwnd || IsShellDesktopOrTray(fg) || !IsWindowVisible(fg) ||
        IsIconic(fg)) {
        return false;
    }

    RECT fgRect{};
    if (!GetWindowRect(fg, &fgRect)) {
        return false;
    }

    HMONITOR monitor = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr) {
        return false;
    }
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return false;
    }

    return fgRect.left <= mi.rcMonitor.left && fgRect.top <= mi.rcMonitor.top &&
           fgRect.right >= mi.rcMonitor.right && fgRect.bottom >= mi.rcMonitor.bottom;
}

} // namespace geekdesk::platform
