#include "ui/theme_palette.hpp"

#include "core/utf8.hpp"

#include <algorithm>

namespace geekdesk::ui {

namespace {

int HexVal(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

int ParseByte(char hi, char lo) noexcept {
    const int h = HexVal(hi);
    const int l = HexVal(lo);
    if (h < 0 || l < 0) {
        return -1;
    }
    return (h << 4) | l;
}

constexpr ColorRgba RgbaByte(int r, int g, int b, float a = 1.0f) noexcept {
    return ColorRgba{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                     static_cast<float>(b) / 255.0f, a};
}

} // namespace

ColorRgba ParseHexColor(std::string_view hex, ColorRgba fallback) {
    hex = core::utf8::TrimAscii(hex);
    if (!hex.empty() && hex.front() == '#') {
        hex.remove_prefix(1);
    }
    if (hex.size() == 3) {
        const int r = HexVal(hex[0]);
        const int g = HexVal(hex[1]);
        const int b = HexVal(hex[2]);
        if (r < 0 || g < 0 || b < 0) {
            return fallback;
        }
        return RgbaByte(r * 17, g * 17, b * 17, 1.0f);
    }
    if (hex.size() == 6) {
        const int r = ParseByte(hex[0], hex[1]);
        const int g = ParseByte(hex[2], hex[3]);
        const int b = ParseByte(hex[4], hex[5]);
        if (r < 0 || g < 0 || b < 0) {
            return fallback;
        }
        return RgbaByte(r, g, b, 1.0f);
    }
    if (hex.size() == 8) {
        const int a = ParseByte(hex[0], hex[1]);
        const int r = ParseByte(hex[2], hex[3]);
        const int g = ParseByte(hex[4], hex[5]);
        const int b = ParseByte(hex[6], hex[7]);
        if (a < 0 || r < 0 || g < 0 || b < 0) {
            return fallback;
        }
        return RgbaByte(r, g, b, static_cast<float>(a) / 255.0f);
    }
    return fallback;
}

ThemePalette BuildThemePalette(const core::AppearanceSettings& app,
                               bool isDark,
                               bool nativeBackdropActive) {
    ThemePalette p;
    p.isDark = isDark;

    const bool isGlassMode =
        (app.backdrop == core::BackdropMode::Acrylic || app.backdrop == core::BackdropMode::Mica ||
         app.backdrop == core::BackdropMode::MicaAlt);
    p.useNativeBackdrop = isGlassMode && nativeBackdropActive;

    const float bgOpacity = static_cast<float>(std::clamp(app.backgroundOpacity, 0.0, 1.0));
    const float cardOpacity = static_cast<float>(std::clamp(app.cardOpacity, 0.0, 1.0));

    // When DWM Acrylic/Mica/MicaAlt is active, map backgroundOpacity into a clearly visible
    // tint alpha [0.12, 0.92] so dragging opacity visibly changes transparency.
    // On fallback or Gradient/Wallpaper mode, window base is opaque (1.0f).
    const float tintAlpha = p.useNativeBackdrop ? (0.12f + 0.80f * bgOpacity) : 1.0f;

    if (isDark) {
        p.windowTint = RgbaByte(20, 22, 28, tintAlpha);
        p.sidebarBg = RgbaByte(16, 18, 24, p.useNativeBackdrop ? (0.18f + 0.40f * bgOpacity) : 0.94f);
        p.headerButtonBg = RgbaByte(255, 255, 255, 0.06f);
        p.headerButtonHoverBg = RgbaByte(255, 255, 255, 0.14f);
        // Opaque high-contrast dark card base multiplied by cardOpacity so changes stand out.
        p.contentCardBg = RgbaByte(30, 32, 40, std::clamp(cardOpacity, 0.03f, 0.95f));
        p.tileBg = RgbaByte(255, 255, 255, 0.00f);
        p.tileHoverBg = RgbaByte(255, 255, 255, 0.11f);
        p.tileSelectedBg = RgbaByte(59, 130, 246, 0.28f);
        p.searchBoxBg = RgbaByte(14, 16, 22, 0.62f);
        p.searchBoxBorder = RgbaByte(255, 255, 255, 0.14f);
        p.searchBoxFocusBorder = RgbaByte(96, 165, 250, 0.90f);
        p.accent = RgbaByte(59, 130, 246, 1.0f);
        p.textPrimary = RgbaByte(244, 245, 248, 1.0f);
        p.textSecondary = RgbaByte(164, 170, 184, 0.95f);
        p.divider = RgbaByte(255, 255, 255, 0.08f);
        p.badgeBg = RgbaByte(245, 158, 11, 0.92f);
        p.badgeText = RgbaByte(15, 23, 42, 1.0f);
    } else {
        p.windowTint = RgbaByte(244, 246, 250, tintAlpha);
        p.sidebarBg = RgbaByte(234, 238, 245, p.useNativeBackdrop ? (0.20f + 0.45f * bgOpacity) : 0.96f);
        p.headerButtonBg = RgbaByte(15, 23, 42, 0.05f);
        p.headerButtonHoverBg = RgbaByte(15, 23, 42, 0.12f);
        // Opaque white card base multiplied by cardOpacity.
        p.contentCardBg = RgbaByte(255, 255, 255, std::clamp(cardOpacity, 0.04f, 0.96f));
        p.tileBg = RgbaByte(15, 23, 42, 0.00f);
        p.tileHoverBg = RgbaByte(15, 23, 42, 0.07f);
        p.tileSelectedBg = RgbaByte(37, 99, 235, 0.18f);
        p.searchBoxBg = RgbaByte(255, 255, 255, 0.78f);
        p.searchBoxBorder = RgbaByte(15, 23, 42, 0.14f);
        p.searchBoxFocusBorder = RgbaByte(37, 99, 235, 0.90f);
        p.accent = RgbaByte(37, 99, 235, 1.0f);
        p.textPrimary = RgbaByte(18, 24, 38, 1.0f);
        p.textSecondary = RgbaByte(88, 98, 118, 0.95f);
        p.divider = RgbaByte(15, 23, 42, 0.09f);
        p.badgeBg = RgbaByte(217, 119, 6, 0.95f);
        p.badgeText = RgbaByte(255, 255, 255, 1.0f);
    }

    // Apply custom user textColor unless it is the stock white default while running Light theme.
    const std::string textUpper = core::utf8::ToLowerAscii(core::utf8::TrimAscii(app.textColor));
    if (!textUpper.empty() && !(!isDark && (textUpper == "#ffffff" || textUpper == "#fff"))) {
        p.textPrimary = ParseHexColor(app.textColor, p.textPrimary);
    }

    p.gradientStart = ParseHexColor(app.gradientColor1, RgbaByte(37, 99, 235, 1.0f));
    p.gradientEnd = ParseHexColor(app.gradientColor2, RgbaByte(15, 23, 42, 1.0f));
    return p;
}

} // namespace geekdesk::ui
