#pragma once

#include <cstdint>

namespace geekdesk::core {

struct PixelPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator==(const PixelPoint& other) const noexcept = default;
};

struct PixelRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;

    [[nodiscard]] constexpr std::int32_t width() const noexcept {
        return right - left;
    }
    [[nodiscard]] constexpr std::int32_t height() const noexcept {
        return bottom - top;
    }
    bool operator==(const PixelRect& other) const noexcept = default;
};

/// Convert DIPs to physical pixels at the given DPI (96 DPI = 100% scaling).
[[nodiscard]] int DipToPhysicalPx(double dip, std::uint32_t dpi) noexcept;

/// Compute window top-left physical coordinates aligning cursor to window top-left,
/// clamped cleanly within the target monitor's workArea (multi-DPI safe).
[[nodiscard]] PixelPoint ComputeTopLeftClampedPosition(PixelPoint cursorPx,
                                                       int windowWidthPx,
                                                       int windowHeightPx,
                                                       const PixelRect& workAreaPx) noexcept;

/// Compute centered physical window coordinates inside the target workArea.
[[nodiscard]] PixelPoint ComputeCenteredPosition(int windowWidthPx,
                                                 int windowHeightPx,
                                                 const PixelRect& workAreaPx) noexcept;

/// Compute how many item tiles fit per row for a given content width, tile width, and gap.
[[nodiscard]] int ComputeGridColumns(int contentWidthPx,
                                     int tileWidthPx,
                                     int gapPx,
                                     int minColumns = 1) noexcept;

} // namespace geekdesk::core
