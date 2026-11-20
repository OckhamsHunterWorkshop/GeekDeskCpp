#include "ui/settings_window.hpp"

#include "core/hotkey_parser.hpp"
#include "core/placement_math.hpp"
#include "core/utf8.hpp"
#include "platform/backdrop.hpp"
#include "platform/dpi_helper.hpp"
#include "platform/file_dialogs.hpp"
#include "platform/shell_launcher.hpp"
#include "resource.h"
#include "storage/backup_manager.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace geekdesk::ui {

namespace {

constexpr const wchar_t* kSettingsClassName = L"GeekDeskCppSettingsWindow";

int ScalePx(int dip, std::uint32_t dpi) noexcept {
    return core::DipToPhysicalPx(static_cast<double>(dip), dpi);
}

std::wstring ReadCtrlText(HWND ctrl) {
    const int len = GetWindowTextLengthW(ctrl);
    if (len <= 0) {
        return {};
    }
    std::wstring buf(static_cast<std::size_t>(len) + 1, L'\0');
    GetWindowTextW(ctrl, buf.data(), len + 1);
    buf.resize(static_cast<std::size_t>(len));
    return buf;
}

std::string TodayCompactDate() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    return std::string(buf);
}

int SortToIdx(core::SortOrder order) noexcept {
    switch (order) {
    case core::SortOrder::Custom:
        return 0;
    case core::SortOrder::LaunchCountAsc:
        return 1;
    case core::SortOrder::LaunchCountDesc:
        return 2;
    case core::SortOrder::NameAsc:
        return 3;
    case core::SortOrder::NameDesc:
        return 4;
    }
    return 0;
}

core::SortOrder IdxToSort(int idx) noexcept {
    switch (idx) {
    case 0:
        return core::SortOrder::Custom;
    case 1:
        return core::SortOrder::LaunchCountAsc;
    case 2:
        return core::SortOrder::LaunchCountDesc;
    case 3:
        return core::SortOrder::NameAsc;
    case 4:
        return core::SortOrder::NameDesc;
    default:
        return core::SortOrder::Custom;
    }
}

void AddSortItems(HWND combo) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u81EA\u5B9A\u4E49\u987A\u5E8F (Custom)"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u542F\u52A8\u6B21\u6570\u5347\u5E8F"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u542F\u52A8\u6B21\u6570\u964D\u5E8F"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u540D\u79F0 A-Z \u5347\u5E8F"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u540D\u79F0 Z-A \u964D\u5E8F"));
}

} // namespace

SettingsWindow::~SettingsWindow() {
    Close();
}

void SettingsWindow::Close() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (bgBrush_ != nullptr) {
        DeleteObject(bgBrush_);
        bgBrush_ = nullptr;
    }
    if (editBrush_ != nullptr) {
        DeleteObject(editBrush_);
        editBrush_ = nullptr;
    }
}

