#include "ui/main_window.hpp"

#include "core/catalog_mutator.hpp"
#include "core/catalog_sorter.hpp"
#include "core/folder_sync.hpp"
#include "core/placement_math.hpp"
#include "core/utf8.hpp"
#include "platform/autostart.hpp"
#include "platform/backdrop.hpp"
#include "platform/dpi_helper.hpp"
#include "platform/file_dialogs.hpp"
#include "platform/fullscreen_detector.hpp"
#include "platform/shell_launcher.hpp"
#include "platform/shortcut_resolver.hpp"
#include "resource.h"
#include "storage/json_codec.hpp"
#include "ui/context_menus.hpp"
#include "ui/item_dialogs.hpp"

#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <imm.h>
#include <ole2.h>
#include <psapi.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace geekdesk::ui {

namespace {

constexpr const wchar_t* kWindowClassName = L"GeekDeskCppMainWindow";
constexpr UINT kMsgMiddleClick = WM_APP + 1;
constexpr UINT kMsgTrayIcon = WM_APP + 2;
constexpr UINT kMsgIconReady = WM_APP + 3;
constexpr UINT kMsgFolderChanged = WM_APP + 4;

constexpr UINT_PTR kTimerCaret = 1;
constexpr UINT_PTR kTimerHoverGroup = 2;
constexpr UINT_PTR kTimerResumeRecover = 3;

struct ModalGuard {
    int& depth;
    explicit ModalGuard(int& d) noexcept : depth(d) {
        ++depth;
    }
    ~ModalGuard() {
        --depth;
    }
};

core::LaunchItem BuildLaunchItemFromPath(const std::filesystem::path& filePath) {
    core::LaunchItem item;
    item.id = core::utf8::GenerateUuidV4();

    std::error_code ec;
    const bool isDir = std::filesystem::is_directory(filePath, ec);
    const std::string extLower =
        core::utf8::ToLowerAscii(core::utf8::PathToUtf8(filePath.extension()));
    const std::string fullUtf8 = core::utf8::PathToUtf8(filePath);

    if (isDir) {
        item.kind = core::ItemKind::Folder;
        item.name = core::utf8::PathToUtf8(filePath.filename());
        item.targetPath = fullUtf8;
        return item;
    }

    if (extLower == ".lnk" || extLower == ".url") {
        const auto resolved = platform::ResolveShortcutFile(filePath);
        item.name = core::utf8::PathToUtf8(filePath.stem());
        if (resolved.ok) {
            item.targetPath = resolved.targetPath;
            item.arguments = resolved.arguments;
            if (!resolved.workingDirectory.empty()) {
                item.workingDirectory = resolved.workingDirectory;
            }
            item.kind = resolved.isUrl ? core::ItemKind::Url : core::ItemKind::File;
            if (!resolved.isUrl) {
                item.lnkPath = fullUtf8;
            }
        } else {
            item.kind = core::ItemKind::File;
            item.targetPath = fullUtf8;
        }
        return item;
    }

    item.kind = core::ItemKind::File;
    item.targetPath = fullUtf8;
    if (extLower == ".exe") {
        const std::string stem = core::utf8::PathToUtf8(filePath.stem());
        item.name = stem.empty() ? core::utf8::PathToUtf8(filePath.filename()) : stem;
    } else {
        item.name = core::utf8::PathToUtf8(filePath.filename());
    }
    return item;
}

std::string CurrentUtcIsoTimestamp() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

void ForceWindowForeground(HWND hwnd) {
    AllowSetForegroundWindow(ASFW_ANY);
    const HWND fg = GetForegroundWindow();
    if (fg == nullptr || fg == hwnd) {
        SetForegroundWindow(hwnd);
        return;
    }
    const DWORD fgTid = GetWindowThreadProcessId(fg, nullptr);
    const DWORD curTid = GetCurrentThreadId();
    bool attached = false;
    if (fgTid != 0 && fgTid != curTid) {
        attached = (AttachThreadInput(fgTid, curTid, TRUE) != FALSE);
    }
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetFocus(hwnd);
    if (attached) {
        AttachThreadInput(fgTid, curTid, FALSE);
    }
}

std::wstring ReadClipboardText(HWND hwnd) {
    if (!OpenClipboard(hwnd)) {
        return {};
    }
    std::wstring result;
    if (HANDLE hData = GetClipboardData(CF_UNICODETEXT); hData != nullptr) {
        if (const auto* raw = static_cast<const wchar_t*>(GlobalLock(hData)); raw != nullptr) {
            result = raw;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}

} // namespace

MainWindow::MainWindow(std::filesystem::path dataRoot,
                       core::AppSettings settings,
                       core::CatalogDocument catalog,
                       storage::FileLogger* logger,
                       UINT singleInstanceMsg)
    : dataRoot_(std::move(dataRoot)),
      settings_(std::move(settings)),
      catalog_(std::move(catalog)),
      logger_(logger),
      singleInstanceMsg_(singleInstanceMsg),
      debouncedWriter_([this](const std::filesystem::path& path, const std::string& err) {
          if (logger_ != nullptr) {
              logger_->Error("Async write failed for '" + core::utf8::PathToUtf8(path) +
                             "': " + err);
          }
      }) {
    if (catalog_.groups.empty()) {
        catalog_ = core::CatalogDocument::CreateDefault();
    }

    // Ensure consistent sorting & folder sync on startup
    for (auto& grp : catalog_.groups) {
        if (grp.kind == core::GroupKind::LinkedFolder) {
            (void)core::SyncLinkedFolderGroup(grp);
        }
        core::SortItems(grp.items, settings_.behavior.itemSort);
    }
    core::SortGroups(catalog_.groups, settings_.behavior.groupSort);

    if (settings_.state.lastSelectedGroupId.has_value()) {
        for (std::size_t i = 0; i < catalog_.groups.size(); ++i) {
            if (catalog_.groups[i].id == *settings_.state.lastSelectedGroupId) {
                activeGroupIndex_ = i;
                break;
            }
        }
    }
    searchEngine_.Rebuild(catalog_);
}

MainWindow::~MainWindow() {
    Destroy();
}

bool MainWindow::Create(HINSTANCE hInstance, bool registerSystemHooks) {
    if (hwnd_ != nullptr) {
        return true;
    }

    if (!renderer_.Initialize()) {
        if (logger_ != nullptr) {
            logger_->Error("Failed to initialize Direct2D/DirectWrite factories");
        }
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::StaticWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (logger_ != nullptr) {
            logger_->Error("RegisterClassExW failed (Win32 error " +
                           std::to_string(GetLastError()) + ")");
        }
        return false;
    }

    POINT cursorPt{};
    GetCursorPos(&cursorPt);
    const auto metrics = platform::GetMonitorMetricsForPoint(cursorPt);
    dpi_ = metrics.dpi;
    const int initW = core::DipToPhysicalPx(settings_.window.width, dpi_);
    const int initH = core::DipToPhysicalPx(settings_.window.height, dpi_);
    const auto initPos = core::ComputeCenteredPosition(initW, initH, metrics.workArea);

    DWORD exStyle = WS_EX_TOOLWINDOW;
    if (settings_.window.alwaysTopmost) {
        exStyle |= WS_EX_TOPMOST;
    }

    suppressSizePersist_ = true;
    hwnd_ = CreateWindowExW(exStyle, kWindowClassName, L"GeekDeskCpp",
                            WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN, initPos.x, initPos.y,
                            initW, initH, nullptr, nullptr, hInstance, this);
    suppressSizePersist_ = false;

    if (hwnd_ == nullptr) {
        if (logger_ != nullptr) {
            logger_->Error("CreateWindowExW failed (Win32 error " +
                           std::to_string(GetLastError()) + ")");
        }
        return false;
    }

    dpi_ = platform::GetWindowDpi(hwnd_);
    iconCache_.Initialize(dataRoot_, hwnd_, kMsgIconReady);
    ApplyBackdropAndPalette();
    RebuildDisplayedItems();

    if (registerSystemHooks) {
        hooksRegistered_ = true;
        wtsRegistered_ = (WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION) != FALSE);

        if (settings_.activation.enableHotKey) {
            std::string err;
            if (!hotKeyManager_.Register(hwnd_, settings_.activation.hotKey, err)) {
                hotKeyError_ = err;
                if (logger_ != nullptr) {
                    logger_->Warn(err);
                }
            }
        }

        if (settings_.activation.enableMiddleMouse) {
            if (!mouseHook_.Start(hwnd_, kMsgMiddleClick) && logger_ != nullptr) {
                logger_->Warn("Failed to install WH_MOUSE_LL hook");
            }
        }

        if (settings_.state.showTrayIcon) {
            trayIcon_.Show(hwnd_, kMsgTrayIcon, L"GeekDeskCpp");
        }

        // Synchronize startup AutoStart registry state with settings.behavior.autoStart cleanly
        (void)platform::SetAutoStartEnabled(settings_.behavior.autoStart);

        // Register external OLE drop target (CF_HDROP) & start LinkedFolder real-time watcher
        DropTargetCallbacks dtCb;
        dtCb.onHover = [this](POINT screenPt) {
            bool accepted = false;
            HandleExternalDropHover(screenPt, accepted);
            return accepted;
        };
        dtCb.onLeave = [this]() {
            if (dropHighlightGroupIdx_ != -1 || dropHighlightGrid_) {
                dropHighlightGroupIdx_ = -1;
                dropHighlightGrid_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        };
        dtCb.onDrop = [this](POINT screenPt, const std::vector<std::filesystem::path>& paths) {
            HandleExternalDropFiles(screenPt, paths);
        };
        dropTarget_.Attach(new ExternalDropTarget(std::move(dtCb)));
        if (SUCCEEDED(RegisterDragDrop(hwnd_, dropTarget_.Get()))) {
            dropRegistered_ = true;
        } else {
            dropTarget_.Reset();
        }

        RestartFolderWatchers();
    }

    ResetDefaultStatus();
    return true;
}

void MainWindow::Destroy() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    renderer_.Shutdown();
    debouncedWriter_.Flush();
}

void MainWindow::ShowAndPosition() {
    if (hwnd_ == nullptr) {
        return;
    }

    POINT cursorPt{};
    GetCursorPos(&cursorPt);
    const auto metrics = platform::GetMonitorMetricsForPoint(cursorPt);
    dpi_ = metrics.dpi;

    const int widthPx = std::max(320, core::DipToPhysicalPx(settings_.window.width, dpi_));
    const int heightPx = std::max(240, core::DipToPhysicalPx(settings_.window.height, dpi_));

    const core::PixelPoint pos =
        settings_.window.followMouse
            ? core::ComputeTopLeftClampedPosition({cursorPt.x, cursorPt.y}, widthPx, heightPx,
                                                  metrics.workArea)
            : core::ComputeCenteredPosition(widthPx, heightPx, metrics.workArea);

    const HWND zOrder = settings_.window.alwaysTopmost ? HWND_TOPMOST : HWND_NOTOPMOST;
    suppressSizePersist_ = true;
    SetWindowPos(hwnd_, zOrder, pos.x, pos.y, widthPx, heightPx, SWP_SHOWWINDOW);
    suppressSizePersist_ = false;

    ApplyBackdropAndPalette();
    lastShownTickMs_ = GetTickCount64();
    ForceWindowForeground(hwnd_);

    caretVisible_ = true;
    SetTimer(hwnd_, kTimerCaret, 530, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::HideWindow() {
    if (hwnd_ == nullptr || !IsWindowVisible(hwnd_)) {
        return;
    }
    KillTimer(hwnd_, kTimerCaret);
    KillTimer(hwnd_, kTimerHoverGroup);
    imeCompWide_.clear();
    if (HIMC himc = ImmGetContext(hwnd_); himc != nullptr) {
        ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmReleaseContext(hwnd_, himc);
    }
    ShowWindow(hwnd_, SW_HIDE);
    renderer_.ReleaseBackBuffer();
}

void MainWindow::ToggleWindow() {
    if (hwnd_ == nullptr) {
        return;
    }
    if (IsWindowVisible(hwnd_)) {
        HideWindow();
    } else {
        ShowAndPosition();
    }
}

bool MainWindow::RenderSmokeFrame() {
    if (hwnd_ == nullptr) {
        return false;
    }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    const bool updated = (UpdateWindow(hwnd_) != FALSE);
    HideWindow();
    return updated;
}

SoakReport MainWindow::RunSoakCycles(int cycles) {
    SoakReport report;
    report.cycles = cycles;
    if (hwnd_ == nullptr || cycles <= 0) {
        return report;
    }

    const core::AppSettings savedSettings = settings_;
    const std::size_t savedGroupIdx = activeGroupIndex_;
    const std::wstring savedQuery = searchQueryWide_;

    auto pumpWindowQueue = []() {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    };

    auto captureResources = [](std::uint32_t& outGdi, std::uint32_t& outUser,
                               std::uint32_t& outHandles, SIZE_T& outWs, SIZE_T& outPriv) {
        HANDLE self = GetCurrentProcess();
        outGdi = GetGuiResources(self, GR_GDIOBJECTS);
        outUser = GetGuiResources(self, GR_USEROBJECTS);
        DWORD hCount = 0;
        GetProcessHandleCount(self, &hCount);
        outHandles = static_cast<std::uint32_t>(hCount);
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        GetProcessMemoryInfo(self, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
        outWs = pmc.WorkingSetSize;
        outPriv = pmc.PrivateUsage;
    };

    suppressDiskPersist_ = true;

    const core::BackdropMode kModes[] = {
        core::BackdropMode::Acrylic,
        core::BackdropMode::Mica,
        core::BackdropMode::MicaAlt,
        core::BackdropMode::Gradient,
    };

    auto runStep = [&](int i) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd_);

        if (!catalog_.groups.empty()) {
            const std::size_t nextGrp = static_cast<std::size_t>(i) % catalog_.groups.size();
            SelectGroup(nextGrp, false);
        }
        (void)searchEngine_.Search("a", 24);

        if ((i % 4) == 0) {
            core::AppSettings next = settings_;
            next.appearance.backdrop = kModes[(i / 4) % 4];
            next.appearance.backgroundOpacity = 0.25 + 0.15 * static_cast<double>((i / 4) % 5);
            next.appearance.cardOpacity = 0.10 + 0.15 * static_cast<double>((i / 4) % 4);
            ApplySettingsLive(next);
            UpdateWindow(hwnd_);
        }

        if ((i % 25) == 0) {
            RefreshHooksAndTray();
            OpenSettingsWindow();
            pumpWindowQueue();
            settingsWindow_.Close();
            pumpWindowQueue();
        }

        HideWindow();
        pumpWindowQueue();
    };

    // Warmup cycles (including visiting every group & opening Settings twice) to finish lazy OS
    // Shell/DirectWrite/Common-Controls allocations before capturing steady-state baseline.
    const int warmupCount = std::max(12, static_cast<int>(catalog_.groups.size()) + 2);
    for (int w = 0; w < warmupCount; ++w) {
        runStep(w);
    }
    OpenSettingsWindow();
    pumpWindowQueue();
    settingsWindow_.Close();
    pumpWindowQueue();
    iconCache_.WaitUntilIdle(5000);
    // Paint once more after icons arrive so ID2D1Bitmap caches are populated cleanly at baseline.
    for (std::size_t gi = 0; gi < catalog_.groups.size(); ++gi) {
        SelectGroup(gi, false);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd_);
        HideWindow();
    }
    pumpWindowQueue();
    captureResources(report.gdiStart, report.userStart, report.handlesStart,
                     report.workingSetStart, report.privateStart);

    for (int i = 0; i < cycles; ++i) {
        runStep(i);
    }

    ApplySettingsLive(savedSettings);
    activeGroupIndex_ = std::min(savedGroupIdx, catalog_.groups.size() - 1);
    searchQueryWide_ = savedQuery;
    RebuildDisplayedItems();
    HideWindow();
    pumpWindowQueue();

    captureResources(report.gdiEnd, report.userEnd, report.handlesEnd, report.workingSetEnd,
                     report.privateEnd);
    suppressDiskPersist_ = false;
    report.ok = true;
    return report;
}

LRESULT CALLBACK MainWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (singleInstanceMsg_ != 0 && msg == singleInstanceMsg_) {
        ShowAndPosition();
        return 0;
    }
    if (trayIcon_.TaskbarCreatedMessage() != 0 && msg == trayIcon_.TaskbarCreatedMessage()) {
        if (hooksRegistered_ && settings_.state.showTrayIcon) {
            trayIcon_.Recreate();
        }
        return 0;
    }

    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            return 0;
        }
        break;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = core::DipToPhysicalPx(480.0, dpi_);
        mmi->ptMinTrackSize.y = core::DipToPhysicalPx(320.0, dpi_);
        return 0;
    }

    case WM_NCHITTEST: {
        RECT winRect{};
        GetWindowRect(hwnd_, &winRect);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int border = core::DipToPhysicalPx(6.0, dpi_);

        const bool onLeft = x < winRect.left + border;
        const bool onRight = x >= winRect.right - border;
        const bool onTop = y < winRect.top + border;
        const bool onBottom = y >= winRect.bottom - border;

        if (onTop && onLeft) {
            return HTTOPLEFT;
        }
        if (onTop && onRight) {
            return HTTOPRIGHT;
        }
        if (onBottom && onLeft) {
            return HTBOTTOMLEFT;
        }
        if (onBottom && onRight) {
            return HTBOTTOMRIGHT;
        }
        if (onLeft) {
            return HTLEFT;
        }
        if (onRight) {
            return HTRIGHT;
        }
        if (onTop) {
            return HTTOP;
        }
        if (onBottom) {
            return HTBOTTOM;
        }

        POINT clientPt{x, y};
        ScreenToClient(hwnd_, &clientPt);
        const float xDip = PxToDip(clientPt.x);
        const float yDip = PxToDip(clientPt.y);
        if (layout_.headerRect.Contains(xDip, yDip) && !layout_.searchBoxRect.Contains(xDip, yDip) &&
            HitTestHeaderButton(xDip, yDip) == HeaderButton::None) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_SIZE:
        OnResizeOrDpiChanged();
        return 0;

    case WM_DPICHANGED: {
        dpi_ = LOWORD(wParam);
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested != nullptr) {
            suppressSizePersist_ = true;
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            suppressSizePersist_ = false;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }

    case WM_SETTINGCHANGE:
        if (lParam != 0 &&
            std::wstring_view(reinterpret_cast<const wchar_t*>(lParam)) == L"ImmersiveColorSet") {
            ApplyBackdropAndPalette();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && settings_.window.hidePolicy == core::HidePolicy::OnDeactivate &&
            !inTrayMenu_ && modalDepth_ == 0 && !settingsWindow_.IsOpen() &&
            (GetTickCount64() - lastShownTickMs_ > 180)) {
            HideWindow();
        }
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_MOUSELEAVE:
        OnMouseLeave();
        return 0;

    case WM_LBUTTONDOWN:
        OnLeftButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_LBUTTONUP:
        OnLeftButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_LBUTTONDBLCLK:
        OnLeftButtonDoubleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_RBUTTONUP:
        OnRightButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_CAPTURECHANGED:
        CancelInternalDrag();
        return 0;

    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;

    case WM_KEYDOWN:
        OnKeyDown(wParam);
        return 0;

    case WM_CHAR:
        OnChar(static_cast<wchar_t>(wParam));
        return 0;

    case WM_IME_STARTCOMPOSITION:
        OnImeStartComposition();
        break;

    case WM_IME_COMPOSITION:
        OnImeComposition(lParam);
        break;

    case WM_IME_ENDCOMPOSITION:
        imeCompWide_.clear();
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;

    case WM_HOTKEY:
        if (static_cast<int>(wParam) == platform::HotKeyManager::kDefaultHotKeyId) {
            OnHotKeyTriggered();
        }
        return 0;

    case kMsgMiddleClick:
        if (settings_.activation.enableMiddleMouse) {
            OnHotKeyTriggered();
        }
        return 0;

    case kMsgTrayIcon:
        OnTrayIconEvent(lParam);
        return 0;

    case kMsgIconReady:
        if (IsWindowVisible(hwnd_)) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;

    case kMsgFolderChanged:
        OnFolderChangedNotifications();
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMESUSPEND || wParam == PBT_APMRESUMEAUTOMATIC) {
            SetTimer(hwnd_, kTimerResumeRecover, 300, nullptr);
        }
        return TRUE;

    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_UNLOCK) {
            SetTimer(hwnd_, kTimerResumeRecover, 300, nullptr);
        }
        return 0;

    case WM_TIMER:
        if (wParam == kTimerCaret) {
            if (searchFocused_) {
                caretVisible_ = !caretVisible_;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        } else if (wParam == kTimerHoverGroup) {
            KillTimer(hwnd_, kTimerHoverGroup);
            if (settings_.behavior.hoverSwitchGroup && hoveredGroupIdx_ >= 0 &&
                static_cast<std::size_t>(hoveredGroupIdx_) < catalog_.groups.size()) {
                SelectGroup(static_cast<std::size_t>(hoveredGroupIdx_), true);
            }
        } else if (wParam == kTimerResumeRecover) {
            KillTimer(hwnd_, kTimerResumeRecover);
            RefreshHooksAndTray();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd_, kTimerCaret);
        KillTimer(hwnd_, kTimerHoverGroup);
        KillTimer(hwnd_, kTimerResumeRecover);
        if (wtsRegistered_) {
            WTSUnRegisterSessionNotification(hwnd_);
            wtsRegistered_ = false;
        }
        settingsWindow_.Close();
        folderWatcher_.Stop();
        if (dropRegistered_) {
            RevokeDragDrop(hwnd_);
            dropRegistered_ = false;
        }
        dropTarget_.Reset();
        mouseHook_.Stop();
        hotKeyManager_.Unregister();
        trayIcon_.Hide();
        iconCache_.Shutdown();
        hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void MainWindow::OnPaint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int widthPx = rc.right - rc.left;
    const int heightPx = rc.bottom - rc.top;

    if (hdc != nullptr && widthPx > 0 && heightPx > 0) {
        RenderState state;
        state.settings = &settings_;
        state.palette = &palette_;
        state.groups = &catalog_.groups;
        state.activeGroupIndex = activeGroupIndex_;
        state.displayedItems = &displayedItems_;
        state.isSearchActive = !searchQueryWide_.empty();
        state.searchQueryWide = searchQueryWide_;
        state.imeCompWide = imeCompWide_;
        state.searchCaretIndex = searchCaretIndex_;
        state.searchFocused = searchFocused_;
        state.caretVisible = caretVisible_;
        state.scrollOffsetDip = scrollOffsetDip_;
        state.hoveredGroupIdx = hoveredGroupIdx_;
        state.hoveredTileIdx = hoveredTileIdx_;
        state.selectedTileIdx = selectedTileIdx_;
        state.dropHighlightGroupIdx = dropHighlightGroupIdx_;
        state.dropHighlightGrid = dropHighlightGrid_;
        state.draggingTileIdx = (dragActive_ && dragCandidate_ == DragKind::Tile) ? dragStartIndex_ : -1;
        state.dragTargetTileIdx = (dragActive_ && dragCandidate_ == DragKind::Tile) ? dragTargetTileIdx_ : -1;
        state.draggingGroupIdx = (dragActive_ && dragCandidate_ == DragKind::Group) ? dragStartIndex_ : -1;
        state.dragTargetGroupIdx = dragActive_ ? dragTargetGroupIdx_ : -1;
        state.hoveredHeaderButton = hoveredHeaderBtn_;
        state.alwaysTopmost = settings_.window.alwaysTopmost;
        state.statusText = statusText_;
        state.dataRoot = dataRoot_;
        state.iconCache = &iconCache_;

        renderer_.Render(hdc, widthPx, heightPx, dpi_, state, layout_);
        scrollOffsetDip_ = layout_.clampedScrollDip;
    }

    EndPaint(hwnd_, &ps);
}

