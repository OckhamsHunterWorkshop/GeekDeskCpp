#include "ui/item_dialogs.hpp"

#include "core/placement_math.hpp"
#include "core/utf8.hpp"
#include "platform/dpi_helper.hpp"
#include "platform/file_dialogs.hpp"
#include "platform/shortcut_resolver.hpp"

#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <string>

namespace geekdesk::ui {

namespace {

int Scale(int dip, std::uint32_t dpi) noexcept {
    return core::DipToPhysicalPx(static_cast<double>(dip), dpi);
}

HFONT CreateUiFont(std::uint32_t dpi, int pointSize = 10) {
    return CreateFontW(-MulDiv(pointSize, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE,
                       FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

std::wstring GetControlText(HWND ctrl) {
    const int len = GetWindowTextLengthW(ctrl);
    if (len <= 0) {
        return {};
    }
    std::wstring buf(static_cast<std::size_t>(len) + 1, L'\0');
    GetWindowTextW(ctrl, buf.data(), len + 1);
    buf.resize(static_cast<std::size_t>(len));
    return buf;
}

void ApplyFontToChildren(HWND parent, HFONT font) {
    EnumChildWindows(
        parent,
        [](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(font));
}

struct ThemeColors {
    bool isDark = false;
    HBRUSH bgBrush = nullptr;
    HBRUSH editBrush = nullptr;
    COLORREF bgColor = 0;
    COLORREF editColor = 0;
    COLORREF textColor = 0;

    explicit ThemeColors(bool dark) : isDark(dark) {
        if (dark) {
            bgColor = RGB(30, 32, 40);
            editColor = RGB(42, 45, 56);
            textColor = RGB(242, 244, 248);
        } else {
            bgColor = RGB(246, 247, 250);
            editColor = RGB(255, 255, 255);
            textColor = RGB(20, 24, 34);
        }
        bgBrush = CreateSolidBrush(bgColor);
        editBrush = CreateSolidBrush(editColor);
    }

    ~ThemeColors() {
        if (bgBrush) {
            DeleteObject(bgBrush);
        }
        if (editBrush) {
            DeleteObject(editBrush);
        }
    }

    LRESULT HandleColor(UINT msg, WPARAM wParam) const {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, textColor);
        if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
            SetBkColor(hdc, editColor);
            return reinterpret_cast<LRESULT>(editBrush);
        }
        SetBkColor(hdc, bgColor);
        return reinterpret_cast<LRESULT>(bgBrush);
    }
};

struct TextDialogState {
    ThemeColors colors;
    HFONT font = nullptr;
    HWND edit = nullptr;
    bool accepted = false;
    bool done = false;
    std::wstring resultText;

    explicit TextDialogState(bool dark) : colors(dark) {}
    ~TextDialogState() {
        if (font) {
            DeleteObject(font);
        }
    }
};

constexpr int kIdTextEdit = 101;

LRESULT CALLBACK TextDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<TextDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        st = static_cast<TextDialogState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        return TRUE;
    }
    if (st == nullptr) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, st->colors.bgBrush);
        return 1;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
        return st->colors.HandleColor(msg, wParam);

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDOK) {
            st->resultText = GetControlText(st->edit);
            st->accepted = true;
            st->done = true;
            return 0;
        }
        if (id == IDCANCEL) {
            st->accepted = false;
            st->done = true;
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        st->accepted = false;
        st->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

struct ItemDialogState {
    ThemeColors colors;
    HFONT font = nullptr;
    core::LaunchItem* item = nullptr;
    HWND nameEdit = nullptr;
    HWND kindCombo = nullptr;
    HWND targetEdit = nullptr;
    HWND argsEdit = nullptr;
    HWND workDirEdit = nullptr;
    HWND adminCheck = nullptr;
    HWND tooltipEdit = nullptr;
    bool accepted = false;
    bool done = false;

    ItemDialogState(bool dark, core::LaunchItem* ptr) : colors(dark), item(ptr) {}
    ~ItemDialogState() {
        if (font) {
            DeleteObject(font);
        }
    }
};

constexpr int kIdItemName = 201;
constexpr int kIdItemKind = 202;
constexpr int kIdItemTarget = 203;
constexpr int kIdItemBrowseFile = 204;
constexpr int kIdItemBrowseDir = 205;
constexpr int kIdItemArgs = 206;
constexpr int kIdItemWorkDir = 207;
constexpr int kIdItemAdmin = 208;
constexpr int kIdItemTooltip = 209;

core::ItemKind ComboIndexToKind(int idx) noexcept {
    switch (idx) {
    case 0:
        return core::ItemKind::File;
    case 1:
        return core::ItemKind::Folder;
    case 2:
        return core::ItemKind::Url;
    case 3:
        return core::ItemKind::SystemItem;
    default:
        return core::ItemKind::File;
    }
}

int KindToComboIndex(core::ItemKind kind) noexcept {
    switch (kind) {
    case core::ItemKind::File:
        return 0;
    case core::ItemKind::Folder:
        return 1;
    case core::ItemKind::Url:
        return 2;
    case core::ItemKind::SystemItem:
        return 3;
    }
    return 0;
}

LRESULT CALLBACK ItemDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<ItemDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        st = static_cast<ItemDialogState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        return TRUE;
    }
    if (st == nullptr) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, st->colors.bgBrush);
        return 1;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return st->colors.HandleColor(msg, wParam);

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (code == STN_CLICKED && id == kIdItemAdmin + 1000) {
            const LRESULT cur = SendMessageW(st->adminCheck, BM_GETCHECK, 0, 0);
            SendMessageW(st->adminCheck, BM_SETCHECK,
                         cur == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
            return 0;
        }
        if (id == kIdItemBrowseFile) {
            if (auto chosen = platform::PickFileDialog(hwnd, L"\u9009\u62E9\u5E94\u7528\u6216\u6587\u4EF6");
                chosen.has_value()) {
                const std::string extLower =
                    core::utf8::ToLowerAscii(core::utf8::PathToUtf8(chosen->extension()));
                if (extLower == ".lnk" || extLower == ".url") {
                    const auto resolved = platform::ResolveShortcutFile(*chosen);
                    if (resolved.ok) {
                        SetWindowTextW(st->targetEdit,
                                       core::utf8::Utf8ToWide(resolved.targetPath).c_str());
                        if (!resolved.arguments.empty()) {
                            SetWindowTextW(st->argsEdit,
                                           core::utf8::Utf8ToWide(resolved.arguments).c_str());
                        }
                        if (!resolved.workingDirectory.empty()) {
                            SetWindowTextW(
                                st->workDirEdit,
                                core::utf8::Utf8ToWide(resolved.workingDirectory).c_str());
                        }
                        SendMessageW(st->kindCombo, CB_SETCURSEL,
                                     resolved.isUrl ? 2 : 0, 0);
                    } else {
                        SetWindowTextW(st->targetEdit, chosen->c_str());
                        SendMessageW(st->kindCombo, CB_SETCURSEL, 0, 0);
                    }
                } else {
                    SetWindowTextW(st->targetEdit, chosen->c_str());
                    SendMessageW(st->kindCombo, CB_SETCURSEL, 0, 0);
                }
                if (GetControlText(st->nameEdit).empty()) {
                    SetWindowTextW(st->nameEdit, chosen->stem().c_str());
                }
            }
            return 0;
        }
        if (id == kIdItemBrowseDir) {
            if (auto dir = platform::PickFolderDialog(hwnd, L"\u9009\u62E9\u76EE\u6807\u6587\u4EF6\u5939");
                dir.has_value()) {
                SetWindowTextW(st->targetEdit, dir->c_str());
                SendMessageW(st->kindCombo, CB_SETCURSEL, 1, 0);
                if (GetControlText(st->nameEdit).empty()) {
                    SetWindowTextW(st->nameEdit, dir->filename().c_str());
                }
            }
            return 0;
        }
        if (id == IDOK) {
            const std::string nameUtf8 = core::utf8::WideToUtf8(GetControlText(st->nameEdit));
            const std::string targetUtf8 = core::utf8::WideToUtf8(GetControlText(st->targetEdit));
            const auto trimName = core::utf8::TrimAscii(nameUtf8);
            const auto trimTarget = core::utf8::TrimAscii(targetUtf8);
            if (trimName.empty() || trimTarget.empty()) {
                MessageBoxW(hwnd,
                            L"\u540D\u79F0\u548C\u76EE\u6807\u8DEF\u5F84 / \u7F51\u5740\u4E0D\u80FD\u4E3A\u7A7A\u3002",
                            L"\u63D0\u793A", MB_OK | MB_ICONWARNING);
                return 0;
            }

            st->item->name.assign(trimName);
            st->item->targetPath.assign(trimTarget);
            st->item->kind = ComboIndexToKind(
                static_cast<int>(SendMessageW(st->kindCombo, CB_GETCURSEL, 0, 0)));
            st->item->arguments = core::utf8::WideToUtf8(GetControlText(st->argsEdit));

            const std::string wdUtf8 = core::utf8::WideToUtf8(GetControlText(st->workDirEdit));
            const auto trimWd = core::utf8::TrimAscii(wdUtf8);
            if (trimWd.empty()) {
                st->item->workingDirectory = std::nullopt;
            } else {
                st->item->workingDirectory = std::string(trimWd);
            }

            st->item->runAsAdmin = (SendMessageW(st->adminCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

            const std::string tipUtf8 = core::utf8::WideToUtf8(GetControlText(st->tooltipEdit));
            const auto trimTip = core::utf8::TrimAscii(tipUtf8);
            if (trimTip.empty()) {
                st->item->tooltip = std::nullopt;
            } else {
                st->item->tooltip = std::string(trimTip);
            }

            st->accepted = true;
            st->done = true;
            return 0;
        }
        if (id == IDCANCEL) {
            st->accepted = false;
            st->done = true;
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        st->accepted = false;
        st->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RunModalLoop(HWND dlg, const bool& doneFlag) {
    MSG msg{};
    while (!doneFlag && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && (msg.hwnd == dlg || IsChild(dlg, msg.hwnd))) {
            if (msg.wParam == VK_ESCAPE) {
                SendMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
                continue;
            }
            if (msg.wParam == VK_RETURN) {
                wchar_t cls[32]{};
                GetClassNameW(msg.hwnd, cls, static_cast<int>(std::size(cls)));
                if (std::wstring_view(cls) != L"ComboBox") {
                    SendMessageW(dlg, WM_COMMAND, IDOK, 0);
                    continue;
                }
            }
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

} // namespace

std::optional<std::string> ShowTextInputDialog(HWND owner,
                                               bool isDark,
                                               std::wstring_view title,
                                               std::wstring_view promptLabel,
                                               std::string_view initialUtf8) {
    const HINSTANCE hInst = GetModuleHandleW(nullptr);
    constexpr const wchar_t* kCls = L"GeekDeskCppTextInputDlg";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TextDlgProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCls;
    RegisterClassExW(&wc);

    const std::uint32_t dpi = platform::GetWindowDpi(owner);
    const auto metrics = platform::GetMonitorMetricsForWindow(owner);
    const int winW = Scale(380, dpi);
    const int winH = Scale(168, dpi);
    const auto pos = core::ComputeCenteredPosition(winW, winH, metrics.workArea);

    TextDialogState state(isDark);
    state.font = CreateUiFont(dpi, 10);

    std::wstring titleW(title);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kCls, titleW.c_str(),
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, pos.x, pos.y, winW, winH,
                               owner, nullptr, hInst, &state);
    if (dlg == nullptr) {
        return std::nullopt;
    }

    const BOOL darkFlag = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(dlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFlag, sizeof(darkFlag));

    RECT rc{};
    GetClientRect(dlg, &rc);
    const int cw = rc.right - rc.left;
    const int pad = Scale(16, dpi);

    std::wstring labelW(promptLabel);
    CreateWindowExW(0, L"STATIC", labelW.c_str(), WS_CHILD | WS_VISIBLE, pad, pad, cw - pad * 2,
                    Scale(20, dpi), dlg, nullptr, hInst, nullptr);

    const std::wstring initW = core::utf8::Utf8ToWide(initialUtf8);
    state.edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", initW.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, pad, pad + Scale(26, dpi),
        cw - pad * 2, Scale(28, dpi), dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTextEdit)), hInst, nullptr);

    const int btnW = Scale(86, dpi);
    const int btnH = Scale(30, dpi);
    const int btnY = rc.bottom - pad - btnH;
    CreateWindowExW(0, L"BUTTON", L"\u786E\u5B9A",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    cw - pad - btnW * 2 - Scale(10, dpi), btnY, btnW, btnH, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"\u53D6\u6D88", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    cw - pad - btnW, btnY, btnW, btnH, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);

    ApplyFontToChildren(dlg, state.font);
    if (isDark) {
        EnumChildWindows(
            dlg,
            [](HWND ch, LPARAM) -> BOOL {
                SetWindowTheme(ch, L"DarkMode_Explorer", nullptr);
                return TRUE;
            },
            0);
    }

    if (owner != nullptr) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(dlg, SW_SHOW);
    SetFocus(state.edit);
    SendMessageW(state.edit, EM_SETSEL, 0, -1);

    RunModalLoop(dlg, state.done);

    if (owner != nullptr) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    DestroyWindow(dlg);

    if (!state.accepted) {
        return std::nullopt;
    }
    const std::string outUtf8 = core::utf8::WideToUtf8(state.resultText);
    const auto trimmed = core::utf8::TrimAscii(outUtf8);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    return std::string(trimmed);
}

bool ShowItemEditDialog(HWND owner,
                        bool isDark,
                        std::wstring_view title,
                        core::LaunchItem& inOutItem) {
    const HINSTANCE hInst = GetModuleHandleW(nullptr);
    constexpr const wchar_t* kCls = L"GeekDeskCppItemEditDlg";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &ItemDlgProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCls;
    RegisterClassExW(&wc);

    const std::uint32_t dpi = platform::GetWindowDpi(owner);
    const auto metrics = platform::GetMonitorMetricsForWindow(owner);
    const int winW = Scale(520, dpi);
    const int winH = Scale(395, dpi);
    const auto pos = core::ComputeCenteredPosition(winW, winH, metrics.workArea);

    ItemDialogState state(isDark, &inOutItem);
    state.font = CreateUiFont(dpi, 10);

    std::wstring titleW(title);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kCls, titleW.c_str(),
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, pos.x, pos.y, winW, winH,
                               owner, nullptr, hInst, &state);
    if (dlg == nullptr) {
        return false;
    }

    const BOOL darkFlag = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(dlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFlag, sizeof(darkFlag));

    RECT rc{};
    GetClientRect(dlg, &rc);
    const int cw = rc.right - rc.left;
    const int pad = Scale(16, dpi);
    const int labelW = Scale(88, dpi);
    const int ctrlX = pad + labelW + Scale(8, dpi);
    const int ctrlW = cw - ctrlX - pad;
    const int rowH = Scale(28, dpi);
    const int gap = Scale(10, dpi);
    int y = pad;

    auto addLabel = [&](const wchar_t* txt, int yy) {
        CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE, pad, yy + Scale(4, dpi), labelW,
                        Scale(20, dpi), dlg, nullptr, hInst, nullptr);
    };

    addLabel(L"\u663E\u793A\u540D\u79F0:", y);
    state.nameEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", core::utf8::Utf8ToWide(inOutItem.name).c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, ctrlX, y, ctrlW, rowH, dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemName)), hInst, nullptr);
    y += rowH + gap;

    addLabel(L"\u6761\u76EE\u7C7B\u578B:", y);
    state.kindCombo = CreateWindowExW(
        0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, ctrlX, y,
        Scale(160, dpi), Scale(160, dpi), dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemKind)), hInst, nullptr);
    SendMessageW(state.kindCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6587\u4EF6 / \u5E94\u7528 (File)"));
    SendMessageW(state.kindCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6587\u4EF6\u5939 (Folder)"));
    SendMessageW(state.kindCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u7F51\u5740 (Url)"));
    SendMessageW(state.kindCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u7CFB\u7EDF\u9879 (System)"));
    SendMessageW(state.kindCombo, CB_SETCURSEL, KindToComboIndex(inOutItem.kind), 0);
    y += rowH + gap;

    addLabel(L"\u76EE\u6807\u8DEF\u5F84:", y);
    const int browseBtnW = Scale(58, dpi);
    const int targetW = ctrlW - browseBtnW * 2 - Scale(8, dpi);
    state.targetEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", core::utf8::Utf8ToWide(inOutItem.targetPath).c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, ctrlX, y, targetW, rowH, dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemTarget)), hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"\u6587\u4EF6...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    ctrlX + targetW + Scale(4, dpi), y, browseBtnW, rowH, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemBrowseFile)), hInst,
                    nullptr);
    CreateWindowExW(0, L"BUTTON", L"\u76EE\u5F55...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    ctrlX + targetW + browseBtnW + Scale(8, dpi), y, browseBtnW, rowH, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemBrowseDir)), hInst,
                    nullptr);
    y += rowH + gap;

    addLabel(L"\u542F\u52A8\u53C2\u6570:", y);
    state.argsEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", core::utf8::Utf8ToWide(inOutItem.arguments).c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, ctrlX, y, ctrlW, rowH, dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemArgs)), hInst, nullptr);
    y += rowH + gap;

    addLabel(L"\u5DE5\u4F5C\u76EE\u5F55:", y);
    state.workDirEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT",
        inOutItem.workingDirectory.has_value()
            ? core::utf8::Utf8ToWide(*inOutItem.workingDirectory).c_str()
            : L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, ctrlX, y, ctrlW, rowH, dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemWorkDir)), hInst, nullptr);
    y += rowH + gap;

    addLabel(L"\u5907\u6CE8\u63D0\u793A:", y);
    state.tooltipEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT",
        inOutItem.tooltip.has_value() ? core::utf8::Utf8ToWide(*inOutItem.tooltip).c_str() : L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, ctrlX, y, ctrlW, rowH, dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemTooltip)), hInst, nullptr);
    y += rowH + gap;

    const int cbSize = Scale(18, dpi);
    const int cbH = Scale(24, dpi);
    state.adminCheck = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, ctrlX, y + Scale(3, dpi),
        cbSize, cbSize, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemAdmin)), hInst,
        nullptr);
    CreateWindowExW(
        0, L"STATIC", L"\u9ED8\u8BA4\u4EE5\u7BA1\u7406\u5458\u6743\u9650\u8FD0\u884C (runAsAdmin)",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTERIMAGE, ctrlX + cbSize + Scale(6, dpi), y,
        ctrlW - cbSize - Scale(6, dpi), cbH, dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdItemAdmin + 1000)), hInst, nullptr);
    SendMessageW(state.adminCheck, BM_SETCHECK, inOutItem.runAsAdmin ? BST_CHECKED : BST_UNCHECKED,
                 0);

    const int btnW = Scale(90, dpi);
    const int btnH = Scale(30, dpi);
    const int btnY = rc.bottom - pad - btnH;
    CreateWindowExW(0, L"BUTTON", L"\u4FDD\u5B58",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    cw - pad - btnW * 2 - Scale(10, dpi), btnY, btnW, btnH, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"\u53D6\u6D88", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    cw - pad - btnW, btnY, btnW, btnH, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);

    ApplyFontToChildren(dlg, state.font);
    if (isDark) {
        EnumChildWindows(
            dlg,
            [](HWND ch, LPARAM) -> BOOL {
                wchar_t cls[32]{};
                GetClassNameW(ch, cls, 32);
                if (std::wstring_view(cls) == L"ComboBox") {
                    SetWindowTheme(ch, L"DarkMode_CFD", nullptr);
                } else {
                    SetWindowTheme(ch, L"DarkMode_Explorer", nullptr);
                }
                return TRUE;
            },
            0);
    }

    if (owner != nullptr) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(dlg, SW_SHOW);
    SetFocus(state.nameEdit);

    RunModalLoop(dlg, state.done);

    if (owner != nullptr) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    DestroyWindow(dlg);
    return state.accepted;
}

} // namespace geekdesk::ui
