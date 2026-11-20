#include "platform/dpi_helper.hpp"

#include <shellscalingapi.h>

namespace geekdesk::platform {

namespace {

MonitorMetrics MetricsFromMonitor(HMONITOR monitor) {
    MonitorMetrics m;
    m.handle = monitor;
    if (monitor != nullptr) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(monitor, &mi)) {
            m.workArea = {mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom};
            m.monitorArea = {mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right,
                             mi.rcMonitor.bottom};
        }
        UINT dpiX = 96;
        UINT dpiY = 96;
        if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0) {
            m.dpi = dpiX;
        }
    }
    if (m.workArea.width() <= 0 || m.workArea.height() <= 0) {
        const int w = GetSystemMetrics(SM_CXSCREEN);
        const int h = GetSystemMetrics(SM_CYSCREEN);
        m.workArea = {0, 0, w, h};
        m.monitorArea = {0, 0, w, h};
    }
    return m;
}

} // namespace

MonitorMetrics GetMonitorMetricsForPoint(POINT pt) {
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    return MetricsFromMonitor(monitor);
}

MonitorMetrics GetMonitorMetricsForWindow(HWND hwnd) {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    return MetricsFromMonitor(monitor);
}

std::uint32_t GetWindowDpi(HWND hwnd) noexcept {
    if (hwnd != nullptr) {
        const UINT dpi = GetDpiForWindow(hwnd);
        if (dpi > 0) {
            return dpi;
        }
    }
    return 96;
}

} // namespace geekdesk::platform
