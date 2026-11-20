#pragma once

#include <windows.h>

#include <optional>
#include <string_view>

namespace geekdesk::platform {

enum class TrayCommand : UINT {
    ToggleWindow = 1001,
    OpenSettings = 1002,
    SyncCatalog = 1003,
    OpenDataDir = 1004,
    ToggleAutoStart = 1005,
    Exit = 1006,
};

/// System tray notification icon manager (Shell_NotifyIconW) with TaskbarCreated recovery.
class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Show(HWND hwnd, UINT callbackMsg, std::wstring_view tooltip = L"GeekDeskCpp");
    void Hide();
    bool Recreate();

    [[nodiscard]] bool IsVisible() const noexcept {
        return added_;
    }

    [[nodiscard]] UINT TaskbarCreatedMessage() const noexcept {
        return taskbarCreatedMsg_;
    }

    /// Show native context menu at cursor position; returns selected command if clicked.
    [[nodiscard]] std::optional<TrayCommand> ShowContextMenu(HWND hwnd,
                                                             bool autoStartChecked,
                                                             bool englishUi = false) const;

private:
    HWND hwnd_ = nullptr;
    UINT callbackMsg_ = 0;
    UINT taskbarCreatedMsg_ = 0;
    HICON icon_ = nullptr;
    bool added_ = false;
    bool ownedIcon_ = false;
    wchar_t tooltip_[128]{};
};

} // namespace geekdesk::platform
