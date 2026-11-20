#include "platform/tray_icon.hpp"

#include "resource.h"

#include <shellapi.h>

#include <algorithm>
#include <cstring>

namespace geekdesk::platform {

TrayIcon::~TrayIcon() {
    Hide();
    if (ownedIcon_ && icon_ != nullptr) {
        DestroyIcon(icon_);
        icon_ = nullptr;
        ownedIcon_ = false;
    }
}

bool TrayIcon::Show(HWND hwnd, UINT callbackMsg, std::wstring_view tooltip) {
    if (hwnd == nullptr || callbackMsg == 0) {
        return false;
    }
    hwnd_ = hwnd;
    callbackMsg_ = callbackMsg;
    if (taskbarCreatedMsg_ == 0) {
        taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    }

    const std::size_t copyCount = std::min<std::size_t>(tooltip.size(), std::size(tooltip_) - 1);
    std::wmemset(tooltip_, L'\0', std::size(tooltip_));
    if (copyCount > 0) {
        std::memcpy(tooltip_, tooltip.data(), copyCount * sizeof(wchar_t));
    }

    if (icon_ == nullptr) {
        icon_ = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                              MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                              GetSystemMetrics(SM_CXSMICON),
                                              GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        if (icon_ != nullptr) {
            ownedIcon_ = true;
        } else {
            icon_ = LoadIconW(nullptr, IDI_APPLICATION);
        }
    }

    if (added_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_TIP | NIF_ICON;
        nid.hIcon = icon_;
        std::memcpy(nid.szTip, tooltip_, sizeof(tooltip_));
        return Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = callbackMsg_;
    nid.hIcon = icon_;
    std::memcpy(nid.szTip, tooltip_, sizeof(tooltip_));

    added_ = (Shell_NotifyIconW(NIM_ADD, &nid) != FALSE);
    return added_;
}

void TrayIcon::Hide() {
    if (added_ && hwnd_ != nullptr) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        added_ = false;
    }
}

bool TrayIcon::Recreate() {
    if (hwnd_ == nullptr || callbackMsg_ == 0) {
        return false;
    }
    added_ = false;
    return Show(hwnd_, callbackMsg_, tooltip_);
}

std::optional<TrayCommand> TrayIcon::ShowContextMenu(HWND hwnd, bool autoStartChecked) const {
    if (hwnd == nullptr) {
        return std::nullopt;
    }
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return std::nullopt;
    }

    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::ToggleWindow),
                L"\u663E\u793A / \u9690\u85CF\u4E3B\u9762\u677F");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::OpenSettings),
                L"\u8BBE\u7F6E\u4E0E\u5907\u4EFD\u6062\u590D...");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::SyncCatalog),
                L"\u5237\u65B0\u5E76\u540C\u6B65\u5173\u8054\u6587\u4EF6\u5939");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::OpenDataDir),
                L"\u6253\u5F00\u6570\u636E\u76EE\u5F55");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu,
                MF_STRING | (autoStartChecked ? MF_CHECKED : MF_UNCHECKED),
                static_cast<UINT_PTR>(TrayCommand::ToggleAutoStart),
                L"\u5F00\u673A\u81EA\u52A8\u542F\u52A8");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::Exit), L"\u9000\u51FA GeekDeskCpp");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd);
    const UINT cmd = TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd == 0) {
        return std::nullopt;
    }
    return static_cast<TrayCommand>(cmd);
}

} // namespace geekdesk::platform
