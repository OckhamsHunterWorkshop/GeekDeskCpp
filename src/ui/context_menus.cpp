#include "ui/context_menus.hpp"

#include "core/placement_math.hpp"
#include "platform/com_ptr.hpp"
#include "platform/dpi_helper.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <vector>

namespace geekdesk::ui {

namespace {

constexpr const wchar_t* kMenuClassName = L"GeekDeskCppThemedPopupMenu";

struct MenuRowLayout {
    PopupMenuItem item;
    float topDip = 0.0f;
    float bottomDip = 0.0f;
};

struct ThemedMenuState {
    bool isDark = true;
    std::uint32_t dpi = 96;
    float widthDip = 180.0f;
    float heightDip = 60.0f;
    int widthPx = 180;
    int heightPx = 60;
    std::vector<MenuRowLayout> rows;
    int hoveredIdx = -1;
    bool trackingLeave = false;
    bool done = false;
    bool closedByDeactivate = false;
    std::optional<UINT> selectedId;

    platform::ComPtr<ID2D1Factory> factory;
    platform::ComPtr<IDWriteFactory> dwrite;
    platform::ComPtr<IDWriteTextFormat> textFmt;
    platform::ComPtr<ID2D1DCRenderTarget> rt;
    platform::ComPtr<ID2D1SolidColorBrush> brush;
    platform::ComPtr<ID2D1Layer> clipLayer;

    HDC memDc = nullptr;
    HBITMAP memBmp = nullptr;
    HGDIOBJ oldBmp = nullptr;

    ~ThemedMenuState() {
        if (memDc != nullptr) {
            if (oldBmp != nullptr) {
                SelectObject(memDc, oldBmp);
                oldBmp = nullptr;
            }
            if (memBmp != nullptr) {
                DeleteObject(memBmp);
                memBmp = nullptr;
            }
            DeleteDC(memDc);
            memDc = nullptr;
        }
    }
};

struct ScreenDcGuard {
    HDC hdc = nullptr;
    ~ScreenDcGuard() {
        if (hdc != nullptr) {
            ReleaseDC(nullptr, hdc);
        }
    }
};

bool InitMenuResources(ThemedMenuState& st) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                 reinterpret_cast<void**>(st.factory.ReleaseAndGetAddressOf())))) {
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(
                                       st.dwrite.ReleaseAndGetAddressOf())))) {
        return false;
    }
    if (FAILED(st.dwrite->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                           13.0f, L"zh-CN",
                                           st.textFmt.ReleaseAndGetAddressOf()))) {
        return false;
    }
    st.textFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    st.textFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    st.textFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    D2D1_RENDER_TARGET_PROPERTIES props{};
    props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = static_cast<float>(st.dpi > 0 ? st.dpi : 96u);
    props.dpiY = props.dpiX;
    props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    if (FAILED(st.factory->CreateDCRenderTarget(&props, st.rt.ReleaseAndGetAddressOf()))) {
        return false;
    }
    st.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (FAILED(st.rt->CreateSolidColorBrush(D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f},
                                            st.brush.ReleaseAndGetAddressOf()))) {
        return false;
    }
    st.rt->CreateLayer(nullptr, st.clipLayer.ReleaseAndGetAddressOf());
    return true;
}

