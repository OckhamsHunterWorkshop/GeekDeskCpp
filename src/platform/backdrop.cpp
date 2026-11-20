#include "platform/backdrop.hpp"

#include <dwmapi.h>

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

bool ApplyWindowBackdrop(HWND hwnd, core::BackdropMode mode, bool isDark, int cornerRadius) {
    if (hwnd == nullptr) {
        return false;
    }

    const BOOL darkFlag = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFlag, sizeof(darkFlag));

    const DWORD cornerPref =
        (cornerRadius > 0) ? static_cast<DWORD>(DWMWCP_ROUND) : static_cast<DWORD>(DWMWCP_DONOTROUND);
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

    const bool wantsSystemBackdrop =
        (mode == core::BackdropMode::Acrylic || mode == core::BackdropMode::Mica ||
         mode == core::BackdropMode::MicaAlt);

    const MARGINS margins = wantsSystemBackdrop ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    DWORD backdropType = static_cast<DWORD>(DWMSBT_NONE);
    switch (mode) {
    case core::BackdropMode::Acrylic:
        backdropType = static_cast<DWORD>(DWMSBT_TRANSIENTWINDOW);
        break;
    case core::BackdropMode::Mica:
        backdropType = static_cast<DWORD>(DWMSBT_MAINWINDOW);
        break;
    case core::BackdropMode::MicaAlt:
        backdropType = static_cast<DWORD>(DWMSBT_TABBEDWINDOW);
        break;
    case core::BackdropMode::Wallpaper:
    case core::BackdropMode::Gradient:
        backdropType = static_cast<DWORD>(DWMSBT_NONE);
        break;
    }

    const HRESULT hr =
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
    return wantsSystemBackdrop && SUCCEEDED(hr);
}

} // namespace geekdesk::platform
