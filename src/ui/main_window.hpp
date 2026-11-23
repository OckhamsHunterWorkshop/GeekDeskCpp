#pragma once

#include "core/models.hpp"
#include "core/search_engine.hpp"
#include "platform/com_ptr.hpp"
#include "platform/folder_watcher.hpp"
#include "platform/hotkey_manager.hpp"
#include "platform/mouse_hook.hpp"
#include "platform/tray_icon.hpp"
#include "storage/atomic_writer.hpp"
#include "storage/logger.hpp"
#include "ui/d2d_renderer.hpp"
#include "ui/drag_drop.hpp"
#include "ui/icon_cache.hpp"
#include "ui/settings_window.hpp"
#include "ui/theme_palette.hpp"

#include <windows.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace geekdesk::ui {

struct SoakReport {
    bool ok = false;
    int cycles = 0;
    std::uint32_t gdiStart = 0;
    std::uint32_t gdiEnd = 0;
    std::uint32_t userStart = 0;
    std::uint32_t userEnd = 0;
    std::uint32_t handlesStart = 0;
    std::uint32_t handlesEnd = 0;
    SIZE_T workingSetStart = 0;
    SIZE_T workingSetEnd = 0;
    SIZE_T privateStart = 0;
    SIZE_T privateEnd = 0;
};

class MainWindow {
public:
    MainWindow(std::filesystem::path dataRoot,
               core::AppSettings settings,
               core::CatalogDocument catalog,
               storage::FileLogger* logger,
               UINT singleInstanceMsg = 0);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    /// Create the Win32 window, initialize Direct2D Software renderer, IconCache,
    /// global hotkey, low-level middle mouse hook, and tray icon.
    bool Create(HINSTANCE hInstance, bool registerSystemHooks = true);

    /// Show window at cursor-aligned or centered coordinates, or hide it.
    void ShowAndPosition();
    void HideWindow();
    void ToggleWindow();

    /// Trigger one synchronous render cycle (used by --smoke-ui verification).
    bool RenderSmokeFrame();

    /// Run an automated UI/D2D/Settings soak stability stress pass and capture resource deltas.
    [[nodiscard]] SoakReport RunSoakCycles(int cycles);

    void Destroy();

    [[nodiscard]] HWND Handle() const noexcept {
        return hwnd_;
    }

    [[nodiscard]] std::size_t DisplayedItemCount() const noexcept {
        return displayedItems_.size();
    }

    [[nodiscard]] HWND SettingsHandle() const noexcept {
        return settingsWindow_.Handle();
    }

private:
    enum class DragKind { None, Tile, Group };

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool PresentLayeredFrame();
    void RequestRender();
    void OnResizeOrDpiChanged();
    void OnMouseMove(int xPx, int yPx);
    void OnMouseLeave();
    void OnLeftButtonDown(int xPx, int yPx);
    void OnLeftButtonUp(int xPx, int yPx);
    void OnLeftButtonDoubleClick(int xPx, int yPx);
    void OnRightButtonUp(int xPx, int yPx);
    void OnMouseWheel(int xScreenPx, int yScreenPx, short wheelDelta);
    void OnKeyDown(WPARAM vk);
    void OnChar(wchar_t ch);
    void OnImeStartComposition();
    void OnImeComposition(LPARAM lParam);
    void OnHotKeyTriggered(const char* source);
    void OnTrayIconEvent(LPARAM lParam);
    void OnFolderChangedNotifications();