bool RenderThemedMenu(HWND hwnd, ThemedMenuState& st) {
    if (hwnd == nullptr || !st.rt || !st.brush) {
        return false;
    }
    ScreenDcGuard screenDc;
    screenDc.hdc = GetDC(nullptr);
    if (screenDc.hdc == nullptr) {
        return false;
    }

    if (st.memDc == nullptr) {
        st.memDc = CreateCompatibleDC(screenDc.hdc);
        if (st.memDc == nullptr) {
            return false;
        }
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = st.widthPx;
        bmi.bmiHeader.biHeight = -st.heightPx;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        st.memBmp = CreateDIBSection(st.memDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (st.memBmp == nullptr) {
            return false;
        }
        st.oldBmp = SelectObject(st.memDc, st.memBmp);
    }

    const RECT bindRect{0, 0, st.widthPx, st.heightPx};
    if (FAILED(st.rt->BindDC(st.memDc, &bindRect))) {
        return false;
    }

    const D2D1_COLOR_F bgClr = st.isDark ? D2D1_COLOR_F{26.0f / 255.0f, 29.0f / 255.0f, 38.0f / 255.0f, 0.96f}
                                         : D2D1_COLOR_F{248.0f / 255.0f, 249.0f / 255.0f, 252.0f / 255.0f, 0.97f};
    const D2D1_COLOR_F borderClr = st.isDark ? D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 0.18f}
                                             : D2D1_COLOR_F{15.0f / 255.0f, 23.0f / 255.0f, 42.0f / 255.0f, 0.16f};
    const D2D1_COLOR_F hoverClr = st.isDark ? D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 0.11f}
                                            : D2D1_COLOR_F{15.0f / 255.0f, 23.0f / 255.0f, 42.0f / 255.0f, 0.08f};
    const D2D1_COLOR_F textClr = st.isDark ? D2D1_COLOR_F{244.0f / 255.0f, 245.0f / 255.0f, 248.0f / 255.0f, 1.0f}
                                           : D2D1_COLOR_F{18.0f / 255.0f, 24.0f / 255.0f, 38.0f / 255.0f, 1.0f};
    const D2D1_COLOR_F disClr = st.isDark ? D2D1_COLOR_F{120.0f / 255.0f, 128.0f / 255.0f, 144.0f / 255.0f, 0.85f}
                                          : D2D1_COLOR_F{148.0f / 255.0f, 156.0f / 255.0f, 172.0f / 255.0f, 0.85f};
    const D2D1_COLOR_F sepClr = st.isDark ? D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 0.10f}
                                          : D2D1_COLOR_F{15.0f / 255.0f, 23.0f / 255.0f, 42.0f / 255.0f, 0.10f};
    const D2D1_COLOR_F accClr = st.isDark ? D2D1_COLOR_F{59.0f / 255.0f, 130.0f / 255.0f, 246.0f / 255.0f, 1.0f}
                                          : D2D1_COLOR_F{37.0f / 255.0f, 99.0f / 255.0f, 235.0f / 255.0f, 1.0f};

    constexpr float kMenuRadius = 8.0f;
    const D2D1_RECT_F fullRect{0.0f, 0.0f, st.widthDip, st.heightDip};
    const D2D1_ROUNDED_RECT roundRect{
        D2D1_RECT_F{0.5f, 0.5f, std::max(1.5f, st.widthDip - 0.5f), std::max(1.5f, st.heightDip - 0.5f)},
        kMenuRadius, kMenuRadius};

    st.rt->BeginDraw();
    st.rt->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});

    bool pushed = false;
    if (st.clipLayer && st.factory) {
        platform::ComPtr<ID2D1RoundedRectangleGeometry> geo;
        if (SUCCEEDED(st.factory->CreateRoundedRectangleGeometry(roundRect,
                                                                  geo.ReleaseAndGetAddressOf()))) {
            D2D1_LAYER_PARAMETERS lp{};
            lp.contentBounds = fullRect;
            lp.geometricMask = geo.Get();
            lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
            lp.maskTransform._11 = 1.0f;
            lp.maskTransform._22 = 1.0f;
            lp.opacity = 1.0f;
            lp.layerOptions = D2D1_LAYER_OPTIONS_NONE;
            st.rt->PushLayer(&lp, st.clipLayer.Get());
            pushed = true;
        }
    }

    st.brush->SetColor(bgClr);
    st.rt->FillRectangle(fullRect, st.brush.Get());

    for (std::size_t i = 0; i < st.rows.size(); ++i) {
        const auto& row = st.rows[i];
        if (row.item.separator) {
            const float midY = (row.topDip + row.bottomDip) * 0.5f;
            st.brush->SetColor(sepClr);
            st.rt->DrawLine(D2D1_POINT_2F{10.0f, midY}, D2D1_POINT_2F{st.widthDip - 10.0f, midY},
                            st.brush.Get(), 1.0f);
            continue;
        }

        const D2D1_RECT_F pill{6.0f, row.topDip + 1.0f, st.widthDip - 6.0f, row.bottomDip - 1.0f};
        if (static_cast<int>(i) == st.hoveredIdx && row.item.enabled) {
            st.brush->SetColor(hoverClr);
            st.rt->FillRoundedRectangle(D2D1_ROUNDED_RECT{pill, 6.0f, 6.0f}, st.brush.Get());
        }

        if (row.item.checked) {
            const float cx = 17.0f;
            const float cy = (row.topDip + row.bottomDip) * 0.5f;
            st.brush->SetColor(accClr);
            st.rt->DrawLine(D2D1_POINT_2F{cx - 3.5f, cy}, D2D1_POINT_2F{cx - 0.8f, cy + 3.0f},
                            st.brush.Get(), 1.7f);
            st.rt->DrawLine(D2D1_POINT_2F{cx - 0.8f, cy + 3.0f},
                            D2D1_POINT_2F{cx + 4.5f, cy - 3.0f}, st.brush.Get(), 1.7f);
        }

        const D2D1_RECT_F txtRc{28.0f, row.topDip, st.widthDip - 14.0f, row.bottomDip};
        st.brush->SetColor(row.item.enabled ? textClr : disClr);
        st.rt->DrawTextW(row.item.label.data(), static_cast<UINT32>(row.item.label.size()),
                         st.textFmt.Get(), txtRc, st.brush.Get());
    }

    if (pushed) {
        st.rt->PopLayer();
    }

    st.brush->SetColor(borderClr);
    st.rt->DrawRoundedRectangle(roundRect, st.brush.Get(), 1.0f);

    if (FAILED(st.rt->EndDraw())) {
        return false;
    }

    POINT ptSrc{0, 0};
    SIZE sizeWnd{st.widthPx, st.heightPx};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    return UpdateLayeredWindow(hwnd, screenDc.hdc, nullptr, &sizeWnd, st.memDc, &ptSrc, 0, &blend,
                               ULW_ALPHA) != FALSE;
}

