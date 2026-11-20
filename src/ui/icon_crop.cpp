#include "ui/icon_crop.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace geekdesk::ui {

namespace {

constexpr std::uint8_t kAlphaThreshold = 8;

} // namespace

bool HasTransparentMoat(const BgraImage& image, int ringOffset) noexcept {
    if (!image.IsValid() || ringOffset < 1 || image.width <= (ringOffset * 2) + 8 ||
        image.height <= (ringOffset * 2) + 8) {
        return false;
    }

    const int stride = image.width * 4;
    const int topRowOffset = ringOffset * stride;
    const int bottomRowOffset = (image.height - 1 - ringOffset) * stride;
    for (int x = ringOffset; x < image.width - ringOffset; ++x) {
        if (image.pixels[static_cast<std::size_t>(topRowOffset + x * 4 + 3)] > kAlphaThreshold ||
            image.pixels[static_cast<std::size_t>(bottomRowOffset + x * 4 + 3)] > kAlphaThreshold) {
            return false;
        }
    }

    const int leftByteOffset = ringOffset * 4;
    const int rightByteOffset = (image.width - 1 - ringOffset) * 4;
    for (int y = ringOffset; y < image.height - ringOffset; ++y) {
        const int rowOffset = y * stride;
        if (image.pixels[static_cast<std::size_t>(rowOffset + leftByteOffset + 3)] > kAlphaThreshold ||
            image.pixels[static_cast<std::size_t>(rowOffset + rightByteOffset + 3)] > kAlphaThreshold) {
            return false;
        }
    }

    return true;
}

BgraImage TrimIconPadding(const BgraImage& source) {
    if (!source.IsValid() || source.width < 16 || source.height < 16) {
        return source;
    }

    const int width = source.width;
    const int height = source.height;
    const int stride = width * 4;
    const int scanMargin = HasTransparentMoat(source, 6) ? 6 : 0;

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;

    for (int y = scanMargin; y < height - scanMargin; ++y) {
        const int rowOffset = y * stride;
        for (int x = scanMargin; x < width - scanMargin; ++x) {
            if (source.pixels[static_cast<std::size_t>(rowOffset + x * 4 + 3)] > kAlphaThreshold) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        return source;
    }

    const int contentW = maxX - minX + 1;
    const int contentH = maxY - minY + 1;

    // Keep full-bleed icons untouched when there is no Shell rim moat.
    if (scanMargin == 0 && contentW >= static_cast<int>(width * 0.84) &&
        contentH >= static_cast<int>(height * 0.84)) {
        return source;
    }

    const int side = std::max(contentW, contentH);
    const int margin = std::max(1, static_cast<int>(std::lround(side * 0.04)));
    const int boxSide = std::min(std::min(width, height), side + margin * 2);

    const double centerX = (minX + maxX) / 2.0;
    const double centerY = (minY + maxY) / 2.0;
    const int cropX = std::clamp(static_cast<int>(std::lround(centerX - boxSide / 2.0)), 0,
                                 std::max(0, width - boxSide));
    const int cropY = std::clamp(static_cast<int>(std::lround(centerY - boxSide / 2.0)), 0,
                                 std::max(0, height - boxSide));
    const int cropW = std::min(boxSide, width - cropX);
    const int cropH = std::min(boxSide, height - cropY);

    if (cropW <= 0 || cropH <= 0 || (cropW == width && cropH == height)) {
        return source;
    }

    BgraImage cropped;
    cropped.width = cropW;
    cropped.height = cropH;
    cropped.pixels.resize(static_cast<std::size_t>(cropW) * static_cast<std::size_t>(cropH) * 4u);

    const int dstStride = cropW * 4;
    for (int row = 0; row < cropH; ++row) {
        const auto srcOffset = static_cast<std::size_t>((cropY + row) * stride + cropX * 4);
        const auto dstOffset = static_cast<std::size_t>(row * dstStride);
        std::memcpy(cropped.pixels.data() + dstOffset, source.pixels.data() + srcOffset,
                    static_cast<std::size_t>(dstStride));
    }
    return cropped;
}

} // namespace geekdesk::ui
