#include "platform/fullscreen_detector.hpp"

#include "core/utf8.hpp"

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

bool IsForegroundFullScreen(HWND ownHwnd, std::string* outReason) {
    if (outReason != nullptr) {
        outReason->clear();
    }

    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        // Note: intentionally exclude QUNS_BUSY because Focus Assist / Do Not Disturb / idle
        // states on Windows 11 can report QUNS_BUSY on the normal desktop.
        if (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE) {
            if (outReason != nullptr) {
                *outReason = (state == QUNS_RUNNING_D3D_FULL_SCREEN)
                                 ? "QUNS_RUNNING_D3D_FULL_SCREEN"
                                 : "QUNS_PRESENTATION_MODE";
            }
            return true;
        }
    }

    const HWND fg = GetForegroundWindow();
    if (fg == nullptr || fg == ownHwnd || IsShellDesktopOrTray(fg) || !IsWindowVisible(fg) ||
        IsIconic(fg)) {
        return false;
    }

    const LONG_PTR exStyle = GetWindowLongPtrW(fg, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
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

    const bool coversMonitor =
        fgRect.left <= mi.rcMonitor.left && fgRect.top <= mi.rcMonitor.top &&
        fgRect.right >= mi.rcMonitor.right && fgRect.bottom >= mi.rcMonitor.bottom;
    if (coversMonitor && outReason != nullptr) {
        wchar_t cls[64]{};
        const int len = GetClassNameW(fg, cls, static_cast<int>(std::size(cls)));
        const std::string clsUtf8 =
            (len > 0) ? core::utf8::WideToUtf8(std::wstring_view(cls, static_cast<std::size_t>(len)))
                      : std::string("unknown");
        *outReason = "ForegroundRectCoversMonitor(class=" + clsUtf8 + ")";
    }
    return coversMonitor;
}

} // namespace geekdesk::platform
