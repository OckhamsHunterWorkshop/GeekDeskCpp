#pragma once

#include "core/models.hpp"

#include <string_view>

namespace geekdesk::ui {

struct ColorRgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr bool operator==(const ColorRgba& other) const noexcept = default;
};

struct ThemePalette {
    ColorRgba windowTint;
    ColorRgba sidebarBg;
    ColorRgba headerButtonBg;
    ColorRgba headerButtonHoverBg;
    ColorRgba contentCardBg;
    ColorRgba tileBg;
    ColorRgba tileHoverBg;
    ColorRgba tileSelectedBg;
    ColorRgba searchBoxBg;
    ColorRgba searchBoxBorder;
    ColorRgba searchBoxFocusBorder;
    ColorRgba accent;
    ColorRgba textPrimary;
    ColorRgba textSecondary;
    ColorRgba divider;
    ColorRgba badgeBg;
    ColorRgba badgeText;
    ColorRgba gradientStart;
    ColorRgba gradientEnd;
    bool isDark = true;
    bool useNativeBackdrop = true;
};

/// Parse #RGB, #RRGGBB, or #AARRGGBB hex strings into normalized 0..1 RGBA values.
[[nodiscard]] ColorRgba ParseHexColor(std::string_view hex, ColorRgba fallback = {});

/// Compute concrete Direct2D palette colors from AppearanceSettings, resolved theme polarity,
/// and whether DWM native system backdrop (Acrylic/Mica/MicaAlt) is active.
[[nodiscard]] ThemePalette BuildThemePalette(const core::AppearanceSettings& app,
                                              bool isDark,
                                              bool nativeBackdropActive);

} // namespace geekdesk::ui
