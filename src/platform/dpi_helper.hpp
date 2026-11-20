#pragma once

#include "core/placement_math.hpp"

#include <windows.h>

#include <cstdint>

namespace geekdesk::platform {

struct MonitorMetrics {
    core::PixelRect workArea;
    core::PixelRect monitorArea;
    std::uint32_t dpi = 96;
    HMONITOR handle = nullptr;
};

/// Query monitor physical workArea, monitorArea, and effective DPI for the monitor nearest to pt.
[[nodiscard]] MonitorMetrics GetMonitorMetricsForPoint(POINT pt);

/// Query monitor physical metrics for the monitor nearest to the given window.
[[nodiscard]] MonitorMetrics GetMonitorMetricsForWindow(HWND hwnd);

/// Query effective DPI for an HWND (defaults to 96 if unavailable).
[[nodiscard]] std::uint32_t GetWindowDpi(HWND hwnd) noexcept;

} // namespace geekdesk::platform
