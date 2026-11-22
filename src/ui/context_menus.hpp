#pragma once

#include "core/models.hpp"

#include <windows.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace geekdesk::ui {

enum class GroupMenuAction : UINT {
    NewManualGroup = 2001,
    NewLinkedFolderGroup = 2002,
    RenameGroup = 2003,
    MoveGroupUp = 2004,
    MoveGroupDown = 2005,
    SyncLinkedGroup = 2006,
    DeleteGroup = 2007,
};

enum class ItemMenuAction : UINT {
    OpenItem = 3001,
    RunAsAdminOnce = 3002,
    OpenLocation = 3003,
    EditItem = 3004,
    ToggleRunAsAdmin = 3005,
    DeleteItem = 3006,
    AddFile = 3010,
    AddFolder = 3011,
    AddCustom = 3012,
    SortCustom = 3020,
    SortCountDesc = 3021,
    SortNameAsc = 3022,
};

struct PopupMenuItem {
    UINT id = 0;
    std::wstring label;
    bool separator = false;
    bool enabled = true;
    bool checked = false;
};

/// Show a unified Direct2D Software layered rounded popup menu matching the main window theme.
[[nodiscard]] std::optional<UINT> ShowThemedPopupMenu(HWND owner,
                                                      POINT screenPt,
                                                      bool isDark,
                                                      const std::vector<PopupMenuItem>& items);

[[nodiscard]] std::optional<GroupMenuAction> ShowGroupContextMenu(HWND owner,
                                                                  POINT screenPt,
                                                                  bool isDark,
                                                                  bool clickedOnGroup,
                                                                  bool isLinkedFolder,
                                                                  std::size_t groupIndex,
                                                                  std::size_t groupCount,
                                                                  bool englishUi = false);

[[nodiscard]] std::optional<ItemMenuAction> ShowItemContextMenu(HWND owner,
                                                                POINT screenPt,
                                                                bool isDark,
                                                                bool clickedOnItem,
                                                                bool isManualGroup,
                                                                bool runAsAdmin,
                                                                core::SortOrder currentItemSort,
                                                                bool englishUi = false);

} // namespace geekdesk::ui
