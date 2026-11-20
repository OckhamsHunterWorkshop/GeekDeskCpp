#include "ui/context_menus.hpp"

namespace geekdesk::ui {

namespace {

UINT ShowPopupAndReturn(HWND owner, HMENU menu, POINT screenPt) {
    SetForegroundWindow(owner);
    const UINT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                      screenPt.x, screenPt.y, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    return cmd;
}

} // namespace

std::optional<GroupMenuAction> ShowGroupContextMenu(HWND owner,
                                                    POINT screenPt,
                                                    bool clickedOnGroup,
                                                    bool isLinkedFolder,
                                                    std::size_t groupIndex,
                                                    std::size_t groupCount) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return std::nullopt;
    }

    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(GroupMenuAction::NewManualGroup),
                L"\u65B0\u5EFA\u666E\u901A\u5206\u7EC4...");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(GroupMenuAction::NewLinkedFolderGroup),
                L"\u65B0\u5EFA\u5173\u8054\u6587\u4EF6\u5939\u5206\u7EC4...");

    if (clickedOnGroup) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(GroupMenuAction::RenameGroup),
                    L"\u91CD\u547D\u540D\u5206\u7EC4...");
        if (isLinkedFolder) {
            AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(GroupMenuAction::SyncLinkedGroup),
                        L"\u7ACB\u5373\u540C\u6B65\u8BE5\u6587\u4EF6\u5939");
        }
        AppendMenuW(menu,
                    MF_STRING | (groupIndex == 0 ? MF_GRAYED : 0),
                    static_cast<UINT_PTR>(GroupMenuAction::MoveGroupUp), L"\u4E0A\u79FB\u5206\u7EC4");
        AppendMenuW(menu,
                    MF_STRING | (groupIndex + 1 >= groupCount ? MF_GRAYED : 0),
                    static_cast<UINT_PTR>(GroupMenuAction::MoveGroupDown), L"\u4E0B\u79FB\u5206\u7EC4");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(GroupMenuAction::DeleteGroup),
                    L"\u5220\u9664\u5206\u7EC4");
    }

    const UINT cmd = ShowPopupAndReturn(owner, menu, screenPt);
    DestroyMenu(menu);
    if (cmd == 0) {
        return std::nullopt;
    }
    return static_cast<GroupMenuAction>(cmd);
}

std::optional<ItemMenuAction> ShowItemContextMenu(HWND owner,
                                                  POINT screenPt,
                                                  bool clickedOnItem,
                                                  bool isManualGroup,
                                                  bool runAsAdmin,
                                                  core::SortOrder currentItemSort) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return std::nullopt;
    }

    if (clickedOnItem) {
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(ItemMenuAction::OpenItem),
                    L"\u6253\u5F00 / \u542F\u52A8");
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(ItemMenuAction::RunAsAdminOnce),
                    L"\u4EE5\u7BA1\u7406\u5458\u8EAB\u4EFD\u8FD0\u884C");
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(ItemMenuAction::OpenLocation),
                    L"\u6253\u5F00\u6240\u5728\u4F4D\u7F6E");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(ItemMenuAction::EditItem),
                    L"\u7F16\u8F91\u5C5E\u6027...");
        AppendMenuW(menu,
                    MF_STRING | (runAsAdmin ? MF_CHECKED : MF_UNCHECKED),
                    static_cast<UINT_PTR>(ItemMenuAction::ToggleRunAsAdmin),
                    L"\u9ED8\u8BA4\u4EE5\u7BA1\u7406\u5458\u6743\u9650\u542F\u52A8");
        if (isManualGroup) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(ItemMenuAction::DeleteItem),
                        L"\u4ECE\u5206\u7EC4\u79FB\u9664");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    if (isManualGroup) {
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(ItemMenuAction::AddFile),
                    L"\u6DFB\u52A0\u6587\u4EF6 / \u5E94\u7528...");
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(ItemMenuAction::AddFolder),
                    L"\u6DFB\u52A0\u6587\u4EF6\u5939...");
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(ItemMenuAction::AddCustom),
                    L"\u6DFB\u52A0\u7F51\u5740 / \u81EA\u5B9A\u4E49\u6761\u76EE...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    AppendMenuW(menu,
                MF_STRING | (currentItemSort == core::SortOrder::Custom ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(ItemMenuAction::SortCustom), L"\u6392\u5E8F\uFF1A\u81EA\u5B9A\u4E49");
    AppendMenuW(menu,
                MF_STRING | (currentItemSort == core::SortOrder::LaunchCountDesc ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(ItemMenuAction::SortCountDesc),
                L"\u6392\u5E8F\uFF1A\u542F\u52A8\u6B21\u6570\u4F18\u5148");
    AppendMenuW(menu,
                MF_STRING | (currentItemSort == core::SortOrder::NameAsc ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(ItemMenuAction::SortNameAsc), L"\u6392\u5E8F\uFF1A\u6309\u540D\u79F0 A-Z");

    const UINT cmd = ShowPopupAndReturn(owner, menu, screenPt);
    DestroyMenu(menu);
    if (cmd == 0) {
        return std::nullopt;
    }
    return static_cast<ItemMenuAction>(cmd);
}

} // namespace geekdesk::ui