    void ApplyBackdropAndPalette();
    void RebuildDisplayedItems();
    void SelectGroup(std::size_t index, bool persistSelection);
    void SwitchGroupByDelta(int delta);
    void LaunchDisplayedIndex(std::size_t index);
    void SyncAllLinkedFolders(bool showFeedback);
    void RestartFolderWatchers();
    void OpenSettingsWindow();
    void CreateManualGroupInteractive();
    void CreateLinkedFolderGroupInteractive();
    void ApplySettingsLive(const core::AppSettings& next);
    void ApplyBackupRestored(const core::AppSettings& nextSettings,
                             const core::CatalogDocument& nextCatalog,
                             const std::filesystem::path& rollbackPath);
    void HandleExternalDropHover(POINT screenPt, bool& outAccepted);
    void HandleExternalDropFiles(POINT screenPt, const std::vector<std::filesystem::path>& paths);
    void ProcessDeferredExternalDrops();
    void UpdateImeCandidateWindowPosition();
    void EnsureTileVisible(std::size_t tileIndex);
    void CancelInternalDrag();
    void ScheduleSaveSettings();
    void ScheduleSaveCatalog();
    void ResetDefaultStatus();
    void RefreshHooksAndTray();
    [[nodiscard]] std::string BuildDiagnosticsReport() const;
    void UpdateHeaderTooltip(HeaderButton btn, SidebarButton sbtn = SidebarButton::None);
    bool FindCatalogIndices(const core::LaunchItem* ptr,
                            std::size_t& outGroupIdx,
                            std::size_t& outItemIdx) const noexcept;

    [[nodiscard]] float PxToDip(int px) const noexcept;
    [[nodiscard]] int HitTestGroup(float xDip, float yDip) const noexcept;
    [[nodiscard]] int HitTestTile(float xDip, float yDip) const noexcept;
    [[nodiscard]] HeaderButton HitTestHeaderButton(float xDip, float yDip) const noexcept;
    [[nodiscard]] SidebarButton HitTestSidebarButton(float xDip, float yDip) const noexcept;

    std::filesystem::path dataRoot_;
    core::AppSettings settings_;
    core::CatalogDocument catalog_;
    storage::FileLogger* logger_ = nullptr;
    UINT singleInstanceMsg_ = 0;

    core::SearchEngine searchEngine_;
    storage::DebouncedFileWriter debouncedWriter_;
    platform::HotKeyManager hotKeyManager_;
    platform::MouseHook mouseHook_;
    platform::TrayIcon trayIcon_;
    platform::FolderWatcher folderWatcher_;
    platform::ComPtr<ExternalDropTarget> dropTarget_;
    IconCache iconCache_;
    D2DRenderer renderer_;
    SettingsWindow settingsWindow_;

    std::mutex folderQueueMutex_;
    std::vector<std::string> pendingChangedGroups_;

    ThemePalette palette_;
    LayoutSnapshot layout_;
    std::vector<DisplayedItemView> displayedItems_;

    HWND hwnd_ = nullptr;
    HWND headerTooltip_ = nullptr;
    std::uint32_t dpi_ = 96;
    bool nativeBackdropActive_ = false;
    bool hooksRegistered_ = false;
    bool wtsRegistered_ = false;
    bool dropRegistered_ = false;
    bool trackingMouseLeave_ = false;
    bool inTrayMenu_ = false;
    int modalDepth_ = 0;
    bool suppressSizePersist_ = false;
    bool suppressDpiResize_ = false;
    bool suppressDiskPersist_ = false;
    bool renderPending_ = false;
    int preferredColumns_ = 0;

    std::size_t activeGroupIndex_ = 0;
    std::wstring searchQueryWide_;
    std::wstring imeCompWide_;
    std::size_t searchCaretIndex_ = 0;
    bool searchFocused_ = false;
    bool caretVisible_ = true;
    float scrollOffsetDip_ = 0.0f;
    int hoveredGroupIdx_ = -1;
    int hoveredTileIdx_ = -1;
    int selectedTileIdx_ = -1;
    int dropHighlightGroupIdx_ = -1;
    bool dropHighlightGrid_ = false;
    DragKind dragCandidate_ = DragKind::None;
    bool dragActive_ = false;
    int dragStartIndex_ = -1;
    POINT dragOriginPx_{};
    int dragTargetTileIdx_ = -1;
    int dragTargetGroupIdx_ = -1;
    float dragGroupCursorYDip_ = -1.0f;
    HeaderButton hoveredHeaderBtn_ = HeaderButton::None;
    SidebarButton hoveredSidebarBtn_ = SidebarButton::None;

    struct PendingExternalDrop {
        POINT screenPt{};
        std::size_t targetGroupIdx = 0;
        std::vector<std::filesystem::path> paths;
    };

    std::string hotKeyError_;
    std::string statusText_;
    std::uint64_t lastShownTickMs_ = 0;
    std::deque<PendingExternalDrop> pendingExternalDrops_;
};

} // namespace geekdesk::ui