int HitTestRow(const ThemedMenuState& st, int xPx, int yPx) noexcept {
    const float scale = 96.0f / static_cast<float>(st.dpi > 0 ? st.dpi : 96u);
    const float xDip = static_cast<float>(xPx) * scale;
    const float yDip = static_cast<float>(yPx) * scale;
    if (xDip < 0.0f || xDip >= st.widthDip) {
        return -1;
    }
    for (std::size_t i = 0; i < st.rows.size(); ++i) {
        const auto& r = st.rows[i];
        if (!r.item.separator && r.item.enabled && yDip >= r.topDip && yDip < r.bottomDip) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void StepHover(ThemedMenuState& st, int dir) noexcept {
    if (st.rows.empty()) {
        return;
    }
    const int n = static_cast<int>(st.rows.size());
    int cur = st.hoveredIdx;
    for (int step = 0; step < n; ++step) {
        cur = (cur < 0) ? ((dir > 0) ? 0 : n - 1) : ((cur + dir + n) % n);
        if (!st.rows[static_cast<std::size_t>(cur)].item.separator &&
            st.rows[static_cast<std::size_t>(cur)].item.enabled) {
            st.hoveredIdx = cur;
            return;
        }
    }
}

LRESULT CALLBACK ThemedMenuProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<ThemedMenuState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        st = static_cast<ThemedMenuState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        return TRUE;
    }
    if (st == nullptr) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        if (!st->trackingLeave) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            st->trackingLeave = true;
        }
        const int hit = HitTestRow(*st, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hit != st->hoveredIdx) {
            st->hoveredIdx = hit;
            RenderThemedMenu(hwnd, *st);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        st->trackingLeave = false;
        if (st->hoveredIdx != -1) {
            st->hoveredIdx = -1;
            RenderThemedMenu(hwnd, *st);
        }
        return 0;
    case WM_LBUTTONUP: {
        const int hit = HitTestRow(*st, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hit >= 0) {
            st->selectedId = st->rows[static_cast<std::size_t>(hit)].item.id;
        }
        st->done = true;
        return 0;
    }
    case WM_RBUTTONUP:
        st->done = true;
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            st->done = true;
            return 0;
        }
        if (wParam == VK_UP) {
            StepHover(*st, -1);
            RenderThemedMenu(hwnd, *st);
            return 0;
        }
        if (wParam == VK_DOWN) {
            StepHover(*st, 1);
            RenderThemedMenu(hwnd, *st);
            return 0;
        }
        if (wParam == VK_RETURN && st->hoveredIdx >= 0 &&
            static_cast<std::size_t>(st->hoveredIdx) < st->rows.size()) {
            const auto& row = st->rows[static_cast<std::size_t>(st->hoveredIdx)];
            if (!row.item.separator && row.item.enabled) {
                st->selectedId = row.item.id;
            }
            st->done = true;
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            st->closedByDeactivate = true;
            st->done = true;
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        st->closedByDeactivate = true;
        st->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

std::optional<UINT> ShowThemedPopupMenu(HWND owner,
                                        POINT screenPt,
                                        bool isDark,
                                        const std::vector<PopupMenuItem>& items) {
    if (items.empty()) {
        return std::nullopt;
    }

    const HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &ThemedMenuProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kMenuClassName;
    RegisterClassExW(&wc);

    const auto metrics = platform::GetMonitorMetricsForPoint(screenPt);

    ThemedMenuState st;
    st.isDark = isDark;
    st.dpi = metrics.dpi;
    if (!InitMenuResources(st)) {
        return std::nullopt;
    }

    constexpr float kPadY = 6.0f;
    constexpr float kRowH = 28.0f;
    constexpr float kSepH = 9.0f;
    float yDip = kPadY;
    float maxTextW = 80.0f;

    st.rows.reserve(items.size());
    for (const auto& item : items) {
        const float h = item.separator ? kSepH : kRowH;
        st.rows.push_back(MenuRowLayout{item, yDip, yDip + h});
        yDip += h;

        if (!item.separator && !item.label.empty() && st.dwrite && st.textFmt) {
            platform::ComPtr<IDWriteTextLayout> lay;
            if (SUCCEEDED(st.dwrite->CreateTextLayout(item.label.data(),
                                                      static_cast<UINT32>(item.label.size()),
                                                      st.textFmt.Get(), 600.0f, kRowH,
                                                      lay.ReleaseAndGetAddressOf()))) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(lay->GetMetrics(&tm))) {
                    maxTextW = std::max(maxTextW, tm.widthIncludingTrailingWhitespace);
                }
            }
        }
    }

    st.widthDip = std::clamp(28.0f + maxTextW + 18.0f, 168.0f, 360.0f);
    st.heightDip = yDip + kPadY;
    st.widthPx = std::max(120, core::DipToPhysicalPx(st.widthDip, st.dpi));
    st.heightPx = std::max(32, core::DipToPhysicalPx(st.heightDip, st.dpi));

    int posX = screenPt.x;
    int posY = screenPt.y;
    if (posX + st.widthPx > metrics.workArea.right) {
        posX = screenPt.x - st.widthPx;
    }
    if (posY + st.heightPx > metrics.workArea.bottom) {
        posY = screenPt.y - st.heightPx;
    }
    posX = std::clamp(posX, static_cast<int>(metrics.workArea.left),
                      std::max(static_cast<int>(metrics.workArea.left),
                               static_cast<int>(metrics.workArea.right) - st.widthPx));
    posY = std::clamp(posY, static_cast<int>(metrics.workArea.top),
                      std::max(static_cast<int>(metrics.workArea.top),
                               static_cast<int>(metrics.workArea.bottom) - st.heightPx));

    HWND popup = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, kMenuClassName,
                                 L"", WS_POPUP, posX, posY, st.widthPx, st.heightPx, owner,
                                 nullptr, hInst, &st);
    if (popup == nullptr) {
        return std::nullopt;
    }

    const DWORD noRound = static_cast<DWORD>(DWMWCP_DONOTROUND);
    DwmSetWindowAttribute(popup, DWMWA_WINDOW_CORNER_PREFERENCE, &noRound, sizeof(noRound));
    const COLORREF noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(popup, DWMWA_BORDER_COLOR, &noBorder, sizeof(noBorder));

    RenderThemedMenu(popup, st);
    ShowWindow(popup, SW_SHOW);
    SetForegroundWindow(popup);

    MSG msg{};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyWindow(popup);
    if (owner != nullptr && !st.closedByDeactivate) {
        SetForegroundWindow(owner);
    }
    return st.selectedId;
}