void MainWindow::OnResizeOrDpiChanged() {
    if (suppressSizePersist_ || hwnd_ == nullptr || IsIconic(hwnd_)) {
        return;
    }
    RECT rc{};
    if (!GetWindowRect(hwnd_, &rc)) {
        return;
    }
    const double scale = 96.0 / static_cast<double>(dpi_ > 0 ? dpi_ : 96u);
    const double wDip = std::round(static_cast<double>(rc.right - rc.left) * scale);
    const double hDip = std::round(static_cast<double>(rc.bottom - rc.top) * scale);
    if (wDip >= 480.0 && hDip >= 320.0 &&
        (std::abs(wDip - settings_.window.width) >= 1.0 ||
         std::abs(hDip - settings_.window.height) >= 1.0)) {
        settings_.window.width = wDip;
        settings_.window.height = hDip;
        ScheduleSaveSettings();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnMouseMove(int xPx, int yPx) {
    if (!trackingMouseLeave_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        trackingMouseLeave_ = true;
    }

    const float xDip = PxToDip(xPx);
    const float yDip = PxToDip(yPx);

    if (dragCandidate_ != DragKind::None) {
        if (!dragActive_) {
            const int dx = std::abs(xPx - dragOriginPx_.x);
            const int dy = std::abs(yPx - dragOriginPx_.y);
            if (dx >= GetSystemMetrics(SM_CXDRAG) || dy >= GetSystemMetrics(SM_CYDRAG)) {
                dragActive_ = true;
            }
        }
        if (dragActive_) {
            const int hitGrp = HitTestGroup(xDip, yDip);
            const int hitTile = HitTestTile(xDip, yDip);
            int nextTargetGrp = -1;
            int nextTargetTile = -1;

            if (dragCandidate_ == DragKind::Group) {
                if (hitGrp >= 0 && hitGrp != dragStartIndex_) {
                    nextTargetGrp = hitGrp;
                }
            } else if (dragCandidate_ == DragKind::Tile) {
                if (hitGrp >= 0 && static_cast<std::size_t>(hitGrp) != activeGroupIndex_ &&
                    static_cast<std::size_t>(hitGrp) < catalog_.groups.size() &&
                    catalog_.groups[static_cast<std::size_t>(hitGrp)].kind ==
                        core::GroupKind::Manual &&
                    activeGroupIndex_ < catalog_.groups.size() &&
                    catalog_.groups[activeGroupIndex_].kind == core::GroupKind::Manual &&
                    searchQueryWide_.empty()) {
                    nextTargetGrp = hitGrp;
                } else if (hitTile >= 0 && searchQueryWide_.empty() &&
                           activeGroupIndex_ < catalog_.groups.size() &&
                           catalog_.groups[activeGroupIndex_].kind == core::GroupKind::Manual) {
                    nextTargetTile = hitTile;
                }
            }

            if (nextTargetGrp != dragTargetGroupIdx_ || nextTargetTile != dragTargetTileIdx_) {
                dragTargetGroupIdx_ = nextTargetGrp;
                dragTargetTileIdx_ = nextTargetTile;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return;
        }
    }

    const int newGroup = HitTestGroup(xDip, yDip);
    const int newTile = HitTestTile(xDip, yDip);
    const HeaderButton newBtn = HitTestHeaderButton(xDip, yDip);
    const bool groupChanged = (newGroup != hoveredGroupIdx_);

    if (groupChanged) {
        hoveredGroupIdx_ = newGroup;
        if (settings_.behavior.hoverSwitchGroup && newGroup >= 0) {
            SetTimer(hwnd_, kTimerHoverGroup, 180, nullptr);
        } else {
            KillTimer(hwnd_, kTimerHoverGroup);
        }
    }

    const bool changed = (groupChanged || newTile != hoveredTileIdx_ || newBtn != hoveredHeaderBtn_);
    hoveredTileIdx_ = newTile;
    hoveredHeaderBtn_ = newBtn;

    if (changed) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnMouseLeave() {
    trackingMouseLeave_ = false;
    KillTimer(hwnd_, kTimerHoverGroup);
    if (hoveredGroupIdx_ != -1 || hoveredTileIdx_ != -1 ||
        hoveredHeaderBtn_ != HeaderButton::None) {
        hoveredGroupIdx_ = -1;
        hoveredTileIdx_ = -1;
        hoveredHeaderBtn_ = HeaderButton::None;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnLeftButtonDown(int xPx, int yPx) {
    const float xDip = PxToDip(xPx);
    const float yDip = PxToDip(yPx);

    if (const HeaderButton btn = HitTestHeaderButton(xDip, yDip); btn != HeaderButton::None) {
        if (btn == HeaderButton::Pin) {
            settings_.window.alwaysTopmost = !settings_.window.alwaysTopmost;
            SetWindowPos(hwnd_, settings_.window.alwaysTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0,
                         0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ScheduleSaveSettings();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (btn == HeaderButton::Refresh) {
            SyncAllLinkedFolders(true);
        } else if (btn == HeaderButton::Settings) {
            OpenSettingsWindow();
        } else if (btn == HeaderButton::Hide) {
            HideWindow();
        }
        return;
    }

    if (layout_.searchBoxRect.Contains(xDip, yDip)) {
        searchFocused_ = true;
        searchCaretIndex_ = searchQueryWide_.size();
        caretVisible_ = true;
        SetTimer(hwnd_, kTimerCaret, 530, nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    searchFocused_ = false;
    KillTimer(hwnd_, kTimerCaret);

    if (const int gIdx = HitTestGroup(xDip, yDip); gIdx >= 0) {
        SelectGroup(static_cast<std::size_t>(gIdx), true);
        dragCandidate_ = DragKind::Group;
        dragActive_ = false;
        dragStartIndex_ = gIdx;
        dragOriginPx_ = {xPx, yPx};
        dragTargetGroupIdx_ = -1;
        dragTargetTileIdx_ = -1;
        SetCapture(hwnd_);
        return;
    }

    if (const int tIdx = HitTestTile(xDip, yDip); tIdx >= 0) {
        selectedTileIdx_ = tIdx;
        dragCandidate_ = DragKind::Tile;
        dragActive_ = false;
        dragStartIndex_ = tIdx;
        dragOriginPx_ = {xPx, yPx};
        dragTargetGroupIdx_ = -1;
        dragTargetTileIdx_ = -1;
        SetCapture(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnLeftButtonUp(int xPx, int yPx) {
    if (dragCandidate_ == DragKind::None) {
        return;
    }

    const DragKind kind = dragCandidate_;
    const bool wasDragged = dragActive_;
    const int startIdx = dragStartIndex_;
    const int targetGrp = dragTargetGroupIdx_;
    const int targetTile = dragTargetTileIdx_;

    CancelInternalDrag();
    if (GetCapture() == hwnd_) {
        ReleaseCapture();
    }

    if (wasDragged) {
        if (kind == DragKind::Group && startIdx >= 0 && targetGrp >= 0 && startIdx != targetGrp) {
            if (core::ReorderGroup(catalog_.groups, static_cast<std::size_t>(startIdx),
                                   static_cast<std::size_t>(targetGrp))) {
                activeGroupIndex_ = static_cast<std::size_t>(targetGrp);
                settings_.behavior.groupSort = core::SortOrder::Custom;
                settingsWindow_.SyncFromExternalSettings(settings_);
                ScheduleSaveSettings();
                ScheduleSaveCatalog();
                RebuildDisplayedItems();
                statusText_ = "\u5DF2\u8C03\u6574\u5206\u7EC4\u987A\u5E8F";
            }
        } else if (kind == DragKind::Tile && startIdx >= 0 && searchQueryWide_.empty() &&
                   activeGroupIndex_ < catalog_.groups.size()) {
            if (targetGrp >= 0) {
                if (core::MoveItemBetweenManualGroups(
                        catalog_, activeGroupIndex_, static_cast<std::size_t>(startIdx),
                        static_cast<std::size_t>(targetGrp))) {
                    searchEngine_.Rebuild(catalog_);
                    RebuildDisplayedItems();
                    ScheduleSaveCatalog();
                    statusText_ = "\u5DF2\u79FB\u52A8\u6761\u76EE\u81F3 \"" +
                                  catalog_.groups[static_cast<std::size_t>(targetGrp)].name + "\"";
                }
            } else if (targetTile >= 0 && targetTile != startIdx &&
                       catalog_.groups[activeGroupIndex_].kind == core::GroupKind::Manual) {
                if (core::ReorderItem(catalog_.groups[activeGroupIndex_].items,
                                      static_cast<std::size_t>(startIdx),
                                      static_cast<std::size_t>(targetTile))) {
                    settings_.behavior.itemSort = core::SortOrder::Custom;
                    settingsWindow_.SyncFromExternalSettings(settings_);
                    selectedTileIdx_ = targetTile;
                    searchEngine_.Rebuild(catalog_);
                    RebuildDisplayedItems();
                    ScheduleSaveSettings();
                    ScheduleSaveCatalog();
                    statusText_ = "\u5DF2\u8C03\u6574\u56FE\u6807\u6392\u5E8F";
                }
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // Standard single-click launch when released without dragging
    if (kind == DragKind::Tile && !settings_.behavior.doubleClickToLaunch && startIdx >= 0) {
        const float xDip = PxToDip(xPx);
        const float yDip = PxToDip(yPx);
        if (HitTestTile(xDip, yDip) == startIdx) {
            LaunchDisplayedIndex(static_cast<std::size_t>(startIdx));
        }
    }
}

void MainWindow::OnLeftButtonDoubleClick(int xPx, int yPx) {
    if (!settings_.behavior.doubleClickToLaunch) {
        return;
    }
    const float xDip = PxToDip(xPx);
    const float yDip = PxToDip(yPx);
    if (const int tIdx = HitTestTile(xDip, yDip); tIdx >= 0) {
        selectedTileIdx_ = tIdx;
        LaunchDisplayedIndex(static_cast<std::size_t>(tIdx));
    }
}

void MainWindow::OnRightButtonUp(int xPx, int yPx) {
    const float xDip = PxToDip(xPx);
    const float yDip = PxToDip(yPx);
    POINT screenPt{xPx, yPx};
    ClientToScreen(hwnd_, &screenPt);

    if (layout_.sidebarRect.Contains(xDip, yDip)) {
        const int gIdx = HitTestGroup(xDip, yDip);
        const bool clickedGroup = (gIdx >= 0 && static_cast<std::size_t>(gIdx) < catalog_.groups.size());
        const bool isLinked =
            clickedGroup &&
            catalog_.groups[static_cast<std::size_t>(gIdx)].kind == core::GroupKind::LinkedFolder;

        ModalGuard guard(modalDepth_);
        const auto action = ShowGroupContextMenu(
            hwnd_, screenPt, clickedGroup, isLinked,
            clickedGroup ? static_cast<std::size_t>(gIdx) : 0u, catalog_.groups.size());
        if (!action.has_value()) {
            return;
        }

        switch (*action) {
        case GroupMenuAction::NewManualGroup: {
            if (auto name = ShowTextInputDialog(hwnd_, palette_.isDark, L"\u65B0\u5EFA\u666E\u901A\u5206\u7EC4",
                                                L"\u8BF7\u8F93\u5165\u5206\u7EC4\u540D\u79F0:", "");
                name.has_value()) {
                const std::size_t idx = core::AddManualGroup(catalog_, *name);
                if (settings_.behavior.groupSort != core::SortOrder::Custom) {
                    const std::string newId = catalog_.groups[idx].id;
                    core::SortGroups(catalog_.groups, settings_.behavior.groupSort);
                    for (std::size_t i = 0; i < catalog_.groups.size(); ++i) {
                        if (catalog_.groups[i].id == newId) {
                            activeGroupIndex_ = i;
                            break;
                        }
                    }
                } else {
                    activeGroupIndex_ = idx;
                }
                settings_.state.lastSelectedGroupId = catalog_.groups[activeGroupIndex_].id;
                searchEngine_.Rebuild(catalog_);
                RebuildDisplayedItems();
                ScheduleSaveSettings();
                ScheduleSaveCatalog();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            break;
        }
        case GroupMenuAction::NewLinkedFolderGroup: {
            if (auto folder = platform::PickFolderDialog(hwnd_, L"\u9009\u62E9\u8981\u5173\u8054\u7684\u6587\u4EF6\u5939");
                folder.has_value()) {
                const std::string defName = core::utf8::PathToUtf8(folder->filename());
                if (auto name = ShowTextInputDialog(hwnd_, palette_.isDark,
                                                    L"\u65B0\u5EFA\u5173\u8054\u6587\u4EF6\u5939\u5206\u7EC4",
                                                    L"\u5206\u7EC4\u540D\u79F0:", defName);
                    name.has_value()) {
                    const std::size_t idx = core::AddLinkedFolderGroup(
                        catalog_, *name, core::utf8::PathToUtf8(*folder));
                    const std::string newId = catalog_.groups[idx].id;
                    core::SortItems(catalog_.groups[idx].items, settings_.behavior.itemSort);
                    core::SortGroups(catalog_.groups, settings_.behavior.groupSort);
                    for (std::size_t i = 0; i < catalog_.groups.size(); ++i) {
                        if (catalog_.groups[i].id == newId) {
                            activeGroupIndex_ = i;
                            break;
                        }
                    }
                    RestartFolderWatchers();
                    settings_.state.lastSelectedGroupId = catalog_.groups[activeGroupIndex_].id;
                    searchEngine_.Rebuild(catalog_);
                    RebuildDisplayedItems();
                    ScheduleSaveSettings();
                    ScheduleSaveCatalog();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            break;
        }
        case GroupMenuAction::RenameGroup:
            if (clickedGroup) {
                const auto idx = static_cast<std::size_t>(gIdx);
                if (auto name = ShowTextInputDialog(hwnd_, palette_.isDark, L"\u91CD\u547D\u540D\u5206\u7EC4",
                                                    L"\u65B0\u5206\u7EC4\u540D\u79F0:",
                                                    catalog_.groups[idx].name);
                    name.has_value()) {
                    if (core::RenameGroup(catalog_, idx, *name)) {
                        searchEngine_.Rebuild(catalog_);
                        RebuildDisplayedItems();
                        ScheduleSaveCatalog();
                        InvalidateRect(hwnd_, nullptr, FALSE);
                    }
                }
            }
            break;
        case GroupMenuAction::SyncLinkedGroup:
            if (clickedGroup) {
                const auto idx = static_cast<std::size_t>(gIdx);
                const auto res = core::SyncLinkedFolderGroup(catalog_.groups[idx]);
                core::SortItems(catalog_.groups[idx].items, settings_.behavior.itemSort);
                searchEngine_.Rebuild(catalog_);
                iconCache_.ClearMemoryCache();
                RebuildDisplayedItems();
                ScheduleSaveCatalog();
                statusText_ = "\u540C\u6B65\u5B8C\u6210 (+" + std::to_string(res.added) + " / -" +
                              std::to_string(res.removed) + ")";
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            break;
        case GroupMenuAction::MoveGroupUp:
            if (clickedGroup && gIdx > 0) {
                const auto idx = static_cast<std::size_t>(gIdx);
                if (core::ReorderGroup(catalog_.groups, idx, idx - 1)) {
                    activeGroupIndex_ = idx - 1;
                    settings_.behavior.groupSort = core::SortOrder::Custom;
                    settingsWindow_.SyncFromExternalSettings(settings_);
                    ScheduleSaveSettings();
                    ScheduleSaveCatalog();
                    RebuildDisplayedItems();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            break;
        case GroupMenuAction::MoveGroupDown:
            if (clickedGroup && static_cast<std::size_t>(gIdx) + 1 < catalog_.groups.size()) {
                const auto idx = static_cast<std::size_t>(gIdx);
                if (core::ReorderGroup(catalog_.groups, idx, idx + 1)) {
                    activeGroupIndex_ = idx + 1;
                    settings_.behavior.groupSort = core::SortOrder::Custom;
                    settingsWindow_.SyncFromExternalSettings(settings_);
                    ScheduleSaveSettings();
                    ScheduleSaveCatalog();
                    RebuildDisplayedItems();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            break;
        case GroupMenuAction::DeleteGroup:
            if (clickedGroup) {
                const auto idx = static_cast<std::size_t>(gIdx);
                const std::wstring confirm =
                    L"\u786E\u5B9A\u5220\u9664\u5206\u7EC4 \"" +
                    core::utf8::Utf8ToWide(catalog_.groups[idx].name) + L"\" \u53CA\u5176\u5185\u90E8\u6761\u76EE\u5417\uFF1F";
                if (MessageBoxW(hwnd_, confirm.c_str(), L"\u5220\u9664\u5206\u7EC4",
                                MB_YESNO | MB_ICONWARNING) == IDYES) {
                    core::DeleteGroup(catalog_, idx);
                    activeGroupIndex_ =
                        std::min(activeGroupIndex_, catalog_.groups.size() - 1);
                    settings_.state.lastSelectedGroupId = catalog_.groups[activeGroupIndex_].id;
                    RestartFolderWatchers();
                    searchEngine_.Rebuild(catalog_);
                    RebuildDisplayedItems();
                    ScheduleSaveSettings();
                    ScheduleSaveCatalog();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            break;
        }
        return;
    }

    if (layout_.contentCardRect.Contains(xDip, yDip) && activeGroupIndex_ < catalog_.groups.size()) {
        const int tIdx = HitTestTile(xDip, yDip);
        const bool clickedItem =
            (tIdx >= 0 && static_cast<std::size_t>(tIdx) < displayedItems_.size() &&
             displayedItems_[static_cast<std::size_t>(tIdx)].item != nullptr);

        std::size_t targetGroupIdx = activeGroupIndex_;
        std::size_t targetItemIdx = 0;
        bool foundIndices = false;
        if (clickedItem) {
            selectedTileIdx_ = tIdx;
            foundIndices = FindCatalogIndices(displayedItems_[static_cast<std::size_t>(tIdx)].item,
                                              targetGroupIdx, targetItemIdx);
        }

        const bool isManual =
            (targetGroupIdx < catalog_.groups.size() &&
             catalog_.groups[targetGroupIdx].kind == core::GroupKind::Manual);
        const bool curAdmin =
            foundIndices ? catalog_.groups[targetGroupIdx].items[targetItemIdx].runAsAdmin : false;

        ModalGuard guard(modalDepth_);
        const auto action = ShowItemContextMenu(
            hwnd_, screenPt, clickedItem && foundIndices, isManual, curAdmin,
            settings_.behavior.itemSort);
        if (!action.has_value()) {
            return;
        }

        switch (*action) {
        case ItemMenuAction::OpenItem:
            if (clickedItem) {
                LaunchDisplayedIndex(static_cast<std::size_t>(tIdx));
            }
            break;
        case ItemMenuAction::RunAsAdminOnce:
            if (foundIndices) {
                core::LaunchItem elevated = catalog_.groups[targetGroupIdx].items[targetItemIdx];
                elevated.runAsAdmin = true;
                const auto lr = platform::LaunchCatalogItem(elevated);
                if (lr.ok) {
                    catalog_.groups[targetGroupIdx].items[targetItemIdx].launchCount += 1;
                    catalog_.groups[targetGroupIdx].items[targetItemIdx].lastLaunchedAt =
                        CurrentUtcIsoTimestamp();
                    ScheduleSaveCatalog();
                    if (settings_.window.hidePolicy == core::HidePolicy::AfterLaunch) {
                        HideWindow();
                    }
                } else {
                    statusText_ = "\u63D0\u6743\u542F\u52A8\u5931\u8D25: " + lr.error;
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            break;
        case ItemMenuAction::OpenLocation:
            if (foundIndices) {
                const auto& item = catalog_.groups[targetGroupIdx].items[targetItemIdx];
                const std::filesystem::path p =
                    core::utf8::PathFromUtf8((item.lnkPath.has_value() && !item.lnkPath->empty())
                                                 ? *item.lnkPath
                                                 : item.targetPath);
                (void)platform::RevealInExplorer(p);
            }
            break;
        case ItemMenuAction::EditItem:
            if (foundIndices) {
                core::LaunchItem& item = catalog_.groups[targetGroupIdx].items[targetItemIdx];
                if (ShowItemEditDialog(hwnd_, palette_.isDark, L"\u7F16\u8F91\u542F\u52A8\u6761\u76EE",
                                       item)) {
                    core::SortItems(catalog_.groups[targetGroupIdx].items,
                                    settings_.behavior.itemSort);
                    searchEngine_.Rebuild(catalog_);
                    iconCache_.ClearMemoryCache();
                    RebuildDisplayedItems();
                    ScheduleSaveCatalog();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            break;
        case ItemMenuAction::ToggleRunAsAdmin:
            if (foundIndices) {
                auto& item = catalog_.groups[targetGroupIdx].items[targetItemIdx];
                item.runAsAdmin = !item.runAsAdmin;
                ScheduleSaveCatalog();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            break;
        case ItemMenuAction::DeleteItem:
            if (foundIndices && isManual) {
                if (core::RemoveItemFromManualGroup(catalog_.groups[targetGroupIdx],
                                                    targetItemIdx)) {
                    searchEngine_.Rebuild(catalog_);
                    RebuildDisplayedItems();
                    ScheduleSaveCatalog();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            break;
        case ItemMenuAction::AddFile:
            if (catalog_.groups[activeGroupIndex_].kind == core::GroupKind::Manual) {
                if (auto file = platform::PickFileDialog(hwnd_, L"\u6DFB\u52A0\u6587\u4EF6\u6216\u5E94\u7528");
                    file.has_value()) {
                    core::LaunchItem item = BuildLaunchItemFromPath(*file);
                    if (core::AddItemToManualGroup(catalog_.groups[activeGroupIndex_],
                                                   std::move(item), true)) {
                        core::SortItems(catalog_.groups[activeGroupIndex_].items,
                                        settings_.behavior.itemSort);
                        searchEngine_.Rebuild(catalog_);
                        RebuildDisplayedItems();
                        ScheduleSaveCatalog();
                        InvalidateRect(hwnd_, nullptr, FALSE);
                    }
                }
            }
            break;
        case ItemMenuAction::AddFolder:
            if (catalog_.groups[activeGroupIndex_].kind == core::GroupKind::Manual) {
                if (auto dir = platform::PickFolderDialog(hwnd_, L"\u6DFB\u52A0\u6587\u4EF6\u5939\u5FEB\u6377\u65B9\u5F0F");
                    dir.has_value()) {
                    core::LaunchItem item = BuildLaunchItemFromPath(*dir);
                    if (core::AddItemToManualGroup(catalog_.groups[activeGroupIndex_],
                                                   std::move(item), true)) {
                        core::SortItems(catalog_.groups[activeGroupIndex_].items,
                                        settings_.behavior.itemSort);
                        searchEngine_.Rebuild(catalog_);
                        RebuildDisplayedItems();
                        ScheduleSaveCatalog();
                        InvalidateRect(hwnd_, nullptr, FALSE);
                    }
                }
            }
            break;
        case ItemMenuAction::AddCustom:
            if (catalog_.groups[activeGroupIndex_].kind == core::GroupKind::Manual) {
                core::LaunchItem item;
                item.id = core::utf8::GenerateUuidV4();
                item.kind = core::ItemKind::Url;
                if (ShowItemEditDialog(hwnd_, palette_.isDark, L"\u65B0\u589E\u81EA\u5B9A\u4E49\u6761\u76EE / \u7F51\u5740",
                                       item)) {
                    if (core::AddItemToManualGroup(catalog_.groups[activeGroupIndex_],
                                                   std::move(item), true)) {
                        core::SortItems(catalog_.groups[activeGroupIndex_].items,
                                        settings_.behavior.itemSort);
                        searchEngine_.Rebuild(catalog_);
                        RebuildDisplayedItems();
                        ScheduleSaveCatalog();
                        InvalidateRect(hwnd_, nullptr, FALSE);
                    }
                }
            }
            break;
        case ItemMenuAction::SortCustom:
        case ItemMenuAction::SortCountDesc:
        case ItemMenuAction::SortNameAsc: {
            const core::SortOrder nextSort =
                (*action == ItemMenuAction::SortCountDesc)
                    ? core::SortOrder::LaunchCountDesc
                    : ((*action == ItemMenuAction::SortNameAsc) ? core::SortOrder::NameAsc
                                                                : core::SortOrder::Custom);
            settings_.behavior.itemSort = nextSort;
            for (auto& grp : catalog_.groups) {
                core::SortItems(grp.items, nextSort);
            }
            settingsWindow_.SyncFromExternalSettings(settings_);
            searchEngine_.Rebuild(catalog_);
            RebuildDisplayedItems();
            ScheduleSaveSettings();
            ScheduleSaveCatalog();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
        }
    }
}

void MainWindow::OnMouseWheel(int xScreenPx, int yScreenPx, short wheelDelta) {
    if (wheelDelta == 0) {
        return;
    }
    POINT clientPt{xScreenPx, yScreenPx};
    ScreenToClient(hwnd_, &clientPt);
    const float xDip = PxToDip(clientPt.x);
    const float yDip = PxToDip(clientPt.y);

    if (layout_.sidebarRect.Contains(xDip, yDip)) {
        SwitchGroupByDelta(wheelDelta > 0 ? -1 : 1);
        return;
    }

    const float step = 68.0f * (static_cast<float>(wheelDelta) / 120.0f);
    if (searchQueryWide_.empty()) {
        if (layout_.maxScrollDip <= 1.0f ||
            (wheelDelta > 0 && scrollOffsetDip_ <= 0.5f) ||
            (wheelDelta < 0 && scrollOffsetDip_ >= layout_.maxScrollDip - 0.5f)) {
            SwitchGroupByDelta(wheelDelta > 0 ? -1 : 1);
            return;
        }
    }

    scrollOffsetDip_ = std::clamp(scrollOffsetDip_ - step, 0.0f, layout_.maxScrollDip);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnKeyDown(WPARAM vk) {
    const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (ctrlDown && vk == 'F') {
        searchFocused_ = true;
        searchCaretIndex_ = searchQueryWide_.size();
        caretVisible_ = true;
        SetTimer(hwnd_, kTimerCaret, 530, nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (ctrlDown && vk == 'V' &&
        (searchFocused_ || settings_.behavior.searchActivation == core::SearchActivationMode::TypeToSearch)) {
        std::wstring clip = ReadClipboardText(hwnd_);
        clip.erase(std::remove_if(clip.begin(), clip.end(),
                                  [](wchar_t c) { return c == L'\r' || c == L'\n'; }),
                   clip.end());
        if (!clip.empty()) {
            searchFocused_ = true;
            const std::size_t pos = std::min(searchCaretIndex_, searchQueryWide_.size());
            searchQueryWide_.insert(pos, clip);
            searchCaretIndex_ = pos + clip.size();
            scrollOffsetDip_ = 0.0f;
            RebuildDisplayedItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    if (ctrlDown && vk == VK_BACK) {
        if (!searchQueryWide_.empty()) {
            searchQueryWide_.clear();
            searchCaretIndex_ = 0;
            scrollOffsetDip_ = 0.0f;
            RebuildDisplayedItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    if (vk == VK_ESCAPE) {
        if (!searchQueryWide_.empty()) {
            searchQueryWide_.clear();
            searchCaretIndex_ = 0;
            scrollOffsetDip_ = 0.0f;
            RebuildDisplayedItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else {
            HideWindow();
        }
        return;
    }

    if (vk == VK_RETURN) {
        if (!displayedItems_.empty()) {
            const std::size_t targetIdx =
                (selectedTileIdx_ >= 0 &&
                 static_cast<std::size_t>(selectedTileIdx_) < displayedItems_.size())
                    ? static_cast<std::size_t>(selectedTileIdx_)
                    : 0u;
            LaunchDisplayedIndex(targetIdx);
        }
        return;
    }

    if (vk == VK_BACK &&
        (searchFocused_ || settings_.behavior.searchActivation == core::SearchActivationMode::TypeToSearch)) {
        if (searchCaretIndex_ > 0 && !searchQueryWide_.empty()) {
            const std::size_t pos = std::min(searchCaretIndex_, searchQueryWide_.size());
            searchQueryWide_.erase(pos - 1, 1);
            searchCaretIndex_ = pos - 1;
            scrollOffsetDip_ = 0.0f;
            RebuildDisplayedItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    if (vk == VK_DELETE && searchFocused_) {
        if (searchCaretIndex_ < searchQueryWide_.size()) {
            searchQueryWide_.erase(searchCaretIndex_, 1);
            scrollOffsetDip_ = 0.0f;
            RebuildDisplayedItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    if (vk == VK_HOME && searchFocused_) {
        searchCaretIndex_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (vk == VK_END && searchFocused_) {
        searchCaretIndex_ = searchQueryWide_.size();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (!displayedItems_.empty() &&
        (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN)) {
        const int total = static_cast<int>(displayedItems_.size());
        const int cols = std::max(1, layout_.gridColumns);
        int cur = (selectedTileIdx_ >= 0 && selectedTileIdx_ < total) ? selectedTileIdx_ : 0;

        if (vk == VK_LEFT) {
            cur = std::max(0, cur - 1);
        } else if (vk == VK_RIGHT) {
            cur = std::min(total - 1, cur + 1);
        } else if (vk == VK_UP) {
            if (cur - cols >= 0) {
                cur -= cols;
            }
        } else if (vk == VK_DOWN) {
            if (cur + cols < total) {
                cur += cols;
            }
        }
        selectedTileIdx_ = cur;
        EnsureTileVisible(static_cast<std::size_t>(cur));
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnChar(wchar_t ch) {
    if (ch < 0x20 || ch == 0x7F) {
        return;
    }
    if (!searchFocused_) {
        if (settings_.behavior.searchActivation != core::SearchActivationMode::TypeToSearch) {
            return;
        }
        searchFocused_ = true;
        searchCaretIndex_ = searchQueryWide_.size();
        caretVisible_ = true;
        SetTimer(hwnd_, kTimerCaret, 530, nullptr);
    }

    const std::size_t pos = std::min(searchCaretIndex_, searchQueryWide_.size());
    searchQueryWide_.insert(pos, 1, ch);
    searchCaretIndex_ = pos + 1;
    scrollOffsetDip_ = 0.0f;
    RebuildDisplayedItems();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnImeStartComposition() {
    if (!searchFocused_ &&
        settings_.behavior.searchActivation == core::SearchActivationMode::TypeToSearch) {
        searchFocused_ = true;
        searchCaretIndex_ = searchQueryWide_.size();
    }
    UpdateImeCandidateWindowPosition();
}

void MainWindow::OnImeComposition(LPARAM lParam) {
    if ((lParam & GCS_COMPSTR) != 0) {
        HIMC himc = ImmGetContext(hwnd_);
        if (himc != nullptr) {
            const LONG byteLen = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
            if (byteLen > 0) {
                std::wstring buf(static_cast<std::size_t>(byteLen) / sizeof(wchar_t), L'\0');
                ImmGetCompositionStringW(himc, GCS_COMPSTR, buf.data(), static_cast<DWORD>(byteLen));
                imeCompWide_ = std::move(buf);
            } else {
                imeCompWide_.clear();
            }
            ImmReleaseContext(hwnd_, himc);
            UpdateImeCandidateWindowPosition();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
}

void MainWindow::OnHotKeyTriggered() {
    if (!IsWindowVisible(hwnd_) && settings_.activation.ignoreWhenFullScreen &&
        platform::IsForegroundFullScreen(hwnd_)) {
        return;
    }
    ToggleWindow();
}

void MainWindow::OnTrayIconEvent(LPARAM lParam) {
    const UINT mouseMsg = static_cast<UINT>(lParam);
    if (mouseMsg == WM_LBUTTONUP || mouseMsg == WM_LBUTTONDBLCLK) {
        ToggleWindow();
    } else if (mouseMsg == WM_RBUTTONUP || mouseMsg == WM_CONTEXTMENU) {
        inTrayMenu_ = true;
        const auto cmd = trayIcon_.ShowContextMenu(hwnd_, platform::IsAutoStartEnabled());
        inTrayMenu_ = false;
        if (!cmd.has_value()) {
            return;
        }
        switch (*cmd) {
        case platform::TrayCommand::ToggleWindow:
            ToggleWindow();
            break;
        case platform::TrayCommand::OpenSettings:
            OpenSettingsWindow();
            break;
        case platform::TrayCommand::SyncCatalog:
            SyncAllLinkedFolders(true);
            break;
        case platform::TrayCommand::OpenDataDir:
            (void)platform::OpenDirectoryInExplorer(dataRoot_);
            break;
        case platform::TrayCommand::ToggleAutoStart: {
            const bool next = !platform::IsAutoStartEnabled();
            if (platform::SetAutoStartEnabled(next)) {
                settings_.behavior.autoStart = next;
                ScheduleSaveSettings();
            }
            break;
        }
        case platform::TrayCommand::Exit:
            if (hwnd_ != nullptr) {
                DestroyWindow(hwnd_);
            }
            break;
        }
    }
}

void MainWindow::ApplyBackdropAndPalette() {
    const bool isDark = platform::ResolveIsDark(settings_.appearance.theme);
    nativeBackdropActive_ = platform::ApplyWindowBackdrop(
        hwnd_, settings_.appearance.backdrop, isDark, settings_.appearance.cornerRadius);
    palette_ = BuildThemePalette(settings_.appearance, isDark, nativeBackdropActive_);
}

void MainWindow::RebuildDisplayedItems() {
    displayedItems_.clear();
    if (!searchQueryWide_.empty()) {
        const std::string queryUtf8 = core::utf8::WideToUtf8(searchQueryWide_);
        const auto matches = searchEngine_.Search(queryUtf8, 64);
        for (const auto& match : matches) {
            for (const auto& grp : catalog_.groups) {
                if (grp.id != match.groupId) {
                    continue;
                }
                for (const auto& item : grp.items) {
                    if (item.id == match.itemId) {
                        displayedItems_.push_back(DisplayedItemView{&item, grp.name});
                        break;
                    }
                }
                break;
            }
        }
    } else if (activeGroupIndex_ < catalog_.groups.size()) {
        const auto& grp = catalog_.groups[activeGroupIndex_];
        displayedItems_.reserve(grp.items.size());
        for (const auto& item : grp.items) {
            displayedItems_.push_back(DisplayedItemView{&item, grp.name});
        }
    }

    if (displayedItems_.empty()) {
        selectedTileIdx_ = -1;
    } else if (!searchQueryWide_.empty()) {
        selectedTileIdx_ = 0;
    } else if (selectedTileIdx_ >= static_cast<int>(displayedItems_.size())) {
        selectedTileIdx_ = 0;
    }

    ResetDefaultStatus();
}

void MainWindow::SelectGroup(std::size_t index, bool persistSelection) {
    if (index >= catalog_.groups.size()) {
        return;
    }
    const bool clearedSearch = !searchQueryWide_.empty();
    activeGroupIndex_ = index;
    searchQueryWide_.clear();
    searchCaretIndex_ = 0;
    scrollOffsetDip_ = 0.0f;
    selectedTileIdx_ = -1;
    hoveredTileIdx_ = -1;

    if (persistSelection) {
        settings_.state.lastSelectedGroupId = catalog_.groups[index].id;
        ScheduleSaveSettings();
    }
    if (clearedSearch) {
        imeCompWide_.clear();
    }
    RebuildDisplayedItems();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::SwitchGroupByDelta(int delta) {
    if (catalog_.groups.size() <= 1 || delta == 0) {
        return;
    }
    const int count = static_cast<int>(catalog_.groups.size());
    const int cur = static_cast<int>(activeGroupIndex_);
    const int next = ((cur + delta) % count + count) % count;
    SelectGroup(static_cast<std::size_t>(next), true);
}

void MainWindow::LaunchDisplayedIndex(std::size_t index) {
    if (index >= displayedItems_.size() || displayedItems_[index].item == nullptr) {
        return;
    }
    const core::LaunchItem* targetPtr = displayedItems_[index].item;
    core::LaunchItem* mutableItem = nullptr;

    for (auto& grp : catalog_.groups) {
        for (auto& it : grp.items) {
            if (&it == targetPtr) {
                mutableItem = &it;
                break;
            }
        }
        if (mutableItem != nullptr) {
            break;
        }
    }
    if (mutableItem == nullptr) {
        return;
    }

    const platform::LaunchResult lr = platform::LaunchCatalogItem(*mutableItem);
    if (!lr.ok) {
        statusText_ = "\u542F\u52A8\u5931\u8D25: " + lr.error;
        if (logger_ != nullptr) {
            logger_->Warn("Launch failed for '" + mutableItem->name + "': " + lr.error);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    mutableItem->launchCount += 1;
    mutableItem->lastLaunchedAt = CurrentUtcIsoTimestamp();
    if (settings_.behavior.itemSort == core::SortOrder::LaunchCountAsc ||
        settings_.behavior.itemSort == core::SortOrder::LaunchCountDesc) {
        for (auto& grp : catalog_.groups) {
            core::SortItems(grp.items, settings_.behavior.itemSort);
        }
    }
    searchEngine_.Rebuild(catalog_);
    RebuildDisplayedItems();
    ScheduleSaveCatalog();

    statusText_ = "\u5DF2\u542F\u52A8: " + mutableItem->name;
    if (settings_.window.hidePolicy == core::HidePolicy::AfterLaunch) {
        HideWindow();
    } else {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::SyncAllLinkedFolders(bool showFeedback) {
    std::size_t totalAdded = 0;
    std::size_t totalRemoved = 0;
    for (auto& grp : catalog_.groups) {
        if (grp.kind == core::GroupKind::LinkedFolder) {
            const auto res = core::SyncLinkedFolderGroup(grp);
            totalAdded += res.added;
            totalRemoved += res.removed;
        }
        core::SortItems(grp.items, settings_.behavior.itemSort);
    }
    core::SortGroups(catalog_.groups, settings_.behavior.groupSort);
    searchEngine_.Rebuild(catalog_);
    iconCache_.ClearMemoryCache();
    RebuildDisplayedItems();

    if (totalAdded > 0 || totalRemoved > 0) {
        ScheduleSaveCatalog();
    }
    if (showFeedback) {
        statusText_ = "\u5DF2\u540C\u6B65\u5173\u8054\u76EE\u5F55 (+" + std::to_string(totalAdded) +
                      " / -" + std::to_string(totalRemoved) + ")";
    }
    if (hwnd_ != nullptr && IsWindowVisible(hwnd_)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::UpdateImeCandidateWindowPosition() {
    if (hwnd_ == nullptr) {
        return;
    }
    HIMC himc = ImmGetContext(hwnd_);
    if (himc == nullptr) {
        return;
    }
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = core::DipToPhysicalPx(layout_.searchCaretX, dpi_);
    cf.ptCurrentPos.y =
        core::DipToPhysicalPx(layout_.searchCaretY + layout_.searchCaretHeight + 4.0f, dpi_);
    ImmSetCompositionWindow(himc, &cf);
    ImmReleaseContext(hwnd_, himc);
}

void MainWindow::EnsureTileVisible(std::size_t tileIndex) {
    if (tileIndex >= layout_.tileRects.size()) {
        return;
    }
    const RectDip& tile = layout_.tileRects[tileIndex];
    if (tile.top < layout_.gridViewportRect.top) {
        scrollOffsetDip_ = std::max(
            0.0f, scrollOffsetDip_ - (layout_.gridViewportRect.top - tile.top));
    } else if (tile.bottom > layout_.gridViewportRect.bottom) {
        scrollOffsetDip_ = std::min(
            layout_.maxScrollDip,
            scrollOffsetDip_ + (tile.bottom - layout_.gridViewportRect.bottom));
    }
}

void MainWindow::ScheduleSaveSettings() {
    if (suppressDiskPersist_) {
        return;
    }
    debouncedWriter_.Schedule(dataRoot_ / L"settings.json", storage::SerializeSettings(settings_),
                              std::chrono::milliseconds(300));
}

void MainWindow::ScheduleSaveCatalog() {
    if (suppressDiskPersist_) {
        return;
    }
    debouncedWriter_.Schedule(dataRoot_ / L"catalog.json", storage::SerializeCatalog(catalog_),
                              std::chrono::milliseconds(300));
}

void MainWindow::ResetDefaultStatus() {
    const std::string hkBadge =
        !hotKeyError_.empty()
            ? "\u70ED\u952E\u51B2\u7A81: " + settings_.activation.hotKey
            : (settings_.activation.enableHotKey ? ("\u70ED\u952E: " + settings_.activation.hotKey)
                                                 : std::string("\u70ED\u952E\u5DF2\u5173\u95ED"));

    if (!searchQueryWide_.empty()) {
        statusText_ = "\u672C\u5730\u641C\u7D22\u547D\u4E2D: " +
                      std::to_string(displayedItems_.size()) + " \u9879  |  " + hkBadge +
                      "  |  Esc \u6E05\u7A7A";
    } else if (activeGroupIndex_ < catalog_.groups.size()) {
        statusText_ = catalog_.groups[activeGroupIndex_].name + " (" +
                      std::to_string(displayedItems_.size()) + " \u9879)  |  " + hkBadge;
    } else {
        statusText_ = hkBadge;
    }
}

void MainWindow::RefreshHooksAndTray() {
    if (!hooksRegistered_ || hwnd_ == nullptr) {
        return;
    }
    if (settings_.activation.enableMiddleMouse) {
        (void)mouseHook_.Restart();
    }
    if (settings_.activation.enableHotKey && !hotKeyManager_.IsRegistered()) {
        std::string err;
        if (hotKeyManager_.Register(hwnd_, settings_.activation.hotKey, err)) {
            hotKeyError_.clear();
        } else {
            hotKeyError_ = err;
        }
    }
    if (settings_.state.showTrayIcon) {
        trayIcon_.Show(hwnd_, kMsgTrayIcon, L"GeekDeskCpp");
    }
    if (logger_ != nullptr) {
        logger_->Info("System resume/unlock hook & tray refresh completed");
    }
}

float MainWindow::PxToDip(int px) const noexcept {
    const float safeDpi = static_cast<float>(dpi_ > 0 ? dpi_ : 96u);
    return static_cast<float>(px) * (96.0f / safeDpi);
}

int MainWindow::HitTestGroup(float xDip, float yDip) const noexcept {
    for (std::size_t i = 0; i < layout_.groupRects.size(); ++i) {
        if (layout_.groupRects[i].Contains(xDip, yDip)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int MainWindow::HitTestTile(float xDip, float yDip) const noexcept {
    if (!layout_.gridViewportRect.Contains(xDip, yDip)) {
        return -1;
    }
    for (std::size_t i = 0; i < layout_.tileRects.size(); ++i) {
        if (layout_.tileRects[i].Contains(xDip, yDip)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

HeaderButton MainWindow::HitTestHeaderButton(float xDip, float yDip) const noexcept {
    for (int i = 0; i < 4; ++i) {
        if (layout_.headerButtonRects[static_cast<std::size_t>(i)].Contains(xDip, yDip)) {
            return static_cast<HeaderButton>(i);
        }
    }
    return HeaderButton::None;
}

void MainWindow::OnFolderChangedNotifications() {
    std::vector<std::string> groupIds;
    {
        std::lock_guard<std::mutex> lock(folderQueueMutex_);
        groupIds.swap(pendingChangedGroups_);
    }
    if (groupIds.empty()) {
        return;
    }

    std::size_t addedTotal = 0;
    std::size_t removedTotal = 0;
    for (const auto& gid : groupIds) {
        for (auto& grp : catalog_.groups) {
            if (grp.id == gid && grp.kind == core::GroupKind::LinkedFolder) {
                const auto res = core::SyncLinkedFolderGroup(grp);
                addedTotal += res.added;
                removedTotal += res.removed;
                core::SortItems(grp.items, settings_.behavior.itemSort);
                break;
            }
        }
    }

    if (addedTotal > 0 || removedTotal > 0) {
        searchEngine_.Rebuild(catalog_);
        RebuildDisplayedItems();
        ScheduleSaveCatalog();
        statusText_ = "\u76EE\u5F55\u5DF2\u81EA\u52A8\u540C\u6B65 (+" + std::to_string(addedTotal) +
                      " / -" + std::to_string(removedTotal) + ")";
        if (hwnd_ != nullptr && IsWindowVisible(hwnd_)) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
}

void MainWindow::RestartFolderWatchers() {
    std::vector<platform::WatchedFolderSpec> specs;
    for (const auto& grp : catalog_.groups) {
        if (grp.kind == core::GroupKind::LinkedFolder && grp.linkedFolderPath.has_value() &&
            !grp.linkedFolderPath->empty()) {
            specs.push_back(platform::WatchedFolderSpec{
                grp.id, core::utf8::PathFromUtf8(*grp.linkedFolderPath)});
        }
    }
    folderWatcher_.Start(
        std::move(specs),
        [this](const std::string& groupId) {
            {
                std::lock_guard<std::mutex> lock(folderQueueMutex_);
                if (std::find(pendingChangedGroups_.begin(), pendingChangedGroups_.end(),
                              groupId) == pendingChangedGroups_.end()) {
                    pendingChangedGroups_.push_back(groupId);
                }
            }
            if (hwnd_ != nullptr) {
                PostMessageW(hwnd_, kMsgFolderChanged, 0, 0);
            }
        },
        std::chrono::milliseconds(400));
}

void MainWindow::OpenSettingsWindow() {
    debouncedWriter_.Flush();

    SettingsCallbacks cb;
    cb.onSettingsChanged = [this](const core::AppSettings& next) { ApplySettingsLive(next); };
    cb.onBackupRestored = [this](const core::AppSettings& nextSettings,
                                 const core::CatalogDocument& nextCatalog,
                                 const std::filesystem::path& rollbackPath) {
        ApplyBackupRestored(nextSettings, nextCatalog, rollbackPath);
    };
    cb.onQueryStats = [this]() -> std::pair<std::size_t, std::size_t> {
        std::size_t totalItems = 0;
        for (const auto& grp : catalog_.groups) {
            totalItems += grp.items.size();
        }
        return {catalog_.groups.size(), totalItems};
    };
    cb.onClosed = [this]() {
        if (hwnd_ != nullptr && IsWindowVisible(hwnd_)) {
            SetForegroundWindow(hwnd_);
        }
    };

    settingsWindow_.OpenOrActivate(GetModuleHandleW(nullptr), hwnd_, dataRoot_, settings_,
                                   std::move(cb));
}

void MainWindow::ApplySettingsLive(const core::AppSettings& next) {
    const core::AppSettings prev = settings_;
    settings_ = next;

    if (prev.appearance.wallpaperFileName != next.appearance.wallpaperFileName) {
        renderer_.InvalidateWallpaperCache();
    }
    if (prev.activation.enableHotKey != next.activation.enableHotKey ||
        prev.activation.hotKey != next.activation.hotKey) {
        hotKeyManager_.Unregister();
        hotKeyError_.clear();
        if (hooksRegistered_ && next.activation.enableHotKey) {
            std::string err;
            if (!hotKeyManager_.Register(hwnd_, next.activation.hotKey, err)) {
                hotKeyError_ = err;
            }
        }
    }
    if (hooksRegistered_ && prev.activation.enableMiddleMouse != next.activation.enableMiddleMouse) {
        if (next.activation.enableMiddleMouse) {
            (void)mouseHook_.Start(hwnd_, kMsgMiddleClick);
        } else {
            mouseHook_.Stop();
        }
    }
    if (prev.behavior.autoStart != next.behavior.autoStart) {
        (void)platform::SetAutoStartEnabled(next.behavior.autoStart);
    }
    if (hooksRegistered_ && prev.state.showTrayIcon != next.state.showTrayIcon) {
        if (next.state.showTrayIcon) {
            trayIcon_.Show(hwnd_, kMsgTrayIcon, L"GeekDeskCpp");
        } else {
            trayIcon_.Hide();
        }
    }
    if (prev.behavior.groupSort != next.behavior.groupSort) {
        const std::string curId =
            (activeGroupIndex_ < catalog_.groups.size()) ? catalog_.groups[activeGroupIndex_].id
                                                         : std::string{};
        core::SortGroups(catalog_.groups, next.behavior.groupSort);
        if (!curId.empty()) {
            for (std::size_t i = 0; i < catalog_.groups.size(); ++i) {
                if (catalog_.groups[i].id == curId) {
                    activeGroupIndex_ = i;
                    break;
                }
            }
        }
    }
    if (prev.behavior.itemSort != next.behavior.itemSort) {
        for (auto& grp : catalog_.groups) {
            core::SortItems(grp.items, next.behavior.itemSort);
        }
        searchEngine_.Rebuild(catalog_);
    }

    ApplyBackdropAndPalette();
    RebuildDisplayedItems();
    ScheduleSaveSettings();
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::ApplyBackupRestored(const core::AppSettings& nextSettings,
                                     const core::CatalogDocument& nextCatalog,
                                     const std::filesystem::path& rollbackPath) {
    catalog_ = nextCatalog;
    if (catalog_.groups.empty()) {
        catalog_ = core::CatalogDocument::CreateDefault();
    }
    activeGroupIndex_ = 0;
    if (nextSettings.state.lastSelectedGroupId.has_value()) {
        for (std::size_t i = 0; i < catalog_.groups.size(); ++i) {
            if (catalog_.groups[i].id == *nextSettings.state.lastSelectedGroupId) {
                activeGroupIndex_ = i;
                break;
            }
        }
    }

    searchQueryWide_.clear();
    searchCaretIndex_ = 0;
    scrollOffsetDip_ = 0.0f;
    iconCache_.ClearMemoryCache();
    renderer_.InvalidateWallpaperCache();
    searchEngine_.Rebuild(catalog_);
    RestartFolderWatchers();
    ApplySettingsLive(nextSettings);

    if (!rollbackPath.empty()) {
        statusText_ = "\u5DF2\u4ECE\u5907\u4EFD\u6062\u590D (\u5DF2\u521B\u5EFA pre-restore \u56DE\u6EDA\u5FEB\u7167)";
    } else {
        statusText_ = "\u5DF2\u4ECE\u5907\u4EFD\u6062\u590D\u914D\u7F6E";
    }
    if (logger_ != nullptr) {
        logger_->Info("Backup restored cleanly; groups=" + std::to_string(catalog_.groups.size()));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::HandleExternalDropHover(POINT screenPt, bool& outAccepted) {
    outAccepted = false;
    int nextDropGrp = -1;
    bool nextDropGrid = false;

    POINT clientPt = screenPt;
    ScreenToClient(hwnd_, &clientPt);
    const float xDip = PxToDip(clientPt.x);
    const float yDip = PxToDip(clientPt.y);

    if (const int gIdx = HitTestGroup(xDip, yDip);
        gIdx >= 0 && static_cast<std::size_t>(gIdx) < catalog_.groups.size()) {
        if (catalog_.groups[static_cast<std::size_t>(gIdx)].kind == core::GroupKind::Manual) {
            nextDropGrp = gIdx;
            outAccepted = true;
        }
    } else if (layout_.contentCardRect.Contains(xDip, yDip) &&
               activeGroupIndex_ < catalog_.groups.size() &&
               catalog_.groups[activeGroupIndex_].kind == core::GroupKind::Manual) {
        nextDropGrid = true;
        outAccepted = true;
    }

    if (nextDropGrp != dropHighlightGroupIdx_ || nextDropGrid != dropHighlightGrid_) {
        dropHighlightGroupIdx_ = nextDropGrp;
        dropHighlightGrid_ = nextDropGrid;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::HandleExternalDropFiles(POINT screenPt,
                                         const std::vector<std::filesystem::path>& paths) {
    POINT clientPt = screenPt;
    ScreenToClient(hwnd_, &clientPt);
    const float xDip = PxToDip(clientPt.x);
    const float yDip = PxToDip(clientPt.y);

    std::size_t targetGroupIdx = activeGroupIndex_;
    if (const int gIdx = HitTestGroup(xDip, yDip);
        gIdx >= 0 && static_cast<std::size_t>(gIdx) < catalog_.groups.size()) {
        targetGroupIdx = static_cast<std::size_t>(gIdx);
    } else if (!layout_.contentCardRect.Contains(xDip, yDip)) {
        return;
    }

    if (targetGroupIdx >= catalog_.groups.size() ||
        catalog_.groups[targetGroupIdx].kind != core::GroupKind::Manual) {
        statusText_ = "\u5173\u8054\u6587\u4EF6\u5939\u5206\u7EC4\u4E0D\u652F\u6301\u62D6\u5165\u6DFB\u52A0";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    std::size_t added = 0;
    for (const auto& p : paths) {
        core::LaunchItem item = BuildLaunchItemFromPath(p);
        if (core::AddItemToManualGroup(catalog_.groups[targetGroupIdx], std::move(item), true)) {
            ++added;
        }
    }

    if (added > 0) {
        core::SortItems(catalog_.groups[targetGroupIdx].items, settings_.behavior.itemSort);
        if (targetGroupIdx != activeGroupIndex_) {
            activeGroupIndex_ = targetGroupIdx;
            settings_.state.lastSelectedGroupId = catalog_.groups[targetGroupIdx].id;
            ScheduleSaveSettings();
        }
        searchEngine_.Rebuild(catalog_);
        RebuildDisplayedItems();
        ScheduleSaveCatalog();
        statusText_ = "\u5DF2\u62D6\u5165\u6DFB\u52A0 " + std::to_string(added) + " \u4E2A\u6761\u76EE";
    } else {
        statusText_ = "\u62D6\u5165\u7684\u6761\u76EE\u5DF2\u5B58\u5728";
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CancelInternalDrag() {
    if (dragCandidate_ != DragKind::None || dragActive_) {
        dragCandidate_ = DragKind::None;
        dragActive_ = false;
        dragStartIndex_ = -1;
        dragTargetTileIdx_ = -1;
        dragTargetGroupIdx_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool MainWindow::FindCatalogIndices(const core::LaunchItem* ptr,
                                    std::size_t& outGroupIdx,
                                    std::size_t& outItemIdx) const noexcept {
    if (ptr == nullptr) {
        return false;
    }
    for (std::size_t gi = 0; gi < catalog_.groups.size(); ++gi) {
        const auto& items = catalog_.groups[gi].items;
        for (std::size_t ii = 0; ii < items.size(); ++ii) {
            if (&items[ii] == ptr) {
                outGroupIdx = gi;
                outItemIdx = ii;
                return true;
            }
        }
    }
    return false;
}

} // namespace geekdesk::ui
