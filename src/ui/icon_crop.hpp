#pragma once

#include <cstdint>
#include <vector>

namespace geekdesk::ui {

struct BgraImage {
    int width = 0;
    int height = 0;
    /// Packed top-down 32bpp BGRA pixel buffer (stride = width * 4).
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool IsValid() const noexcept {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    }
};

/// Detect whether the image has a continuous transparent perimeter ring ("moat") inside an
/// optional Windows Shell outer card border (typically ~1..5px rim with moat at ringOffset=6).
[[nodiscard]] bool HasTransparentMoat(const BgraImage& image, int ringOffset = 6) noexcept;

/// Trim faint Windows Shell card rims and excess transparent padding around the icon artwork,
/// returning a centered square BGRA image preserving ~4% breathing margin.
[[nodiscard]] BgraImage TrimIconPadding(const BgraImage& source);

} // namespace geekdesk::ui
