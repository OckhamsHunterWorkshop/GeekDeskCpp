#pragma once

#include "core/models.hpp"

#include <windows.h>

#include <string_view>

namespace geekdesk::core {

/// Resolve UiLanguage (Auto / ZhCn / EnUs) to whether the UI should display English.
[[nodiscard]] inline bool IsEnglishUi(UiLanguage lang) noexcept {
    if (lang == UiLanguage::EnUs) {
        return true;
    }
    if (lang == UiLanguage::ZhCn) {
        return false;
    }
    const LANGID ui = GetUserDefaultUILanguage();
    return PRIMARYLANGID(ui) != LANG_CHINESE;
}

[[nodiscard]] inline const wchar_t* TrW(bool en,
                                        const wchar_t* zhText,
                                        const wchar_t* enText) noexcept {
    return en ? enText : zhText;
}

[[nodiscard]] inline std::string_view TrU8(bool en,
                                           std::string_view zhText,
                                           std::string_view enText) noexcept {
    return en ? enText : zhText;
}

} // namespace geekdesk::core
