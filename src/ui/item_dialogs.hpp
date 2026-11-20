#pragma once

#include "core/models.hpp"

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace geekdesk::ui {

/// Show a concise modal text input dialog (for creating or renaming a group).
[[nodiscard]] std::optional<std::string> ShowTextInputDialog(HWND owner,
                                                             bool isDark,
                                                             std::wstring_view title,
                                                             std::wstring_view promptLabel,
                                                             std::string_view initialUtf8,
                                                             bool englishUi = false);

/// Show a modal property editor for creating or editing a LaunchItem.
/// Returns true if the user confirmed changes.
[[nodiscard]] bool ShowItemEditDialog(HWND owner,
                                      bool isDark,
                                      std::wstring_view title,
                                      core::LaunchItem& inOutItem,
                                      bool englishUi = false);

} // namespace geekdesk::ui