void SettingsWindow::OpenOrActivate(HINSTANCE hInstance,
                                    HWND ownerHwnd,
                                    std::filesystem::path dataRoot,
                                    const core::AppSettings& currentSettings,
                                    SettingsCallbacks callbacks) {
    dataRoot_ = std::move(dataRoot);
    working_ = currentSettings;
    callbacks_ = std::move(callbacks);
    ownerHwnd_ = ownerHwnd;

    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
        SyncFromExternalSettings(currentSettings);
        RefreshBackupListAndStats();
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::StaticWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSettingsClassName;
    RegisterClassExW(&wc);

    const auto metrics = (ownerHwnd != nullptr)
                             ? platform::GetMonitorMetricsForWindow(ownerHwnd)
                             : platform::GetMonitorMetricsForPoint(POINT{0, 0});
    dpi_ = metrics.dpi;
    const int winW = ScalePx(580, dpi_);
    const int winH = ScalePx(620, dpi_);
    const auto pos = core::ComputeCenteredPosition(winW, winH, metrics.workArea);

    hwnd_ = CreateWindowExW(0, kSettingsClassName,
                            L"GeekDeskCpp \u8BBE\u7F6E\u4E0E\u5907\u4EFD\u6062\u590D\u4E2D\u5FC3",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, pos.x, pos.y,
                            winW, winH, ownerHwnd, nullptr, hInstance, this);
    if (hwnd_ == nullptr) {
        return;
    }

    RebuildThemeBrushes();
    BuildControls();
    PopulateFromSettings();
    RefreshBackupListAndStats();
    SwitchTab(activeTab_);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::SyncFromExternalSettings(const core::AppSettings& settings) {
    working_ = settings;
    if (hwnd_ != nullptr) {
        RebuildThemeBrushes();
        PopulateFromSettings();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void SettingsWindow::RebuildThemeBrushes() {
    isDark_ = platform::ResolveIsDark(working_.appearance.theme);
    if (hwnd_ != nullptr) {
        const BOOL darkFlag = isDark_ ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFlag, sizeof(darkFlag));
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    if (bgBrush_ != nullptr) {
        DeleteObject(bgBrush_);
        bgBrush_ = nullptr;
    }
    if (editBrush_ != nullptr) {
        DeleteObject(editBrush_);
        editBrush_ = nullptr;
    }
    if (isDark_) {
        bgColor_ = RGB(28, 30, 38);
        editColor_ = RGB(42, 45, 56);
        textColor_ = RGB(242, 244, 248);
    } else {
        bgColor_ = RGB(246, 247, 250);
        editColor_ = RGB(255, 255, 255);
        textColor_ = RGB(20, 24, 34);
    }
    bgBrush_ = CreateSolidBrush(bgColor_);
    editBrush_ = CreateSolidBrush(editColor_);
    ApplyChildThemes();
}

void SettingsWindow::ApplyChildThemes() {
    if (hwnd_ == nullptr) {
        return;
    }
    struct ThemeCtx {
        bool dark;
    } ctx{isDark_};
    EnumChildWindows(
        hwnd_,
        [](HWND ch, LPARAM lp) -> BOOL {
            const auto* c = reinterpret_cast<const ThemeCtx*>(lp);
            wchar_t cls[32]{};
            GetClassNameW(ch, cls, static_cast<int>(std::size(cls)));
            const std::wstring_view clsView(cls);
            if (clsView == L"ComboBox") {
                SetWindowTheme(ch, c->dark ? L"DarkMode_CFD" : L"Explorer", nullptr);
            } else {
                SetWindowTheme(ch, c->dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
            }
            InvalidateRect(ch, nullptr, TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
}

LRESULT CALLBACK SettingsWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        if (bgBrush_ != nullptr) {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            FillRect(reinterpret_cast<HDC>(wParam), &rc, bgBrush_);
            return 1;
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        if (hdc != nullptr) {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            const int pad = ScalePx(12, dpi_);
            const int lineY = pad + ScalePx(36, dpi_);
            const RECT lineRect{pad, lineY, rc.right - pad, lineY + 1};
            HBRUSH div = CreateSolidBrush(isDark_ ? RGB(56, 60, 74) : RGB(214, 218, 228));
            FillRect(hdc, &lineRect, div);
            DeleteObject(div);
        }
        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (dis != nullptr && (dis->CtlID == kIdTab0 || dis->CtlID == kIdTab1 || dis->CtlID == kIdTab2)) {
            const int idx = static_cast<int>(dis->CtlID - kIdTab0);
            const bool active = (idx == activeTab_);
            const COLORREF fillClr =
                active ? (isDark_ ? RGB(42, 46, 58) : RGB(255, 255, 255)) : bgColor_;
            HBRUSH fillBr = CreateSolidBrush(fillClr);
            FillRect(dis->hDC, &dis->rcItem, fillBr);
            DeleteObject(fillBr);

            if (active) {
                RECT bar = dis->rcItem;
                bar.top = bar.bottom - ScalePx(3, dpi_);
                HBRUSH accBr = CreateSolidBrush(RGB(59, 130, 246));
                FillRect(dis->hDC, &bar, accBr);
                DeleteObject(accBr);
            }

            wchar_t text[64]{};
            GetWindowTextW(dis->hwndItem, text, static_cast<int>(std::size(text)));
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, active ? textColor_
                                          : (isDark_ ? RGB(164, 170, 184) : RGB(96, 106, 124)));
            RECT txtRc = dis->rcItem;
            DrawTextW(dis->hDC, text, -1, &txtRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, textColor_);
        SetBkColor(hdc, bgColor_);
        return reinterpret_cast<LRESULT>(bgBrush_);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, textColor_);
        SetBkColor(hdc, editColor_);
        return reinterpret_cast<LRESULT>(editBrush_);
    }

    case WM_HSCROLL: {
        if (populating_) {
            return 0;
        }
        HWND src = reinterpret_cast<HWND>(lParam);
        if (src == bgOpacitySlider_) {
            const int v = static_cast<int>(SendMessageW(bgOpacitySlider_, TBM_GETPOS, 0, 0));
            working_.appearance.backgroundOpacity = static_cast<double>(v) / 100.0;
            RefreshSliderLabels();
            EmitSettingsChanged();
        } else if (src == cardOpacitySlider_) {
            const int v = static_cast<int>(SendMessageW(cardOpacitySlider_, TBM_GETPOS, 0, 0));
            working_.appearance.cardOpacity = static_cast<double>(v) / 100.0;
            RefreshSliderLabels();
            EmitSettingsChanged();
        } else if (src == iconSizeSlider_) {
            const int v = static_cast<int>(SendMessageW(iconSizeSlider_, TBM_GETPOS, 0, 0));
            working_.appearance.iconSize = v;
            RefreshSliderLabels();
            EmitSettingsChanged();
        } else if (src == cornerSlider_) {
            const int v = static_cast<int>(SendMessageW(cornerSlider_, TBM_GETPOS, 0, 0));
            working_.appearance.cornerRadius = v;
            RefreshSliderLabels();
            EmitSettingsChanged();
        } else if (src == fontSizeSlider_) {
            const int v = static_cast<int>(SendMessageW(fontSizeSlider_, TBM_GETPOS, 0, 0));
            working_.appearance.fontSize = v;
            RefreshSliderLabels();
            EmitSettingsChanged();
        }
        return 0;
    }

    case WM_COMMAND: {
        if (populating_) {
            return 0;
        }
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == kIdCloseBtn) {
            DestroyWindow(hwnd_);
            return 0;
        }
        if (id == kIdTab0 || id == kIdTab1 || id == kIdTab2) {
            SwitchTab(id - kIdTab0);
            return 0;
        }
        // Clicking the static label paired with a checkbox toggles the checkbox cleanly
        if (code == STN_CLICKED && id >= 5000 && id < 5100) {
            const int checkId = id - 1000;
            if (HWND cb = GetDlgItem(hwnd_, checkId); cb != nullptr) {
                const bool wasChecked = (SendMessageW(cb, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SendMessageW(cb, BM_SETCHECK, wasChecked ? BST_UNCHECKED : BST_CHECKED, 0);
                SendMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(checkId, BN_CLICKED),
                             reinterpret_cast<LPARAM>(cb));
            }
            return 0;
        }

        // Combos
        if (code == CBN_SELCHANGE) {
            if (id == kIdThemeCombo) {
                const int sel = static_cast<int>(SendMessageW(themeCombo_, CB_GETCURSEL, 0, 0));
                working_.appearance.theme = static_cast<core::ThemeMode>(std::clamp(sel, 0, 2));
                RebuildThemeBrushes();
                InvalidateRect(hwnd_, nullptr, TRUE);
                EmitSettingsChanged();
            } else if (id == kIdBackdropCombo) {
                const int sel = static_cast<int>(SendMessageW(backdropCombo_, CB_GETCURSEL, 0, 0));
                working_.appearance.backdrop =
                    static_cast<core::BackdropMode>(std::clamp(sel, 0, 4));
                EmitSettingsChanged();
            } else if (id == kIdFontFamilyCombo) {
                const int sel = static_cast<int>(SendMessageW(fontFamilyCombo_, CB_GETCURSEL, 0, 0));
                if (sel >= 0) {
                    wchar_t buf[128]{};
                    SendMessageW(fontFamilyCombo_, CB_GETLBTEXT, static_cast<WPARAM>(sel),
                                 reinterpret_cast<LPARAM>(buf));
                    const std::string fam = core::utf8::WideToUtf8(buf);
                    if (!fam.empty()) {
                        working_.appearance.fontFamily = fam;
                        EmitSettingsChanged();
                    }
                }
            } else if (id == kIdHidePolicyCombo) {
                const int sel = static_cast<int>(SendMessageW(hidePolicyCombo_, CB_GETCURSEL, 0, 0));
                working_.window.hidePolicy =
                    static_cast<core::HidePolicy>(std::clamp(sel + 1, 1, 3));
                EmitSettingsChanged();
            } else if (id == kIdSearchActCombo) {
                const int sel = static_cast<int>(SendMessageW(searchActCombo_, CB_GETCURSEL, 0, 0));
                working_.behavior.searchActivation =
                    static_cast<core::SearchActivationMode>(std::clamp(sel, 0, 1));
                EmitSettingsChanged();
            } else if (id == kIdGroupSortCombo) {
                const int sel = static_cast<int>(SendMessageW(groupSortCombo_, CB_GETCURSEL, 0, 0));
                working_.behavior.groupSort = IdxToSort(sel);
                EmitSettingsChanged();
            } else if (id == kIdItemSortCombo) {
                const int sel = static_cast<int>(SendMessageW(itemSortCombo_, CB_GETCURSEL, 0, 0));
                working_.behavior.itemSort = IdxToSort(sel);
                EmitSettingsChanged();
            }
            return 0;
        }

        // Checkboxes & Buttons
        if (code == BN_CLICKED) {
            auto checked = [](HWND cb) {
                return SendMessageW(cb, BM_GETCHECK, 0, 0) == BST_CHECKED;
            };
            switch (id) {
            case kIdShowTitleCheck:
                working_.appearance.showItemTitle = checked(showTitleCheck_);
                EmitSettingsChanged();
                break;
            case kIdPickWallpaperBtn: {
                if (auto chosen = platform::PickFileDialog(
                        hwnd_, L"\u9009\u62E9\u58C1\u7EB8\u56FE\u7247",
                        {{L"\u56FE\u7247\u6587\u4EF6 (*.png;*.jpg;*.jpeg;*.bmp)",
                          L"*.png;*.jpg;*.jpeg;*.bmp"}});
                    chosen.has_value()) {
                    const std::filesystem::path assetsDir = dataRoot_ / L"assets";
                    std::error_code ec;
                    std::filesystem::create_directories(assetsDir, ec);
                    const std::filesystem::path dest = assetsDir / chosen->filename();
                    if (*chosen != dest) {
                        std::filesystem::copy_file(
                            *chosen, dest, std::filesystem::copy_options::overwrite_existing, ec);
                    }
                    if (!ec) {
                        working_.appearance.wallpaperFileName =
                            core::utf8::PathToUtf8(chosen->filename());
                        working_.appearance.backdrop = core::BackdropMode::Wallpaper;
                        PopulateFromSettings();
                        EmitSettingsChanged();
                    }
                }
                break;
            }
            case kIdClearWallpaperBtn:
                working_.appearance.wallpaperFileName = std::nullopt;
                if (working_.appearance.backdrop == core::BackdropMode::Wallpaper) {
                    working_.appearance.backdrop = core::BackdropMode::Mica;
                }
                PopulateFromSettings();
                EmitSettingsChanged();
                break;
            case kIdEnableHotKeyCheck:
                working_.activation.enableHotKey = checked(enableHotKeyCheck_);
                RefreshHotKeyHint();
                EmitSettingsChanged();
                break;
            case kIdMiddleMouseCheck:
                working_.activation.enableMiddleMouse = checked(middleMouseCheck_);
                EmitSettingsChanged();
                break;
            case kIdFollowMouseCheck:
                working_.window.followMouse = checked(followMouseCheck_);
                EmitSettingsChanged();
                break;
            case kIdIgnoreFullscreenCheck:
                working_.activation.ignoreWhenFullScreen = checked(ignoreFullscreenCheck_);
                EmitSettingsChanged();
                break;
            case kIdDoubleClickCheck:
                working_.behavior.doubleClickToLaunch = checked(doubleClickCheck_);
                EmitSettingsChanged();
                break;
            case kIdHoverGroupCheck:
                working_.behavior.hoverSwitchGroup = checked(hoverGroupCheck_);
                EmitSettingsChanged();
                break;
            case kIdAutoStartCheck:
                working_.behavior.autoStart = checked(autoStartCheck_);
                EmitSettingsChanged();
                break;
            case kIdApplyRetentionBtn: {
                const std::string raw = core::utf8::WideToUtf8(ReadCtrlText(retentionEdit_));
                int days = 7;
                try {
                    days = std::clamp(std::stoi(raw), 1, 90);
                } catch (...) {
                    days = working_.behavior.backupRetentionDays;
                }
                working_.behavior.backupRetentionDays = days;
                SetWindowTextW(retentionEdit_, std::to_wstring(days).c_str());
                EmitSettingsChanged();
                break;
            }
            case kIdExportBackupBtn: {
                const std::wstring defName =
                    core::utf8::Utf8ToWide("GeekDeskCpp-" + TodayCompactDate() + ".gdbackup");
                if (auto dest = platform::SaveFileDialog(
                        hwnd_, L"\u5BFC\u51FA\u5B8C\u6574\u5907\u4EFD\u5305", defName, L"gdbackup",
                        {{L"GeekDeskCpp \u5907\u4EFD\u5305 (*.gdbackup)", L"*.gdbackup"}});
                    dest.has_value()) {
                    const auto res =
                        storage::BackupManager::ExportBackupBundle(dataRoot_, *dest);
                    if (res.ok) {
                        RefreshBackupListAndStats();
                        MessageBoxW(hwnd_,
                                    (L"\u5907\u4EFD\u5DF2\u5BFC\u51FA\u81F3\uFF1A\n" +
                                     dest->wstring())
                                        .c_str(),
                                    L"\u5BFC\u51FA\u6210\u529F", MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(hwnd_, core::utf8::Utf8ToWide(res.error).c_str(),
                                    L"\u5BFC\u51FA\u5931\u8D25", MB_OK | MB_ICONERROR);
                    }
                }
                break;
            }
            case kIdImportBackupBtn: {
                if (auto chosen = platform::PickFileDialog(
                        hwnd_, L"\u9009\u62E9 .gdbackup \u5907\u4EFD\u5305",
                        {{L"GeekDeskCpp \u5907\u4EFD\u5305 (*.gdbackup)", L"*.gdbackup"}});
                    chosen.has_value()) {
                    PerformRestoreFromPath(*chosen);
                }
                break;
            }
            case kIdRestoreSelectedBtn: {
                const int sel = static_cast<int>(SendMessageW(backupList_, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && static_cast<std::size_t>(sel) < snapshotPaths_.size()) {
                    PerformRestoreFromPath(snapshotPaths_[static_cast<std::size_t>(sel)]);
                }
                break;
            }
            case kIdOpenDataDirBtn:
                (void)platform::OpenDirectoryInExplorer(dataRoot_);
                break;
            case kIdOpenBackupsDirBtn:
                (void)platform::OpenDirectoryInExplorer(dataRoot_ / L"backups");
                break;
            case kIdOpenLogsDirBtn:
                (void)platform::OpenDirectoryInExplorer(dataRoot_ / L"logs");
                break;
            }
            return 0;
        }

        if (id == kIdHotKeyEdit && code == EN_CHANGE) {
            const std::string raw = core::utf8::WideToUtf8(ReadCtrlText(hotKeyEdit_));
            if (auto parsed = core::ParseHotKey(raw); parsed.has_value()) {
                working_.activation.hotKey = parsed->normalized;
                EmitSettingsChanged();
            }
            RefreshHotKeyHint();
            return 0;
        }

        if (id == kIdBackupList && code == LBN_SELCHANGE) {
            const int sel = static_cast<int>(SendMessageW(backupList_, LB_GETCURSEL, 0, 0));
            EnableWindow(restoreSelectedBtn_,
                         (sel >= 0 && static_cast<std::size_t>(sel) < snapshotPaths_.size())
                             ? TRUE
                             : FALSE);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        hwnd_ = nullptr;
        for (auto& vec : tabPages_) {
            vec.clear();
        }
        snapshotPaths_.clear();
        if (font_ != nullptr) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        if (bgBrush_ != nullptr) {
            DeleteObject(bgBrush_);
            bgBrush_ = nullptr;
        }
        if (editBrush_ != nullptr) {
            DeleteObject(editBrush_);
            editBrush_ = nullptr;
        }
        if (callbacks_.onClosed) {
            callbacks_.onClosed();
        }
        return 0;
    }

    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void SettingsWindow::BuildControls() {
    const HINSTANCE hInst = GetModuleHandleW(nullptr);
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    font_ = CreateFontW(-MulDiv(10, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int cw = rc.right - rc.left;
    const int ch = rc.bottom - rc.top;
    const int pad = ScalePx(14, dpi_);
    const int tabH = ScalePx(34, dpi_);
    const int tabW = (cw - pad * 2) / 3;

    const wchar_t* tabLabels[3] = {
        L"\u5916\u89C2\u4E0E\u6750\u8D28",
        L"\u547C\u51FA\u4E0E\u884C\u4E3A",
        L"\u6570\u636E\u4E0E\u5907\u4EFD\u6062\u590D",
    };
    for (int i = 0; i < 3; ++i) {
        tabButtons_[i] = CreateWindowExW(
            0, L"BUTTON", tabLabels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            pad + i * tabW, pad, (i == 2) ? (cw - pad * 2 - tabW * 2) : tabW, tabH, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTab0 + i)), hInst, nullptr);
    }

    const int px = pad + ScalePx(8, dpi_);
    const int py = pad + tabH + ScalePx(16, dpi_);
    const int pw = cw - px * 2;
    const int labelW = ScalePx(130, dpi_);
    const int ctrlX = px + labelW + ScalePx(8, dpi_);
    const int ctrlW = pw - labelW - ScalePx(8, dpi_);

    auto addToTab = [this](int tab, HWND h) -> HWND {
        tabPages_[tab].push_back(h);
        return h;
    };
    auto addLabel = [&](int tab, const wchar_t* txt, int x, int y, int w, int h) {
        return addToTab(tab, CreateWindowExW(0, L"STATIC", txt, WS_CHILD, x, y, w, h, hwnd_,
                                             nullptr, hInst, nullptr));
    };

    // === Tab 0: Appearance ===
    int y0 = py;
    const int rowH = ScalePx(28, dpi_);
    const int gap = ScalePx(10, dpi_);

    addLabel(0, L"\u4E3B\u9898\u6A21\u5F0F:", px, y0 + ScalePx(4, dpi_), labelW, ScalePx(20, dpi_));
    themeCombo_ = addToTab(
        0, CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
                           ctrlX, y0, ScalePx(180, dpi_), ScalePx(120, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdThemeCombo)), hInst,
                           nullptr));
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u8DDF\u968F\u7CFB\u7EDF (System)"));
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6D45\u8272 (Light)"));
    SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6DF1\u8272 (Dark)"));
    y0 += rowH + gap;

    addLabel(0, L"\u80CC\u666F\u6750\u8D28:", px, y0 + ScalePx(4, dpi_), labelW, ScalePx(20, dpi_));
    backdropCombo_ = addToTab(
        0, CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
                           ctrlX, y0, ScalePx(220, dpi_), ScalePx(160, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdBackdropCombo)), hInst,
                           nullptr));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u4E9A\u514B\u529B (Acrylic)"));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u4E91\u6BCD (Mica)"));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6DF1\u4E91\u6BCD (MicaAlt)"));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u81EA\u5B9A\u4E49\u58C1\u7EB8 (Wallpaper)"));
    SendMessageW(backdropCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u53CC\u8272\u6E10\u53D8 (Gradient)"));
    y0 += rowH + gap;

    const int valW = ScalePx(58, dpi_);
    const int sliderW = ctrlW - valW - ScalePx(6, dpi_);

    auto makeSliderRow = [&](const wchar_t* lbl, int sliderId, int valId, int minV, int maxV,
                             int freq, HWND& outSlider, HWND& outVal) {
        addLabel(0, lbl, px, y0 + ScalePx(5, dpi_), labelW, ScalePx(20, dpi_));
        outSlider = addToTab(
            0, CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                               WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, ctrlX, y0,
                               sliderW, ScalePx(32, dpi_), hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(sliderId)), hInst,
                               nullptr));
        SendMessageW(outSlider, TBM_SETRANGE, TRUE, MAKELONG(minV, maxV));
        SendMessageW(outSlider, TBM_SETTICFREQ, static_cast<WPARAM>(freq), 0);
        outVal = addToTab(
            0, CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_RIGHT,
                               ctrlX + sliderW + ScalePx(6, dpi_), y0 + ScalePx(5, dpi_), valW,
                               ScalePx(20, dpi_), hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(valId)), hInst,
                               nullptr));
        y0 += ScalePx(38, dpi_);
    };

    makeSliderRow(L"\u80CC\u666F\u4E0D\u900F\u660E\u5EA6:", kIdBgOpacitySlider, kIdBgOpacityVal, 0, 100, 10,
                  bgOpacitySlider_, bgOpacityVal_);
    makeSliderRow(L"\u5361\u7247\u906E\u7F69\u6D53\u5EA6:", kIdCardOpacitySlider, kIdCardOpacityVal, 0, 100, 10,
                  cardOpacitySlider_, cardOpacityVal_);
    makeSliderRow(L"\u56FE\u6807\u5C3A\u5BF8:", kIdIconSizeSlider, kIdIconSizeVal, 32, 80, 8,
                  iconSizeSlider_, iconSizeVal_);
    makeSliderRow(L"\u7A97\u53E3\u5706\u89D2:", kIdCornerRadiusSlider, kIdCornerRadiusVal, 0, 24, 4,
                  cornerSlider_, cornerVal_);

    addLabel(0, L"\u754C\u9762\u5B57\u4F53:", px, y0 + ScalePx(4, dpi_), labelW, ScalePx(20, dpi_));
    fontFamilyCombo_ = addToTab(
        0, CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
                           ctrlX, y0, ScalePx(220, dpi_), ScalePx(180, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFontFamilyCombo)), hInst,
                           nullptr));
    const wchar_t* kFonts[] = {
        L"Microsoft YaHei UI", L"Segoe UI", L"SimHei", L"KaiTi", L"FangSong", L"Consolas",
    };
    for (const wchar_t* fn : kFonts) {
        SendMessageW(fontFamilyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(fn));
    }
    y0 += rowH + gap;

    makeSliderRow(L"\u5B57\u4F53\u5927\u5C0F:", kIdFontSizeSlider, kIdFontSizeVal, 10, 20, 2,
                  fontSizeSlider_, fontSizeVal_);

    const int boxSz = ScalePx(18, dpi_);
    showTitleCheck_ = addToTab(
        0, CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, ctrlX,
                           y0 + ScalePx(2, dpi_), boxSz, boxSz, hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdShowTitleCheck)), hInst,
                           nullptr));
    addToTab(0, CreateWindowExW(
                    0, L"STATIC",
                    L"\u5728\u56FE\u6807\u4E0B\u65B9\u663E\u793A\u5E94\u7528\u540D\u79F0",
                    WS_CHILD | SS_NOTIFY, ctrlX + boxSz + ScalePx(6, dpi_), y0 + ScalePx(2, dpi_),
                    ctrlW - boxSz - ScalePx(6, dpi_), ScalePx(20, dpi_), hwnd_,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdShowTitleCheck + 1000)), hInst,
                    nullptr));
    y0 += ScalePx(34, dpi_);

    addLabel(0, L"\u5F53\u524D\u58C1\u7EB8:", px, y0 + ScalePx(4, dpi_), labelW, ScalePx(20, dpi_));
    wallpaperLabel_ = addToTab(
        0, CreateWindowExW(0, L"STATIC", L"\u672A\u9009\u62E9", WS_CHILD, ctrlX,
                           y0 + ScalePx(4, dpi_), ctrlW, ScalePx(20, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdWallpaperLabel)), hInst,
                           nullptr));
    y0 += ScalePx(28, dpi_);
    addToTab(0, CreateWindowExW(0, L"BUTTON", L"\u9009\u62E9\u58C1\u7EB8...", WS_CHILD | WS_TABSTOP,
                                ctrlX, y0, ScalePx(110, dpi_), ScalePx(28, dpi_), hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPickWallpaperBtn)),
                                hInst, nullptr));
    addToTab(0, CreateWindowExW(0, L"BUTTON", L"\u6E05\u9664\u58C1\u7EB8", WS_CHILD | WS_TABSTOP,
                                ctrlX + ScalePx(120, dpi_), y0, ScalePx(100, dpi_),
                                ScalePx(28, dpi_), hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdClearWallpaperBtn)),
                                hInst, nullptr));

    // === Tab 1: Activation & Behavior ===
    int y1 = py;
    enableHotKeyCheck_ = addToTab(
        1, CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, px,
                           y1 + ScalePx(4, dpi_), boxSz, boxSz, hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEnableHotKeyCheck)),
                           hInst, nullptr));
    addToTab(1, CreateWindowExW(
                    0, L"STATIC", L"\u542F\u7528\u5168\u5C40\u547C\u51FA\u70ED\u952E:",
                    WS_CHILD | SS_NOTIFY, px + boxSz + ScalePx(6, dpi_), y1 + ScalePx(4, dpi_),
                    ScalePx(130, dpi_), ScalePx(20, dpi_), hwnd_,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEnableHotKeyCheck + 1000)),
                    hInst, nullptr));
    hotKeyEdit_ = addToTab(
        1, CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, px + ScalePx(162, dpi_), y1,
                           ScalePx(140, dpi_), ScalePx(26, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHotKeyEdit)), hInst,
                           nullptr));
    hotKeyHint_ = addToTab(
        1, CreateWindowExW(0, L"STATIC", L"", WS_CHILD, px + ScalePx(310, dpi_),
                           y1 + ScalePx(4, dpi_), pw - ScalePx(310, dpi_), ScalePx(20, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHotKeyHint)), hInst,
                           nullptr));
    y1 += ScalePx(34, dpi_);

    auto addCheck = [&](int checkId, const wchar_t* text) {
        HWND h = addToTab(
            1, CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, px,
                               y1 + ScalePx(2, dpi_), boxSz, boxSz, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(checkId)), hInst,
                               nullptr));
        addToTab(1, CreateWindowExW(
                        0, L"STATIC", text, WS_CHILD | SS_NOTIFY, px + boxSz + ScalePx(6, dpi_),
                        y1 + ScalePx(2, dpi_), pw - boxSz - ScalePx(6, dpi_), ScalePx(20, dpi_),
                        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(checkId + 1000)),
                        hInst, nullptr));
        y1 += ScalePx(28, dpi_);
        return h;
    };

    middleMouseCheck_ = addCheck(kIdMiddleMouseCheck, L"\u542F\u7528\u9F20\u6807\u4E2D\u952E\u547C\u51FA / \u9690\u85CF");
    followMouseCheck_ =
        addCheck(kIdFollowMouseCheck, L"\u547C\u51FA\u65F6\u8DDF\u968F\u9F20\u6807\u4F4D\u7F6E\uFF08\u5149\u6807\u5BF9\u9F50\u7A97\u53E3\u5DE6\u4E0A\u89D2\uFF09");
    ignoreFullscreenCheck_ =
        addCheck(kIdIgnoreFullscreenCheck, L"\u5F53\u524D\u53F0\u5168\u5C4F\u6E38\u620F / \u89C6\u9891\u65F6\u5C4F\u853D\u547C\u51FA");
    doubleClickCheck_ = addCheck(kIdDoubleClickCheck, L"\u53CC\u51FB\u6253\u5F00\u5E94\u7528\uFF08\u53D6\u6D88\u52FE\u9009\u4E3A\u5355\u51FB\u542F\u52A8\uFF09");
    hoverGroupCheck_ = addCheck(kIdHoverGroupCheck, L"\u9F20\u6807\u60AC\u505C\u5DE6\u4FA7\u5206\u7EC4\u81EA\u52A8\u5207\u6362");
    autoStartCheck_ = addCheck(kIdAutoStartCheck, L"\u5F00\u673A\u81EA\u52A8\u542F\u52A8");
    y1 += ScalePx(6, dpi_);

    addLabel(1, L"\u7A97\u53E3\u9690\u85CF\u7B56\u7565:", px, y1 + ScalePx(4, dpi_), labelW, ScalePx(20, dpi_));
    hidePolicyCombo_ = addToTab(
        1, CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
                           ctrlX, y1, ScalePx(230, dpi_), ScalePx(120, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHidePolicyCombo)), hInst,
                           nullptr));
    SendMessageW(hidePolicyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5931\u53BB\u7126\u70B9\u65F6\u81EA\u52A8\u9690\u85CF"));
    SendMessageW(hidePolicyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u542F\u52A8\u5E94\u7528\u540E\u9690\u85CF"));
    SendMessageW(hidePolicyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u4EC5\u624B\u52A8 / \u70ED\u952E\u9690\u85CF"));
    y1 += ScalePx(34, dpi_);

    addLabel(1, L"\u641C\u7D22\u89E6\u53D1\u65B9\u5F0F:", px, y1 + ScalePx(4, dpi_), labelW, ScalePx(20, dpi_));
    searchActCombo_ = addToTab(
        1, CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
                           ctrlX, y1, ScalePx(230, dpi_), ScalePx(100, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSearchActCombo)), hInst,
                           nullptr));
    SendMessageW(searchActCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ctrl+F \u805A\u7126\u641C\u7D22\u6846"));
    SendMessageW(searchActCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u76F4\u63A5\u952E\u5165\u5B57\u7B26\u5373\u641C\u7D22"));
    y1 += ScalePx(34, dpi_);

    addLabel(1, L"\u5206\u7EC4\u6392\u5E8F:", px, y1 + ScalePx(4, dpi_), labelW, ScalePx(20, dpi_));
    groupSortCombo_ = addToTab(
        1, CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
                           ctrlX, y1, ScalePx(230, dpi_), ScalePx(150, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdGroupSortCombo)), hInst,
                           nullptr));
    AddSortItems(groupSortCombo_);
    y1 += ScalePx(34, dpi_);

    addLabel(1, L"\u6761\u76EE\u6392\u5E8F:", px, y1 + ScalePx(4, dpi_), labelW, ScalePx(20, dpi_));
    itemSortCombo_ = addToTab(
        1, CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
                           ctrlX, y1, ScalePx(230, dpi_), ScalePx(150, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemSortCombo)), hInst,
                           nullptr));
    AddSortItems(itemSortCombo_);

    // === Tab 2: Data, Backup & Restore ===
    int y2 = py;
    statsLabel_ = addToTab(
        2, CreateWindowExW(0, L"STATIC", L"", WS_CHILD, px, y2, pw, ScalePx(40, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStatsLabel)), hInst,
                           nullptr));
    y2 += ScalePx(44, dpi_);

    addLabel(2, L"\u6BCF\u65E5\u5907\u4EFD\u4FDD\u7559(\u5929):", px, y2 + ScalePx(4, dpi_), labelW,
             ScalePx(20, dpi_));
    retentionEdit_ = addToTab(
        2, CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"7",
                           WS_CHILD | WS_TABSTOP | ES_NUMBER, ctrlX, y2, ScalePx(70, dpi_),
                           ScalePx(26, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRetentionEdit)), hInst,
                           nullptr));
    addToTab(2, CreateWindowExW(0, L"BUTTON", L"\u5E94\u7528", WS_CHILD | WS_TABSTOP,
                                ctrlX + ScalePx(80, dpi_), y2, ScalePx(68, dpi_),
                                ScalePx(26, dpi_), hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApplyRetentionBtn)),
                                hInst, nullptr));
    y2 += ScalePx(36, dpi_);

    const int halfBtnW = (pw - ScalePx(10, dpi_)) / 2;
    addToTab(2, CreateWindowExW(0, L"BUTTON", L"\u5BFC\u51FA\u5B8C\u6574\u5907\u4EFD (.gdbackup)...",
                                WS_CHILD | WS_TABSTOP, px, y2, halfBtnW, ScalePx(32, dpi_), hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdExportBackupBtn)),
                                hInst, nullptr));
    addToTab(2, CreateWindowExW(0, L"BUTTON", L"\u9009\u62E9\u5907\u4EFD\u5305\u6062\u590D (.gdbackup)...",
                                WS_CHILD | WS_TABSTOP, px + halfBtnW + ScalePx(10, dpi_), y2,
                                halfBtnW, ScalePx(32, dpi_), hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdImportBackupBtn)),
                                hInst, nullptr));
    y2 += ScalePx(42, dpi_);

    addLabel(2, L"\u56DE\u6EDA\u70B9\u4E0E\u5907\u4EFD\u5FEB\u7167 (pre-restore-*):", px, y2, pw,
             ScalePx(20, dpi_));
    y2 += ScalePx(22, dpi_);

    backupList_ = addToTab(
        2, CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                           WS_CHILD | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                           px, y2, pw, ScalePx(140, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdBackupList)), hInst,
                           nullptr));
    y2 += ScalePx(148, dpi_);

    restoreSelectedBtn_ = addToTab(
        2, CreateWindowExW(0, L"BUTTON", L"\u4ECE\u9009\u4E2D\u7684 .gdbackup \u5FEB\u7167\u6062\u590D",
                           WS_CHILD | WS_TABSTOP, px, y2, pw, ScalePx(30, dpi_), hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRestoreSelectedBtn)),
                           hInst, nullptr));
    EnableWindow(restoreSelectedBtn_, FALSE);
    y2 += ScalePx(38, dpi_);

    const int thirdW = (pw - ScalePx(16, dpi_)) / 3;
    addToTab(2, CreateWindowExW(0, L"BUTTON", L"\u6253\u5F00\u6570\u636E\u76EE\u5F55", WS_CHILD | WS_TABSTOP, px,
                                y2, thirdW, ScalePx(28, dpi_), hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOpenDataDirBtn)),
                                hInst, nullptr));
    addToTab(2, CreateWindowExW(0, L"BUTTON", L"\u6253\u5F00\u5907\u4EFD\u76EE\u5F55", WS_CHILD | WS_TABSTOP,
                                px + thirdW + ScalePx(8, dpi_), y2, thirdW, ScalePx(28, dpi_),
                                hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOpenBackupsDirBtn)),
                                hInst, nullptr));
    addToTab(2, CreateWindowExW(0, L"BUTTON", L"\u6253\u5F00\u65E5\u5FD7\u76EE\u5F55", WS_CHILD | WS_TABSTOP,
                                px + (thirdW + ScalePx(8, dpi_)) * 2, y2, thirdW, ScalePx(28, dpi_),
                                hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOpenLogsDirBtn)),
                                hInst, nullptr));

    // Bottom Close button
    const int closeW = ScalePx(96, dpi_);
    const int closeH = ScalePx(30, dpi_);
    CreateWindowExW(0, L"BUTTON", L"\u5173\u95ED", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    cw - pad - closeW, ch - pad - closeH, closeW, closeH, hwnd_,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCloseBtn)), hInst, nullptr);

    // Apply uniform font & Explorer/Combo dark mode theme
    EnumChildWindows(
        hwnd_,
        [](HWND ch, LPARAM fontPtr) -> BOOL {
            SendMessageW(ch, WM_SETFONT, static_cast<WPARAM>(fontPtr), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(font_));
    ApplyChildThemes();
}

void SettingsWindow::SwitchTab(int tabIndex) {
    activeTab_ = std::clamp(tabIndex, 0, 2);
    for (int i = 0; i < 3; ++i) {
        const int cmd = (i == activeTab_) ? SW_SHOW : SW_HIDE;
        for (HWND h : tabPages_[i]) {
            ShowWindow(h, cmd);
        }
        if (tabButtons_[i] != nullptr) {
            InvalidateRect(tabButtons_[i], nullptr, TRUE);
        }
    }
    if (activeTab_ == 2) {
        RefreshBackupListAndStats();
    }
}

void SettingsWindow::PopulateFromSettings() {
    populating_ = true;

    SendMessageW(themeCombo_, CB_SETCURSEL, static_cast<WPARAM>(working_.appearance.theme), 0);
    SendMessageW(backdropCombo_, CB_SETCURSEL, static_cast<WPARAM>(working_.appearance.backdrop), 0);
    SendMessageW(
        bgOpacitySlider_, TBM_SETPOS, TRUE,
        static_cast<LPARAM>(std::lround(std::clamp(working_.appearance.backgroundOpacity, 0.0, 1.0) *
                                        100.0)));
    SendMessageW(
        cardOpacitySlider_, TBM_SETPOS, TRUE,
        static_cast<LPARAM>(std::lround(std::clamp(working_.appearance.cardOpacity, 0.0, 1.0) *
                                        100.0)));
    SendMessageW(iconSizeSlider_, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::clamp(working_.appearance.iconSize, 32, 80)));
    SendMessageW(cornerSlider_, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::clamp(working_.appearance.cornerRadius, 0, 24)));
    {
        const std::wstring wFam = core::utf8::Utf8ToWide(working_.appearance.fontFamily);
        int idx = static_cast<int>(SendMessageW(
            fontFamilyCombo_, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
            reinterpret_cast<LPARAM>(wFam.c_str())));
        if (idx == CB_ERR && !wFam.empty()) {
            idx = static_cast<int>(SendMessageW(fontFamilyCombo_, CB_ADDSTRING, 0,
                                                reinterpret_cast<LPARAM>(wFam.c_str())));
        }
        SendMessageW(fontFamilyCombo_, CB_SETCURSEL, (idx != CB_ERR) ? idx : 0, 0);
    }
    SendMessageW(fontSizeSlider_, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::clamp(working_.appearance.fontSize, 10, 20)));
    SendMessageW(showTitleCheck_, BM_SETCHECK,
                 working_.appearance.showItemTitle ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(wallpaperLabel_,
                   working_.appearance.wallpaperFileName.has_value() &&
                           !working_.appearance.wallpaperFileName->empty()
                       ? core::utf8::Utf8ToWide(*working_.appearance.wallpaperFileName).c_str()
                       : L"\u672A\u9009\u62E9");
    RefreshSliderLabels();

    SendMessageW(enableHotKeyCheck_, BM_SETCHECK,
                 working_.activation.enableHotKey ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(hotKeyEdit_, core::utf8::Utf8ToWide(working_.activation.hotKey).c_str());
    SendMessageW(middleMouseCheck_, BM_SETCHECK,
                 working_.activation.enableMiddleMouse ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(followMouseCheck_, BM_SETCHECK,
                 working_.window.followMouse ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(ignoreFullscreenCheck_, BM_SETCHECK,
                 working_.activation.ignoreWhenFullScreen ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hidePolicyCombo_, CB_SETCURSEL,
                 static_cast<WPARAM>(std::clamp(static_cast<int>(working_.window.hidePolicy) - 1, 0,
                                                2)),
                 0);
    SendMessageW(doubleClickCheck_, BM_SETCHECK,
                 working_.behavior.doubleClickToLaunch ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hoverGroupCheck_, BM_SETCHECK,
                 working_.behavior.hoverSwitchGroup ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(searchActCombo_, CB_SETCURSEL,
                 static_cast<WPARAM>(working_.behavior.searchActivation), 0);
    SendMessageW(groupSortCombo_, CB_SETCURSEL, SortToIdx(working_.behavior.groupSort), 0);
    SendMessageW(itemSortCombo_, CB_SETCURSEL, SortToIdx(working_.behavior.itemSort), 0);
    SendMessageW(autoStartCheck_, BM_SETCHECK,
                 working_.behavior.autoStart ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(retentionEdit_, std::to_wstring(working_.behavior.backupRetentionDays).c_str());

    RefreshHotKeyHint();
    populating_ = false;
}

void SettingsWindow::RefreshSliderLabels() {
    const int bgPct = static_cast<int>(std::lround(working_.appearance.backgroundOpacity * 100.0));
    const int cardPct = static_cast<int>(std::lround(working_.appearance.cardOpacity * 100.0));
    SetWindowTextW(bgOpacityVal_, (std::to_wstring(bgPct) + L"%").c_str());
    SetWindowTextW(cardOpacityVal_, (std::to_wstring(cardPct) + L"%").c_str());
    SetWindowTextW(iconSizeVal_, (std::to_wstring(working_.appearance.iconSize) + L" px").c_str());
    SetWindowTextW(cornerVal_, (std::to_wstring(working_.appearance.cornerRadius) + L" px").c_str());
    SetWindowTextW(fontSizeVal_, (std::to_wstring(working_.appearance.fontSize) + L" px").c_str());
}

void SettingsWindow::RefreshHotKeyHint() {
    if (!working_.activation.enableHotKey) {
        SetWindowTextW(hotKeyHint_, L"\u5DF2\u7981\u7528");
        return;
    }
    const std::string raw = core::utf8::WideToUtf8(ReadCtrlText(hotKeyEdit_));
    if (auto parsed = core::ParseHotKey(raw); parsed.has_value()) {
        const std::wstring msg = L"\u6709\u6548: " + core::utf8::Utf8ToWide(parsed->normalized);
        SetWindowTextW(hotKeyHint_, msg.c_str());
    } else {
        SetWindowTextW(hotKeyHint_, L"\u683C\u5F0F\u65E0\u6548 (\u5982 Ctrl+Q)");
    }
}

void SettingsWindow::RefreshBackupListAndStats() {
    if (hwnd_ == nullptr) {
        return;
    }
    std::size_t groupsCount = 0;
    std::size_t itemsCount = 0;
    if (callbacks_.onQueryStats) {
        auto [g, i] = callbacks_.onQueryStats();
        groupsCount = g;
        itemsCount = i;
    }
    const std::wstring statsW =
        L"\u6570\u636E\u76EE\u5F55: " + dataRoot_.wstring() + L"\n\u5F53\u524D\u72B6\u6001: " +
        std::to_wstring(groupsCount) + L" \u4E2A\u5206\u7EC4 / " + std::to_wstring(itemsCount) +
        L" \u4E2A\u542F\u52A8\u6761\u76EE";
    SetWindowTextW(statsLabel_, statsW.c_str());

    SendMessageW(backupList_, LB_RESETCONTENT, 0, 0);
    snapshotPaths_.clear();

    const std::filesystem::path backupsDir = dataRoot_ / L"backups";
    std::error_code ec;
    if (std::filesystem::exists(backupsDir, ec) && std::filesystem::is_directory(backupsDir, ec)) {
        std::vector<std::filesystem::path> bundles;
        for (const auto& entry : std::filesystem::directory_iterator(backupsDir, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const std::string ext =
                core::utf8::ToLowerAscii(core::utf8::PathToUtf8(entry.path().extension()));
            if (ext == ".gdbackup") {
                bundles.push_back(entry.path());
            }
        }
        std::sort(bundles.begin(), bundles.end(), [](const auto& a, const auto& b) {
            return a.filename().wstring() > b.filename().wstring();
        });
        for (const auto& p : bundles) {
            const auto summary = storage::BackupManager::InspectBackupBundle(p);
            std::wstring display = p.filename().wstring();
            if (summary.ok) {
                display += L"   [" + std::to_wstring(summary.groupCount) + L" \u7EC4 / " +
                           std::to_wstring(summary.itemCount) + L" \u9879]";
            }
            SendMessageW(backupList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
            snapshotPaths_.push_back(p);
        }
    }
    EnableWindow(restoreSelectedBtn_, FALSE);
}

void SettingsWindow::EmitSettingsChanged() {
    if (callbacks_.onSettingsChanged) {
        callbacks_.onSettingsChanged(working_);
    }
}

void SettingsWindow::PerformRestoreFromPath(const std::filesystem::path& bundlePath) {
    const auto summary = storage::BackupManager::InspectBackupBundle(bundlePath);
    if (!summary.ok) {
        MessageBoxW(hwnd_, core::utf8::Utf8ToWide("\u5907\u4EFD\u6587\u4EF6\u65E0\u6548\uFF1A" + summary.error).c_str(),
                    L"\u6821\u9A8C\u5931\u8D25", MB_OK | MB_ICONERROR);
        return;
    }

    const std::wstring confirmMsg =
        L"\u5373\u5C06\u4ECE\u4EE5\u4E0B\u5907\u4EFD\u5305\u6062\u590D\u914D\u7F6E\u4E0E\u5E94\u7528\u76EE\u5F55\uFF1A\n\n"
        L"\u6587\u4EF6: " +
        bundlePath.filename().wstring() + L"\n\u521B\u5EFA\u65F6\u95F4: " +
        core::utf8::Utf8ToWide(summary.createdAt) + L"\n\u5305\u542B\u5185\u5BB9: " +
        std::to_wstring(summary.groupCount) + L" \u4E2A\u5206\u7EC4 / " +
        std::to_wstring(summary.itemCount) +
        L" \u4E2A\u6761\u76EE\n\n\u6062\u590D\u524D\u4F1A\u81EA\u52A8\u4E3A\u5F53\u524D\u72B6\u6001\u751F\u6210 pre-restore-* \u56DE\u6EDA\u5FEB\u7167\u3002\u662F\u5426\u7EE7\u7EED\uFF1F";

    if (MessageBoxW(hwnd_, confirmMsg.c_str(), L"\u786E\u8BA4\u6062\u590D\u5907\u4EFD",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    const auto restoreRes = storage::BackupManager::RestoreBackupBundle(dataRoot_, bundlePath);
    if (!restoreRes.ok) {
        MessageBoxW(hwnd_, core::utf8::Utf8ToWide(restoreRes.error).c_str(),
                    L"\u6062\u590D\u5931\u8D25", MB_OK | MB_ICONERROR);
        return;
    }

    const auto newSettings = storage::BackupManager::LoadSettingsWithRecovery(dataRoot_);
    const auto newCatalog = storage::BackupManager::LoadCatalogWithRecovery(dataRoot_);
    if (newSettings.ok && newCatalog.ok) {
        working_ = newSettings.value;
        RebuildThemeBrushes();
        PopulateFromSettings();
        if (callbacks_.onBackupRestored) {
            callbacks_.onBackupRestored(newSettings.value, newCatalog.value,
                                        restoreRes.rollbackSnapshotPath);
        }
    }
    RefreshBackupListAndStats();
    InvalidateRect(hwnd_, nullptr, TRUE);

    const std::wstring okMsg =
        restoreRes.rollbackSnapshotPath.empty()
            ? L"\u5907\u4EFD\u5DF2\u6210\u529F\u6062\u590D\u5E76\u7ACB\u5373\u751F\u6548\u3002"
            : L"\u5907\u4EFD\u5DF2\u6210\u529F\u6062\u590D\u5E76\u7ACB\u5373\u751F\u6548\u3002\n\n\u5DF2\u521B\u5EFA\u5B89\u5168\u56DE\u6EDA\u70B9\uFF1A\n" +
                  restoreRes.rollbackSnapshotPath.filename().wstring();
    MessageBoxW(hwnd_, okMsg.c_str(), L"\u6062\u590D\u5B8C\u6210", MB_OK | MB_ICONINFORMATION);
}

} // namespace geekdesk::ui
