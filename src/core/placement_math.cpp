#include "core/placement_math.hpp"

#include <algorithm>
#include <cmath>

namespace geekdesk::core {

int DipToPhysicalPx(double dip, std::uint32_t dpi) noexcept {
    if (dpi == 0) {
        dpi = 96;
    }
    const double scaled = dip * (static_cast<double>(dpi) / 96.0);
    return static_cast<int>(std::lround(scaled));
}

PixelPoint ComputeTopLeftClampedPosition(PixelPoint cursorPx,
                                         int windowWidthPx,
                                         int windowHeightPx,
                                         const PixelRect& workAreaPx) noexcept {
    const int safeWidth = std::max(0, windowWidthPx);
    const int safeHeight = std::max(0, windowHeightPx);

    const std::int32_t maxLeft =
        std::max(workAreaPx.left, workAreaPx.right - static_cast<std::int32_t>(safeWidth));
    const std::int32_t maxTop =
        std::max(workAreaPx.top, workAreaPx.bottom - static_cast<std::int32_t>(safeHeight));

    return PixelPoint{
        std::clamp(cursorPx.x, workAreaPx.left, maxLeft),
        std::clamp(cursorPx.y, workAreaPx.top, maxTop),
    };
}

PixelPoint ComputeCenteredPosition(int windowWidthPx,
                                   int windowHeightPx,
                                   const PixelRect& workAreaPx) noexcept {
    const int safeWidth = std::max(0, windowWidthPx);
    const int safeHeight = std::max(0, windowHeightPx);

    const std::int32_t rawX = workAreaPx.left + (workAreaPx.width() - safeWidth) / 2;
    const std::int32_t rawY = workAreaPx.top + (workAreaPx.height() - safeHeight) / 2;

    const std::int32_t maxLeft =
        std::max(workAreaPx.left, workAreaPx.right - static_cast<std::int32_t>(safeWidth));
    const std::int32_t maxTop =
        std::max(workAreaPx.top, workAreaPx.bottom - static_cast<std::int32_t>(safeHeight));

    return PixelPoint{
        std::clamp(rawX, workAreaPx.left, maxLeft),
        std::clamp(rawY, workAreaPx.top, maxTop),
    };
}

int ComputeGridColumns(int contentWidthPx,
                       int tileWidthPx,
                       int gapPx,
                       int minColumns) noexcept {
    if (tileWidthPx <= 0 || contentWidthPx <= 0) {
        return std::max(1, minColumns);
    }
    const int safeGap = std::max(0, gapPx);
    const int denom = tileWidthPx + safeGap;
    const int cols = (contentWidthPx + safeGap) / std::max(1, denom);
    return std::max(std::max(1, minColumns), cols);
}

} // namespace geekdesk::core
