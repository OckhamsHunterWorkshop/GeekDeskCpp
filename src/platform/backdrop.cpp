#include "platform/backdrop.hpp"

#include <dwmapi.h>

#include <array>

namespace geekdesk::platform {

bool IsSystemDarkTheme() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS st =
        RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (st != ERROR_SUCCESS) {
        return false;
    }
    return value == 0;
}

bool ResolveIsDark(core::ThemeMode mode) {
    switch (mode) {
    case core::ThemeMode::Dark:
        return true;
    case core::ThemeMode::Light:
        return false;
    case core::ThemeMode::System:
        return IsSystemDarkTheme();
    }
    return false;
}

std::filesystem::path GetDesktopWallpaperPath() {
    std::array<wchar_t, MAX_PATH * 2> buf{};
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, static_cast<UINT>(buf.size()), buf.data(), 0) ==
        FALSE) {
        return {};
    }
    buf.back() = L'\0';
    if (buf[0] == L'\0') {
        return {};
    }
    std::filesystem::path p(buf.data());
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || !std::filesystem::is_regular_file(p, ec)) {
        return {};
    }
    return p;
}

bool ApplyWindowBackdrop(HWND hwnd, core::BackdropMode mode, bool isDark, int cornerRadius) {
    if (hwnd == nullptr) {
        return false;
    }
    (void)cornerRadius;

    const BOOL darkFlag = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFlag, sizeof(darkFlag));

    // Disable Win11 DWM border and fixed rounding on the layered MainWindow so Direct2D
    // renders crisp 0..24 px anti-aliased corners identically across Windows 10 and Windows 11.
    const DWORD noRound = static_cast<DWORD>(DWMWCP_DONOTROUND);
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &noRound, sizeof(noRound));

    const COLORREF noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &noBorder, sizeof(noBorder));

    return (mode == core::BackdropMode::Acrylic || mode == core::BackdropMode::Mica ||
            mode == core::BackdropMode::MicaAlt);
}

} // namespace geekdesk::platform