std::optional<GroupMenuAction> ShowGroupContextMenu(HWND owner,
                                                    POINT screenPt,
                                                    bool isDark,
                                                    bool clickedOnGroup,
                                                    bool isLinkedFolder,
                                                    std::size_t groupIndex,
                                                    std::size_t groupCount,
                                                    bool englishUi) {
    std::vector<PopupMenuItem> items;
    items.push_back({static_cast<UINT>(GroupMenuAction::NewManualGroup),
                     englishUi ? L"New Group..." : L"\u65B0\u5EFA\u666E\u901A\u5206\u7EC4..."});
    items.push_back({static_cast<UINT>(GroupMenuAction::NewLinkedFolderGroup),
                     englishUi ? L"New Linked Folder Group..."
                               : L"\u65B0\u5EFA\u5173\u8054\u6587\u4EF6\u5939\u5206\u7EC4..."});

    if (clickedOnGroup) {
        items.push_back({0, L"", true, false, false});
        items.push_back({static_cast<UINT>(GroupMenuAction::RenameGroup),
                         englishUi ? L"Rename Group..." : L"\u91CD\u547D\u540D\u5206\u7EC4..."});
        if (isLinkedFolder) {
            items.push_back({static_cast<UINT>(GroupMenuAction::SyncLinkedGroup),
                             englishUi ? L"Sync Folder Now" : L"\u7ACB\u5373\u540C\u6B65\u8BE5\u6587\u4EF6\u5939"});
        }
        items.push_back({static_cast<UINT>(GroupMenuAction::MoveGroupUp),
                         englishUi ? L"Move Up" : L"\u4E0A\u79FB\u5206\u7EC4",
                         false, groupIndex > 0, false});
        items.push_back({static_cast<UINT>(GroupMenuAction::MoveGroupDown),
                         englishUi ? L"Move Down" : L"\u4E0B\u79FB\u5206\u7EC4",
                         false, groupIndex + 1 < groupCount, false});
        items.push_back({0, L"", true, false, false});
        items.push_back({static_cast<UINT>(GroupMenuAction::DeleteGroup),
                         englishUi ? L"Delete Group" : L"\u5220\u9664\u5206\u7EC4"});
    }

    const auto cmd = ShowThemedPopupMenu(owner, screenPt, isDark, items);
    if (!cmd.has_value()) {
        return std::nullopt;
    }
    return static_cast<GroupMenuAction>(*cmd);
}

