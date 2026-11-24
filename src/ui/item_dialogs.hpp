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

enum class MissingFileAction {
    Cancel,
    Relocate,
    Remove,
};

/// Show a modal property editor for creating or editing a LaunchItem.
/// Returns true if the user confirmed changes.
[[nodiscard]] bool ShowItemEditDialog(HWND owner,
                                      bool isDark,
                                      std::wstring_view title,
                                      core::LaunchItem& inOutItem,
                                      bool englishUi = false);

/// Show a themed guidance dialog when a file/folder item's source path no longer exists
/// (including when it was deleted to the Recycle Bin).
[[nodiscard]] MissingFileAction ShowMissingFileDialog(HWND owner,
                                                      bool isDark,
                                                      std::wstring_view itemName,
                                                      std::wstring_view missingPath,
                                                      bool isManualGroup,
                                                      bool inRecycleBin,
                                                      bool englishUi = false);

} // namespace geekdesk::ui
