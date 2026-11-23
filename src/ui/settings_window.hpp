#pragma once

#include "core/models.hpp"

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

namespace geekdesk::ui {

struct SettingsCallbacks {
    std::function<void(const core::AppSettings&)> onSettingsChanged;
    std::function<void(const core::AppSettings&, const core::CatalogDocument&, const std::filesystem::path&)>
        onBackupRestored;
    std::function<std::pair<std::size_t, std::size_t>()> onQueryStats;
    std::function<std::string()> onQueryDiagnostics;
    std::function<void()> onClosed;
};

/// Native Win32 3-Tab Settings & Backup/Restore Center owned by MainWindow.
class SettingsWindow {
public:
    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    void OpenOrActivate(HINSTANCE hInstance,
                        HWND ownerHwnd,
                        std::filesystem::path dataRoot,
                        const core::AppSettings& currentSettings,
                        SettingsCallbacks callbacks);

    void Close();

    void SyncFromExternalSettings(const core::AppSettings& settings);

    [[nodiscard]] bool IsOpen() const noexcept {
        return hwnd_ != nullptr;
    }

    [[nodiscard]] HWND Handle() const noexcept {
        return hwnd_;
    }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void BuildControls();
    void RebuildLanguageUi();
    void SwitchTab(int tabIndex);
    void PopulateFromSettings();
    void RefreshBackupListAndStats();
    void RefreshHotKeyHint();
    void RefreshSliderLabels();
    void EmitSettingsChanged();
    void PerformRestoreFromPath(const std::filesystem::path& bundlePath);
    void RebuildThemeBrushes();
    void ApplyChildThemes();

    enum ControlId : int {
        kIdTab0 = 4000,
        kIdTab1,
        kIdTab2,
        kIdCloseBtn,
        // Appearance
        kIdLanguageCombo,
        kIdThemeCombo,
        kIdBackdropCombo,
        kIdBgOpacitySlider,
        kIdBgOpacityVal,
        kIdCardOpacitySlider,
        kIdCardOpacityVal,
        kIdIconSizeSlider,
        kIdIconSizeVal,
        kIdCornerRadiusSlider,
        kIdCornerRadiusVal,
        kIdFontFamilyCombo,
        kIdFontSizeSlider,
        kIdFontSizeVal,
        kIdShowTitleCheck,
        kIdWallpaperLabel,
        kIdPickWallpaperBtn,
        kIdClearWallpaperBtn,
        // Activation & Behavior
        kIdEnableHotKeyCheck,
        kIdHotKeyEdit,
        kIdHotKeyHint,
        kIdMiddleMouseCheck,
        kIdFollowMouseCheck,
        kIdIgnoreFullscreenCheck,
        kIdHidePolicyCombo,
        kIdDoubleClickCheck,
        kIdHoverGroupCheck,
        kIdSearchActCombo,
        kIdGroupSortCombo,
        kIdItemSortCombo,
        kIdAutoStartCheck,
        kIdDesktopArchiveLabel,
        kIdPickDesktopArchiveBtn,
        kIdClearDesktopArchiveBtn,
        // Data & Backup/Restore
        kIdStatsLabel,
        kIdRetentionEdit,
        kIdApplyRetentionBtn,
        kIdExportBackupBtn,
        kIdImportBackupBtn,
        kIdBackupList,
        kIdRestoreSelectedBtn,
        kIdOpenDataDirBtn,
        kIdOpenBackupsDirBtn,
        kIdOpenLogsDirBtn,
        kIdDiagnosticsBtn,
    };

    HWND hwnd_ = nullptr;
    HWND ownerHwnd_ = nullptr;
    HWND tabButtons_[3]{};
    HFONT font_ = nullptr;
    HBRUSH bgBrush_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    COLORREF bgColor_ = RGB(246, 247, 250);
    COLORREF editColor_ = RGB(255, 255, 255);
    COLORREF textColor_ = RGB(20, 24, 34);
    bool isDark_ = false;
    bool populating_ = false;
    std::uint32_t dpi_ = 96;
    int activeTab_ = 0;

    std::filesystem::path dataRoot_;
    core::AppSettings working_;
    SettingsCallbacks callbacks_;

    std::vector<HWND> tabPages_[3];
    std::vector<std::filesystem::path> snapshotPaths_;

    // Direct control handles for quick access
    HWND languageCombo_ = nullptr;
    HWND themeCombo_ = nullptr;
    HWND backdropCombo_ = nullptr;
    HWND bgOpacitySlider_ = nullptr;
    HWND bgOpacityVal_ = nullptr;
    HWND cardOpacitySlider_ = nullptr;
    HWND cardOpacityVal_ = nullptr;
    HWND iconSizeSlider_ = nullptr;
    HWND iconSizeVal_ = nullptr;
    HWND cornerSlider_ = nullptr;
    HWND cornerVal_ = nullptr;
    HWND fontFamilyCombo_ = nullptr;
    HWND fontSizeSlider_ = nullptr;
    HWND fontSizeVal_ = nullptr;
    HWND showTitleCheck_ = nullptr;
    HWND wallpaperLabel_ = nullptr;

    HWND enableHotKeyCheck_ = nullptr;
    HWND hotKeyEdit_ = nullptr;
    HWND hotKeyHint_ = nullptr;
    HWND middleMouseCheck_ = nullptr;
    HWND followMouseCheck_ = nullptr;
    HWND ignoreFullscreenCheck_ = nullptr;
    HWND hidePolicyCombo_ = nullptr;
    HWND doubleClickCheck_ = nullptr;
    HWND hoverGroupCheck_ = nullptr;
    HWND searchActCombo_ = nullptr;
    HWND groupSortCombo_ = nullptr;
    HWND itemSortCombo_ = nullptr;
    HWND autoStartCheck_ = nullptr;
    HWND desktopArchiveLabel_ = nullptr;

    HWND statsLabel_ = nullptr;
    HWND retentionEdit_ = nullptr;
    HWND backupList_ = nullptr;
    HWND restoreSelectedBtn_ = nullptr;
};

} // namespace geekdesk::ui