std::optional<ItemMenuAction> ShowItemContextMenu(HWND owner,
                                                  POINT screenPt,
                                                  bool isDark,
                                                  bool clickedOnItem,
                                                  bool isManualGroup,
                                                  bool runAsAdmin,
                                                  core::SortOrder currentItemSort,
                                                  bool englishUi) {
    std::vector<PopupMenuItem> items;
    if (clickedOnItem) {
        items.push_back({static_cast<UINT>(ItemMenuAction::OpenItem),
                         englishUi ? L"Open / Launch" : L"\u6253\u5F00 / \u542F\u52A8"});
        items.push_back({static_cast<UINT>(ItemMenuAction::RunAsAdminOnce),
                         englishUi ? L"Run as Administrator" : L"\u4EE5\u7BA1\u7406\u5458\u8EAB\u4EFD\u8FD0\u884C"});
        items.push_back({static_cast<UINT>(ItemMenuAction::OpenLocation),
                         englishUi ? L"Open File Location" : L"\u6253\u5F00\u6240\u5728\u4F4D\u7F6E"});
        items.push_back({0, L"", true, false, false});
        items.push_back({static_cast<UINT>(ItemMenuAction::EditItem),
                         englishUi ? L"Properties..." : L"\u7F16\u8F91\u5C5E\u6027..."});
        items.push_back({static_cast<UINT>(ItemMenuAction::ToggleRunAsAdmin),
                         englishUi ? L"Always Run as Admin" : L"\u9ED8\u8BA4\u4EE5\u7BA1\u7406\u5458\u6743\u9650\u542F\u52A8",
                         false, true, runAsAdmin});
        if (isManualGroup) {
            items.push_back({0, L"", true, false, false});
            items.push_back({static_cast<UINT>(ItemMenuAction::DeleteItem),
                             englishUi ? L"Remove from Group" : L"\u4ECE\u5206\u7EC4\u79FB\u9664"});
        }
        items.push_back({0, L"", true, false, false});
    }

    if (isManualGroup) {
        items.push_back({static_cast<UINT>(ItemMenuAction::AddFile),
                         englishUi ? L"Add File / App..." : L"\u6DFB\u52A0\u6587\u4EF6 / \u5E94\u7528..."});
        items.push_back({static_cast<UINT>(ItemMenuAction::AddFolder),
                         englishUi ? L"Add Folder..." : L"\u6DFB\u52A0\u6587\u4EF6\u5939..."});
        items.push_back({static_cast<UINT>(ItemMenuAction::AddCustom),
                         englishUi ? L"Add URL / Custom..." : L"\u6DFB\u52A0\u7F51\u5740 / \u81EA\u5B9A\u4E49\u6761\u76EE..."});
        items.push_back({0, L"", true, false, false});
    }

    items.push_back({static_cast<UINT>(ItemMenuAction::SortCustom),
                     englishUi ? L"Sort: Custom" : L"\u6392\u5E8F\uFF1A\u81EA\u5B9A\u4E49",
                     false, true, currentItemSort == core::SortOrder::Custom});
    items.push_back({static_cast<UINT>(ItemMenuAction::SortCountDesc),
                     englishUi ? L"Sort: Most Launched" : L"\u6392\u5E8F\uFF1A\u542F\u52A8\u6B21\u6570\u4F18\u5148",
                     false, true, currentItemSort == core::SortOrder::LaunchCountDesc});
    items.push_back({static_cast<UINT>(ItemMenuAction::SortNameAsc),
                     englishUi ? L"Sort: Name A-Z" : L"\u6392\u5E8F\uFF1A\u6309\u540D\u79F0 A-Z",
                     false, true, currentItemSort == core::SortOrder::NameAsc});

    const auto cmd = ShowThemedPopupMenu(owner, screenPt, isDark, items);
    if (!cmd.has_value()) {
        return std::nullopt;
    }
    return static_cast<ItemMenuAction>(*cmd);
}

} // namespace geekdesk::ui
