#include "ui/d2d_renderer.hpp"

#include "core/placement_math.hpp"
#include "core/utf8.hpp"
#include "platform/backdrop.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace geekdesk::ui {

namespace {

constexpr float kOuterPad = 10.0f;
constexpr float kHeaderH = 44.0f;
constexpr float kStatusH = 22.0f;
constexpr float kBodyGap = 8.0f;
constexpr float kGroupRowH = 34.0f;
constexpr float kGroupGap = 4.0f;
constexpr float kGridGap = 10.0f;

D2D1_COLOR_F ToD2D(ColorRgba c, float alphaMult = 1.0f) noexcept {
    return D2D1_COLOR_F{c.r, c.g, c.b, std::clamp(c.a * alphaMult, 0.0f, 1.0f)};
}

D2D1_RECT_F ToRect(const RectDip& r) noexcept {
    return D2D1_RECT_F{r.left, r.top, r.right, r.bottom};
}

D2D1_ROUNDED_RECT ToRounded(const RectDip& r, float rx, float ry) noexcept {
    return D2D1_ROUNDED_RECT{ToRect(r), rx, ry};
}

std::wstring BuildInitialBadge(std::string_view name) {
    if (name.empty()) {
        return L"?";
    }
    std::size_t off = 0;
    const char32_t cp = core::utf8::NextUtf8Codepoint(name, off);
    std::wstring wide = core::utf8::Utf8ToWide(name.substr(0, off));
    if (cp >= U'a' && cp <= U'z' && !wide.empty()) {
        wide[0] = static_cast<wchar_t>(wide[0] - L'a' + L'A');
    }
    return wide.empty() ? std::wstring(L"?") : wide;
}

void DrawMagnifierIcon(ID2D1RenderTarget* rt, ID2D1Brush* brush, float cx, float cy) {
    rt->DrawEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx - 1.5f, cy - 1.5f}, 4.5f, 4.5f}, brush, 1.5f);
    rt->DrawLine(D2D1_POINT_2F{cx + 1.8f, cy + 1.8f}, D2D1_POINT_2F{cx + 5.5f, cy + 5.5f}, brush,
                 1.6f);
}

void DrawPinIcon(ID2D1RenderTarget* rt, ID2D1Brush* brush, float cx, float cy, bool pinned) {
    if (pinned) {
        rt->FillEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx, cy - 2.0f}, 4.2f, 4.2f}, brush);
    } else {
        rt->DrawEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx, cy - 2.0f}, 4.0f, 4.0f}, brush, 1.5f);
    }
    rt->DrawLine(D2D1_POINT_2F{cx, cy + 2.2f}, D2D1_POINT_2F{cx, cy + 7.0f}, brush, 1.6f);
}

void DrawRefreshIcon(ID2D1RenderTarget* rt, ID2D1Brush* brush, float cx, float cy) {
    rt->DrawEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx, cy}, 5.0f, 5.0f}, brush, 1.5f);
    rt->DrawLine(D2D1_POINT_2F{cx + 2.5f, cy - 6.0f}, D2D1_POINT_2F{cx + 5.5f, cy - 3.0f}, brush,
                 1.5f);
    rt->DrawLine(D2D1_POINT_2F{cx + 5.5f, cy - 3.0f}, D2D1_POINT_2F{cx + 2.0f, cy - 1.5f}, brush,
                 1.5f);
}

void DrawCloseIcon(ID2D1RenderTarget* rt, ID2D1Brush* brush, float cx, float cy) {
    rt->DrawLine(D2D1_POINT_2F{cx - 4.5f, cy - 4.5f}, D2D1_POINT_2F{cx + 4.5f, cy + 4.5f}, brush,
                 1.6f);
    rt->DrawLine(D2D1_POINT_2F{cx + 4.5f, cy - 4.5f}, D2D1_POINT_2F{cx - 4.5f, cy + 4.5f}, brush,
                 1.6f);
}

void DrawSettingsGearIcon(ID2D1RenderTarget* rt, ID2D1Brush* brush, float cx, float cy) {
    rt->DrawEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx, cy}, 4.5f, 4.5f}, brush, 1.5f);
    rt->FillEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx, cy}, 1.7f, 1.7f}, brush);
    constexpr float kOuter = 7.0f;
    constexpr float kInner = 5.0f;
    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * 0.78539816f;
        const float cs = std::cos(angle);
        const float sn = std::sin(angle);
        rt->DrawLine(D2D1_POINT_2F{cx + cs * kInner, cy + sn * kInner},
                     D2D1_POINT_2F{cx + cs * kOuter, cy + sn * kOuter}, brush, 1.4f);
    }
}

void DrawFolderGlyph(ID2D1RenderTarget* rt, ID2D1Brush* brush, float left, float cy) {
    const D2D1_RECT_F tab{left, cy - 5.5f, left + 6.0f, cy - 2.5f};
    const D2D1_RECT_F body{left, cy - 3.5f, left + 13.0f, cy + 5.5f};
    rt->DrawRoundedRectangle(D2D1_ROUNDED_RECT{tab, 1.0f, 1.0f}, brush, 1.2f);
    rt->DrawRoundedRectangle(D2D1_ROUNDED_RECT{body, 1.5f, 1.5f}, brush, 1.3f);
}

void BoxBlurPbgra(BgraImage& img, int radius, int passes) {
    if (!img.IsValid() || radius <= 0 || passes <= 0) {
        return;
    }
    const int w = img.width;
    const int h = img.height;
    std::vector<std::uint8_t> tmp(img.pixels.size(), 0);

    for (int pass = 0; pass < passes; ++pass) {
        // Horizontal pass: img -> tmp
        for (int y = 0; y < h; ++y) {
            const std::size_t rowOff = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4u;
            int sumB = 0;
            int sumG = 0;
            int sumR = 0;
            int sumA = 0;
            const int win = radius * 2 + 1;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = std::clamp(k, 0, w - 1);
                const std::size_t off = rowOff + static_cast<std::size_t>(sx) * 4u;
                sumB += img.pixels[off + 0];
                sumG += img.pixels[off + 1];
                sumR += img.pixels[off + 2];
                sumA += img.pixels[off + 3];
            }
            for (int x = 0; x < w; ++x) {
                const std::size_t dstOff = rowOff + static_cast<std::size_t>(x) * 4u;
                tmp[dstOff + 0] = static_cast<std::uint8_t>(sumB / win);
                tmp[dstOff + 1] = static_cast<std::uint8_t>(sumG / win);
                tmp[dstOff + 2] = static_cast<std::uint8_t>(sumR / win);
                tmp[dstOff + 3] = static_cast<std::uint8_t>(sumA / win);

                const int remX = std::clamp(x - radius, 0, w - 1);
                const int addX = std::clamp(x + radius + 1, 0, w - 1);
                const std::size_t remOff = rowOff + static_cast<std::size_t>(remX) * 4u;
                const std::size_t addOff = rowOff + static_cast<std::size_t>(addX) * 4u;
                sumB += static_cast<int>(img.pixels[addOff + 0]) - static_cast<int>(img.pixels[remOff + 0]);
                sumG += static_cast<int>(img.pixels[addOff + 1]) - static_cast<int>(img.pixels[remOff + 1]);
                sumR += static_cast<int>(img.pixels[addOff + 2]) - static_cast<int>(img.pixels[remOff + 2]);
                sumA += static_cast<int>(img.pixels[addOff + 3]) - static_cast<int>(img.pixels[remOff + 3]);
            }
        }

        // Vertical pass: tmp -> img
        for (int x = 0; x < w; ++x) {
            const std::size_t colOff = static_cast<std::size_t>(x) * 4u;
            const std::size_t stride = static_cast<std::size_t>(w) * 4u;
            int sumB = 0;
            int sumG = 0;
            int sumR = 0;
            int sumA = 0;
            const int win = radius * 2 + 1;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = std::clamp(k, 0, h - 1);
                const std::size_t off = static_cast<std::size_t>(sy) * stride + colOff;
                sumB += tmp[off + 0];
                sumG += tmp[off + 1];
                sumR += tmp[off + 2];
                sumA += tmp[off + 3];
            }
            for (int y = 0; y < h; ++y) {
                const std::size_t dstOff = static_cast<std::size_t>(y) * stride + colOff;
                img.pixels[dstOff + 0] = static_cast<std::uint8_t>(sumB / win);
                img.pixels[dstOff + 1] = static_cast<std::uint8_t>(sumG / win);
                img.pixels[dstOff + 2] = static_cast<std::uint8_t>(sumR / win);
                img.pixels[dstOff + 3] = static_cast<std::uint8_t>(sumA / win);

                const int remY = std::clamp(y - radius, 0, h - 1);
                const int addY = std::clamp(y + radius + 1, 0, h - 1);
                const std::size_t remOff = static_cast<std::size_t>(remY) * stride + colOff;
                const std::size_t addOff = static_cast<std::size_t>(addY) * stride + colOff;
                sumB += static_cast<int>(tmp[addOff + 0]) - static_cast<int>(tmp[remOff + 0]);
                sumG += static_cast<int>(tmp[addOff + 1]) - static_cast<int>(tmp[remOff + 1]);
                sumR += static_cast<int>(tmp[addOff + 2]) - static_cast<int>(tmp[remOff + 2]);
                sumA += static_cast<int>(tmp[addOff + 3]) - static_cast<int>(tmp[remOff + 3]);
            }
        }
    }
}

struct ScreenDcGuard {
    HDC hdc = nullptr;
    ~ScreenDcGuard() {
        if (hdc != nullptr) {
            ReleaseDC(nullptr, hdc);
        }
    }
};

} // namespace

D2DRenderer::~D2DRenderer() {
    Shutdown();
}

bool D2DRenderer::Initialize() {
    return EnsureFactories();
}

void D2DRenderer::Shutdown() {
    DiscardTargetAndBitmaps();
    ReleaseBackBuffer();
    emptyFmt_.Reset();
    statusFmt_.Reset();
    badgeFmt_.Reset();
    itemTitleFmt_.Reset();
    searchFmt_.Reset();
    countFmt_.Reset();
    groupFmt_.Reset();
    brandFmt_.Reset();
    dwriteFactory_.Reset();
    factory_.Reset();
}

bool D2DRenderer::EnsureFactories() {
    if (!factory_) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
                                     nullptr, reinterpret_cast<void**>(factory_.ReleaseAndGetAddressOf())))) {
            return false;
        }
    }
    if (!dwriteFactory_) {
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf())))) {
            return false;
        }
    }

    auto makeFmt = [this](platform::ComPtr<IDWriteTextFormat>& out, float size,
                          DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT hAlign,
                          DWRITE_PARAGRAPH_ALIGNMENT vAlign, bool wrap, bool ellipsis) {
        if (out) {
            return true;
        }
        if (FAILED(dwriteFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, weight,
                                                    DWRITE_FONT_STYLE_NORMAL,
                                                    DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN",
                                                    out.ReleaseAndGetAddressOf()))) {
            return false;
        }
        out->SetTextAlignment(hAlign);
        out->SetParagraphAlignment(vAlign);
        out->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        if (ellipsis) {
            platform::ComPtr<IDWriteInlineObject> localTrim;
            if (SUCCEEDED(dwriteFactory_->CreateEllipsisTrimmingSign(
                    out.Get(), localTrim.ReleaseAndGetAddressOf())) &&
                localTrim) {
                DWRITE_TRIMMING trim{};
                trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
                out->SetTrimming(&trim, localTrim.Get());
            }
        }
        return true;
    };

    return makeFmt(brandFmt_, 14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING,
                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false, true) &&
           makeFmt(groupFmt_, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false, true) &&
           makeFmt(countFmt_, 11.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING,
                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false, false) &&
           makeFmt(searchFmt_, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false, false) &&
           makeFmt(itemTitleFmt_, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER,
                   DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true, true) &&
           makeFmt(badgeFmt_, 18.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false, false) &&
           makeFmt(statusFmt_, 11.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false, true) &&
           makeFmt(emptyFmt_, 14.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER,
                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true, false);
}

bool D2DRenderer::EnsureRenderTarget(std::uint32_t dpi) {
    if (!EnsureFactories()) {
        return false;
    }
    if (!dcTarget_) {
        D2D1_RENDER_TARGET_PROPERTIES props{};
        props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
        props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        props.dpiX = static_cast<float>(dpi > 0 ? dpi : 96);
        props.dpiY = props.dpiX;
        props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
        props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

        if (FAILED(factory_->CreateDCRenderTarget(&props, dcTarget_.ReleaseAndGetAddressOf()))) {
            return false;
        }
        dcTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        solidBrush_.Reset();
        clipLayer_.Reset();
    } else if (dpi > 0) {
        dcTarget_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    }

    if (!solidBrush_) {
        if (FAILED(dcTarget_->CreateSolidColorBrush(
                D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f}, solidBrush_.ReleaseAndGetAddressOf()))) {
            return false;
        }
    }
    if (!clipLayer_) {
        dcTarget_->CreateLayer(nullptr, clipLayer_.ReleaseAndGetAddressOf());
    }
    return true;
}

bool D2DRenderer::EnsureBackBuffer(HDC referenceDc, int widthPx, int heightPx) {
    if (widthPx <= 0 || heightPx <= 0) {
        return false;
    }
    if (memDc_ != nullptr && memBitmap_ != nullptr && widthPx == backWidthPx_ &&
        heightPx == backHeightPx_) {
        return true;
    }

    ReleaseBackBuffer();

    memDc_ = CreateCompatibleDC(referenceDc);
    if (memDc_ == nullptr) {
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = widthPx;
    bmi.bmiHeader.biHeight = -heightPx; // Top-down DIB matching Direct2D orientation
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* dibBits = nullptr;
    memBitmap_ = CreateDIBSection(memDc_, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    if (memBitmap_ == nullptr) {
        DeleteDC(memDc_);
        memDc_ = nullptr;
        return false;
    }
    oldBitmap_ = SelectObject(memDc_, memBitmap_);
    backWidthPx_ = widthPx;
    backHeightPx_ = heightPx;
    return true;
}

void D2DRenderer::ReleaseBackBuffer() {
    if (memDc_ != nullptr) {
        if (oldBitmap_ != nullptr) {
            SelectObject(memDc_, oldBitmap_);
            oldBitmap_ = nullptr;
        }
        if (memBitmap_ != nullptr) {
            DeleteObject(memBitmap_);
            memBitmap_ = nullptr;
        }
        DeleteDC(memDc_);
        memDc_ = nullptr;
    }
    backWidthPx_ = 0;
    backHeightPx_ = 0;
}

void D2DRenderer::DiscardTargetAndBitmaps() {
    wallpaperBitmap_.Reset();
    cachedWallpaperKey_.clear();
    wallpaperLoadAttempted_ = false;
    desktopMicaBitmap_.Reset();
    cachedDesktopWallpaperKey_.clear();
    desktopMicaLoadAttempted_ = false;
    iconBitmaps_.clear();
    bitmapLru_.clear();
    clipLayer_.Reset();
    solidBrush_.Reset();
    dcTarget_.Reset();
}

void D2DRenderer::InvalidateWallpaperCache() {
    wallpaperBitmap_.Reset();
    cachedWallpaperKey_.clear();
    wallpaperLoadAttempted_ = false;
}

void D2DRenderer::InvalidateDesktopWallpaperCache() {
    desktopMicaBitmap_.Reset();
    cachedDesktopWallpaperKey_.clear();
    desktopMicaLoadAttempted_ = false;
}

LayoutSnapshot D2DRenderer::ComputeLayout(const core::AppSettings& settings,
                                          float widthDip,
                                          float heightDip,
                                          std::size_t groupCount,
                                          std::size_t itemCount,
                                          float scrollOffsetDip) {
    LayoutSnapshot snap;
    snap.widthDip = std::max(widthDip, 320.0f);
    snap.heightDip = std::max(heightDip, 240.0f);

    const float sidebarW =
        static_cast<float>(std::clamp(settings.window.sidebarWidth, 120.0, 260.0));

    snap.headerRect = {kOuterPad, 6.0f, snap.widthDip - kOuterPad, 6.0f + kHeaderH};
    snap.brandRect = {kOuterPad + 8.0f, snap.headerRect.top, kOuterPad + sidebarW,
                      snap.headerRect.bottom};

    const float btnSize = 28.0f;
    const float btnGap = 6.0f;
    const float btnY = snap.headerRect.top + (kHeaderH - btnSize) * 0.5f;
    float btnRight = snap.headerRect.right;
    for (int i = 3; i >= 0; --i) {
        snap.headerButtonRects[static_cast<std::size_t>(i)] = {
            btnRight - btnSize, btnY, btnRight, btnY + btnSize};
        btnRight -= (btnSize + btnGap);
    }

    const float searchLeft = snap.brandRect.right + kBodyGap;
    const float searchRight = std::max(searchLeft + 120.0f, btnRight - 8.0f);
    const float searchH = 30.0f;
    const float searchTop = snap.headerRect.top + (kHeaderH - searchH) * 0.5f;
    snap.searchBoxRect = {searchLeft, searchTop, searchRight, searchTop + searchH};
    snap.searchCaretX = snap.searchBoxRect.left + 30.0f;
    snap.searchCaretY = snap.searchBoxRect.top + 6.0f;
    snap.searchCaretHeight = searchH - 12.0f;

    const float bodyTop = snap.headerRect.bottom + 4.0f;
    const float bodyBottom = std::max(bodyTop + 100.0f, snap.heightDip - kStatusH - 4.0f);
    snap.statusBarRect = {kOuterPad, bodyBottom + 2.0f, snap.widthDip - kOuterPad,
                          snap.heightDip - 2.0f};

    snap.sidebarRect = {kOuterPad, bodyTop, kOuterPad + sidebarW, bodyBottom};
    const float groupPad = 6.0f;
    float gy = snap.sidebarRect.top + groupPad;
    snap.groupRects.reserve(groupCount);
    for (std::size_t i = 0; i < groupCount; ++i) {
        if (gy + kGroupRowH > snap.sidebarRect.bottom - 4.0f) {
            snap.groupRects.push_back(RectDip{0, 0, 0, 0});
        } else {
            snap.groupRects.push_back(
                RectDip{snap.sidebarRect.left + groupPad, gy,
                        snap.sidebarRect.right - groupPad, gy + kGroupRowH});
        }
        gy += kGroupRowH + kGroupGap;
    }

    snap.contentCardRect = {snap.sidebarRect.right + kBodyGap, bodyTop, snap.widthDip - kOuterPad,
                            bodyBottom};
    const float gridPad = 10.0f;
    snap.gridViewportRect = {snap.contentCardRect.left + gridPad,
                             snap.contentCardRect.top + gridPad,
                             std::max(snap.contentCardRect.left + gridPad + 80.0f,
                                      snap.contentCardRect.right - gridPad - 8.0f),
                             snap.contentCardRect.bottom - gridPad};

    const float iconSize = static_cast<float>(std::clamp(settings.appearance.iconSize, 28, 96));
    const float tileW = iconSize + 40.0f;
    const float tileH =
        settings.appearance.showItemTitle ? (iconSize + 44.0f) : (iconSize + 20.0f);
    snap.rowStrideDip = tileH + kGridGap;

    const int cols = core::ComputeGridColumns(
        static_cast<int>(std::floor(snap.gridViewportRect.Width())), static_cast<int>(tileW),
        static_cast<int>(kGridGap), 1);
    snap.gridColumns = cols;

    const float usedGridW = cols * tileW + (cols - 1) * kGridGap;
    const float gridStartX =
        snap.gridViewportRect.left +
        std::max(0.0f, (snap.gridViewportRect.Width() - usedGridW) * 0.5f);

    const std::size_t rows = (itemCount == 0) ? 0u : ((itemCount + static_cast<std::size_t>(cols) - 1u) /
                                                      static_cast<std::size_t>(cols));
    snap.totalContentHeightDip =
        (rows == 0) ? 0.0f : (static_cast<float>(rows) * tileH + static_cast<float>(rows - 1) * kGridGap);
    snap.maxScrollDip =
        std::max(0.0f, snap.totalContentHeightDip - snap.gridViewportRect.Height());
    snap.clampedScrollDip = std::clamp(scrollOffsetDip, 0.0f, snap.maxScrollDip);

    snap.tileRects.reserve(itemCount);
    for (std::size_t i = 0; i < itemCount; ++i) {
        const std::size_t r = i / static_cast<std::size_t>(cols);
        const std::size_t c = i % static_cast<std::size_t>(cols);
        const float left = gridStartX + static_cast<float>(c) * (tileW + kGridGap);
        const float top = snap.gridViewportRect.top +
                          static_cast<float>(r) * (tileH + kGridGap) - snap.clampedScrollDip;
        snap.tileRects.push_back(RectDip{left, top, left + tileW, top + tileH});
    }

    if (snap.maxScrollDip > 1.0f) {
        snap.scrollbarTrackRect = {snap.contentCardRect.right - 7.0f,
                                   snap.gridViewportRect.top,
                                   snap.contentCardRect.right - 3.0f,
                                   snap.gridViewportRect.bottom};
        const float viewH = snap.scrollbarTrackRect.Height();
        const float ratio = std::clamp(viewH / snap.totalContentHeightDip, 0.12f, 1.0f);
        const float thumbH = std::max(24.0f, viewH * ratio);
        const float thumbTravel = std::max(0.0f, viewH - thumbH);
        const float thumbTop =
            snap.scrollbarTrackRect.top + thumbTravel * (snap.clampedScrollDip / snap.maxScrollDip);
        snap.scrollbarThumbRect = {snap.scrollbarTrackRect.left, thumbTop,
                                   snap.scrollbarTrackRect.right, thumbTop + thumbH};
    }

    return snap;
}

ID2D1Bitmap* D2DRenderer::GetOrUploadIconBitmap(const core::LaunchItem& item,
                                                IconCache& iconCache) {
    if (!dcTarget_) {
        return nullptr;
    }
    const std::string key = IconCache::BuildMemoryKey(item);
    if (key.empty()) {
        return nullptr;
    }
    if (auto it = iconBitmaps_.find(key); it != iconBitmaps_.end()) {
        bitmapLru_.splice(bitmapLru_.begin(), bitmapLru_, it->second.lruIt);
        return it->second.bitmap.Get();
    }

    BgraImage img;
    if (!iconCache.TryGetOrRequest(item, img) || !img.IsValid()) {
        return nullptr;
    }

    D2D1_BITMAP_PROPERTIES props{};
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;

    platform::ComPtr<ID2D1Bitmap> bmp;
    const D2D1_SIZE_U sz{static_cast<UINT32>(img.width), static_cast<UINT32>(img.height)};
    if (FAILED(dcTarget_->CreateBitmap(sz, img.pixels.data(), static_cast<UINT32>(img.width * 4),
                                       &props, bmp.ReleaseAndGetAddressOf()))) {
        return nullptr;
    }

    while (iconBitmaps_.size() >= kMaxBitmapEntries && !bitmapLru_.empty()) {
        iconBitmaps_.erase(bitmapLru_.back());
        bitmapLru_.pop_back();
    }
    bitmapLru_.push_front(key);
    auto [insertedIt, ok] =
        iconBitmaps_.emplace(key, BitmapEntry{std::move(bmp), bitmapLru_.begin()});
    (void)ok;
    return insertedIt->second.bitmap.Get();
}

ID2D1Bitmap* D2DRenderer::GetOrLoadWallpaperBitmap(const std::filesystem::path& dataRoot,
                                                   const std::string& fileName) {
    if (!dcTarget_ || fileName.empty()) {
        return nullptr;
    }
    const std::string key = core::utf8::PathToUtf8(dataRoot) + "|" + fileName;
    if (key == cachedWallpaperKey_ && wallpaperLoadAttempted_) {
        return wallpaperBitmap_.Get();
    }

    cachedWallpaperKey_ = key;
    wallpaperLoadAttempted_ = true;
    wallpaperBitmap_.Reset();

    const std::filesystem::path full =
        dataRoot / L"assets" / core::utf8::PathFromUtf8(fileName).filename();
    std::error_code ec;
    if (!std::filesystem::exists(full, ec)) {
        return nullptr;
    }

    BgraImage img;
    if (!IconCache::LoadImageFileToPbgra(full, 1920, img) || !img.IsValid()) {
        return nullptr;
    }

    D2D1_BITMAP_PROPERTIES props{};
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    dcTarget_->CreateBitmap(D2D1_SIZE_U{static_cast<UINT32>(img.width),
                                        static_cast<UINT32>(img.height)},
                            img.pixels.data(), static_cast<UINT32>(img.width * 4), &props,
                            wallpaperBitmap_.ReleaseAndGetAddressOf());
    return wallpaperBitmap_.Get();
}

ID2D1Bitmap* D2DRenderer::GetOrLoadDesktopMicaBitmap() {
    if (!dcTarget_) {
        return nullptr;
    }
    const std::filesystem::path wpPath = platform::GetDesktopWallpaperPath();
    if (wpPath.empty()) {
        cachedDesktopWallpaperKey_.clear();
        desktopMicaLoadAttempted_ = true;
        desktopMicaBitmap_.Reset();
        return nullptr;
    }

    const std::string key = core::utf8::PathToUtf8(wpPath);
    if (key == cachedDesktopWallpaperKey_ && desktopMicaLoadAttempted_) {
        return desktopMicaBitmap_.Get();
    }

    cachedDesktopWallpaperKey_ = key;
    desktopMicaLoadAttempted_ = true;
    desktopMicaBitmap_.Reset();

    BgraImage img;
    if (!IconCache::LoadImageFileToPbgra(wpPath, 192, img) || !img.IsValid()) {
        return nullptr;
    }
    BoxBlurPbgra(img, 12, 3);

    D2D1_BITMAP_PROPERTIES props{};
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    dcTarget_->CreateBitmap(D2D1_SIZE_U{static_cast<UINT32>(img.width),
                                        static_cast<UINT32>(img.height)},
                            img.pixels.data(), static_cast<UINT32>(img.width * 4), &props,
                            desktopMicaBitmap_.ReleaseAndGetAddressOf());
    return desktopMicaBitmap_.Get();
}

bool D2DRenderer::RenderToLayeredWindow(HWND hwnd,
                                        int widthPx,
                                        int heightPx,
                                        std::uint32_t dpi,
                                        const RenderState& state,
                                        LayoutSnapshot& outLayout) {
    if (hwnd == nullptr || widthPx <= 0 || heightPx <= 0 || state.settings == nullptr ||
        state.palette == nullptr || state.groups == nullptr || state.displayedItems == nullptr) {
        return false;
    }

    ScreenDcGuard screenDc;
    screenDc.hdc = GetDC(nullptr);
    if (screenDc.hdc == nullptr) {
        return false;
    }

    if (!EnsureRenderTarget(dpi) || !EnsureBackBuffer(screenDc.hdc, widthPx, heightPx)) {
        return false;
    }

    const float safeDpi = static_cast<float>(dpi > 0 ? dpi : 96u);
    const float widthDip = static_cast<float>(widthPx) * (96.0f / safeDpi);
    const float heightDip = static_cast<float>(heightPx) * (96.0f / safeDpi);

    outLayout = ComputeLayout(*state.settings, widthDip, heightDip, state.groups->size(),
                              state.displayedItems->size(), state.scrollOffsetDip);

    const RECT bindRect{0, 0, widthPx, heightPx};
    if (FAILED(dcTarget_->BindDC(memDc_, &bindRect))) {
        DiscardTargetAndBitmaps();
        return false;
    }

    const ThemePalette& pal = *state.palette;
    const auto& app = state.settings->appearance;
    const float bgOpacity = std::clamp(static_cast<float>(app.backgroundOpacity), 0.0f, 1.0f);
    const float cornerR = std::clamp(static_cast<float>(app.cornerRadius), 0.0f,
                                     std::min(24.0f, std::min(widthDip, heightDip) * 0.45f));
    const D2D1_RECT_F fullRect{0.0f, 0.0f, widthDip, heightDip};
    const D2D1_ROUNDED_RECT windowRound{
        D2D1_RECT_F{0.5f, 0.5f, std::max(1.5f, widthDip - 0.5f), std::max(1.5f, heightDip - 0.5f)},
        cornerR, cornerR};

    dcTarget_->BeginDraw();
    dcTarget_->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});

    bool pushedLayer = false;
    if (cornerR > 0.5f && clipLayer_ && factory_) {
        platform::ComPtr<ID2D1RoundedRectangleGeometry> clipGeo;
        if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(windowRound,
                                                               clipGeo.ReleaseAndGetAddressOf()))) {
            D2D1_LAYER_PARAMETERS lp{};
            lp.contentBounds = fullRect;
            lp.geometricMask = clipGeo.Get();
            lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
            lp.maskTransform._11 = 1.0f;
            lp.maskTransform._22 = 1.0f;
            lp.opacity = 1.0f;
            lp.opacityBrush = nullptr;
            lp.layerOptions = D2D1_LAYER_OPTIONS_NONE;
            dcTarget_->PushLayer(&lp, clipLayer_.Get());
            pushedLayer = true;
        }
    }

    // 1. Background surface (Gradient / Wallpaper / Mica / MicaAlt / Acrylic)
    if (app.backdrop == core::BackdropMode::Gradient) {
        const float alpha = std::clamp(0.22f + 0.78f * bgOpacity, 0.22f, 1.0f);
        D2D1_COLOR_F s0 = ToD2D(pal.gradientStart);
        D2D1_COLOR_F s1 = ToD2D(pal.gradientEnd);
        s0.a = alpha;
        s1.a = alpha;
        D2D1_GRADIENT_STOP stops[2] = {
            {0.0f, s0},
            {1.0f, s1},
        };
        platform::ComPtr<ID2D1GradientStopCollection> stopCollection;
        if (SUCCEEDED(dcTarget_->CreateGradientStopCollection(
                stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                stopCollection.ReleaseAndGetAddressOf()))) {
            platform::ComPtr<ID2D1LinearGradientBrush> gradBrush;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lbp{
                D2D1_POINT_2F{0.0f, 0.0f}, D2D1_POINT_2F{widthDip, heightDip}};
            if (SUCCEEDED(dcTarget_->CreateLinearGradientBrush(
                    lbp, stopCollection.Get(), gradBrush.ReleaseAndGetAddressOf()))) {
                dcTarget_->FillRectangle(fullRect, gradBrush.Get());
            } else {
                solidBrush_->SetColor(D2D1_COLOR_F{pal.windowTint.r, pal.windowTint.g,
                                                   pal.windowTint.b, alpha});
                dcTarget_->FillRectangle(fullRect, solidBrush_.Get());
            }
        } else {
            solidBrush_->SetColor(
                D2D1_COLOR_F{pal.windowTint.r, pal.windowTint.g, pal.windowTint.b, alpha});
            dcTarget_->FillRectangle(fullRect, solidBrush_.Get());
        }
    } else if (app.backdrop == core::BackdropMode::Wallpaper) {
        ID2D1Bitmap* wp = app.wallpaperFileName.has_value()
                              ? GetOrLoadWallpaperBitmap(state.dataRoot, *app.wallpaperFileName)
                              : nullptr;
        if (wp != nullptr) {
            const float wpAlpha = std::clamp(0.22f + 0.78f * bgOpacity, 0.22f, 1.0f);
            const D2D1_SIZE_F bmpSz = wp->GetSize();
            if (bmpSz.width > 0.0f && bmpSz.height > 0.0f) {
                const float scale = std::max(widthDip / bmpSz.width, heightDip / bmpSz.height);
                const float drawW = bmpSz.width * scale;
                const float drawH = bmpSz.height * scale;
                const D2D1_RECT_F dest{(widthDip - drawW) * 0.5f, (heightDip - drawH) * 0.5f,
                                       (widthDip + drawW) * 0.5f, (heightDip + drawH) * 0.5f};
                dcTarget_->DrawBitmap(wp, dest, wpAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                solidBrush_->SetColor(
                    D2D1_COLOR_F{pal.windowTint.r, pal.windowTint.g, pal.windowTint.b,
                                 0.18f * (0.35f + 0.65f * bgOpacity)});
                dcTarget_->FillRectangle(fullRect, solidBrush_.Get());
            }
        } else {
            solidBrush_->SetColor(
                D2D1_COLOR_F{pal.windowTint.r, pal.windowTint.g, pal.windowTint.b,
                             std::clamp(0.22f + 0.75f * bgOpacity, 0.22f, 0.97f)});
            dcTarget_->FillRectangle(fullRect, solidBrush_.Get());
        }
    } else if (app.backdrop == core::BackdropMode::Mica ||
               app.backdrop == core::BackdropMode::MicaAlt) {
        const bool isAlt = (app.backdrop == core::BackdropMode::MicaAlt);
        const float wpAlpha =
            std::clamp(isAlt ? (0.28f + 0.70f * bgOpacity) : (0.24f + 0.72f * bgOpacity), 0.24f,
                       0.98f);
        const float veilAlpha = isAlt ? (0.24f + 0.56f * bgOpacity) : (0.14f + 0.48f * bgOpacity);

        if (ID2D1Bitmap* micaBmp = GetOrLoadDesktopMicaBitmap()) {
            const D2D1_SIZE_F bSz = micaBmp->GetSize();
            D2D1_RECT_F srcRect{0.0f, 0.0f, bSz.width, bSz.height};
            const long monW = state.monitorScreenRect.right - state.monitorScreenRect.left;
            const long monH = state.monitorScreenRect.bottom - state.monitorScreenRect.top;
            const long winW = state.windowScreenRect.right - state.windowScreenRect.left;
            const long winH = state.windowScreenRect.bottom - state.windowScreenRect.top;
            if (monW > 0 && monH > 0 && winW > 0 && winH > 0 && bSz.width > 2.0f &&
                bSz.height > 2.0f) {
                // Compute cover-fit crop within the blurred wallpaper matching window's monitor rect.
                const float scaleX = bSz.width / static_cast<float>(monW);
                const float scaleY = bSz.height / static_cast<float>(monH);
                const float coverScale = std::max(scaleX, scaleY);
                const float visW = bSz.width / coverScale;
                const float visH = bSz.height / coverScale;
                const float offX = (static_cast<float>(monW) - visW) * 0.5f;
                const float offY = (static_cast<float>(monH) - visH) * 0.5f;

                const float sx0 =
                    (static_cast<float>(state.windowScreenRect.left - state.monitorScreenRect.left) -
                     offX) *
                    coverScale;
                const float sy0 =
                    (static_cast<float>(state.windowScreenRect.top - state.monitorScreenRect.top) -
                     offY) *
                    coverScale;
                const float sx1 = sx0 + static_cast<float>(winW) * coverScale;
                const float sy1 = sy0 + static_cast<float>(winH) * coverScale;

                const float cx0 = std::clamp(sx0, 0.0f, bSz.width - 1.0f);
                const float cy0 = std::clamp(sy0, 0.0f, bSz.height - 1.0f);
                const float cx1 = std::clamp(sx1, cx0 + 1.0f, bSz.width);
                const float cy1 = std::clamp(sy1, cy0 + 1.0f, bSz.height);
                srcRect = D2D1_RECT_F{cx0, cy0, cx1, cy1};
            }
            dcTarget_->DrawBitmap(micaBmp, fullRect, wpAlpha,
                                  D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &srcRect);
            solidBrush_->SetColor(
                D2D1_COLOR_F{pal.windowTint.r, pal.windowTint.g, pal.windowTint.b, veilAlpha});
            dcTarget_->FillRectangle(fullRect, solidBrush_.Get());
        } else {
            const COLORREF desk = GetSysColor(COLOR_DESKTOP);
            const float dR = static_cast<float>(GetRValue(desk)) / 255.0f;
            const float dG = static_cast<float>(GetGValue(desk)) / 255.0f;
            const float dB = static_cast<float>(GetBValue(desk)) / 255.0f;
            const float totalAlpha = std::clamp(wpAlpha + veilAlpha * (1.0f - wpAlpha), 0.30f, 0.99f);
            const float mix = isAlt ? 0.65f : 0.45f;
            D2D1_GRADIENT_STOP stops[2] = {
                {0.0f, D2D1_COLOR_F{dR * (1.0f - mix) + pal.windowTint.r * mix,
                                    dG * (1.0f - mix) + pal.windowTint.g * mix,
                                    dB * (1.0f - mix) + pal.windowTint.b * mix, totalAlpha}},
                {1.0f, D2D1_COLOR_F{pal.windowTint.r, pal.windowTint.g, pal.windowTint.b, totalAlpha}},
            };
            platform::ComPtr<ID2D1GradientStopCollection> stopCollection;
            if (SUCCEEDED(dcTarget_->CreateGradientStopCollection(
                    stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                    stopCollection.ReleaseAndGetAddressOf()))) {
                platform::ComPtr<ID2D1LinearGradientBrush> micaGrad;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lbp{
                    D2D1_POINT_2F{0.0f, 0.0f}, D2D1_POINT_2F{widthDip, heightDip}};
                if (SUCCEEDED(dcTarget_->CreateLinearGradientBrush(
                        lbp, stopCollection.Get(), micaGrad.ReleaseAndGetAddressOf()))) {
                    dcTarget_->FillRectangle(fullRect, micaGrad.Get());
                } else {
                    solidBrush_->SetColor(D2D1_COLOR_F{pal.windowTint.r, pal.windowTint.g,
                                                       pal.windowTint.b, totalAlpha});
                    dcTarget_->FillRectangle(fullRect, solidBrush_.Get());
                }
            } else {
                solidBrush_->SetColor(D2D1_COLOR_F{pal.windowTint.r, pal.windowTint.g,
                                                   pal.windowTint.b, totalAlpha});
                dcTarget_->FillRectangle(fullRect, solidBrush_.Get());
            }
        }
    } else {
        // Acrylic: true per-pixel alpha translucency over the live desktop/windows + subtle sheen
        solidBrush_->SetColor(ToD2D(pal.windowTint));
        dcTarget_->FillRectangle(fullRect, solidBrush_.Get());

        D2D1_GRADIENT_STOP stops[2] = {
            {0.0f, D2D1_COLOR_F{1.0f, 1.0f, 1.0f, pal.isDark ? 0.09f : 0.28f}},
            {1.0f, D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 0.0f}},
        };
        platform::ComPtr<ID2D1GradientStopCollection> stopCollection;
        if (SUCCEEDED(dcTarget_->CreateGradientStopCollection(
                stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                stopCollection.ReleaseAndGetAddressOf()))) {
            platform::ComPtr<ID2D1LinearGradientBrush> sheenBrush;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lbp{
                D2D1_POINT_2F{0.0f, 0.0f}, D2D1_POINT_2F{widthDip * 0.7f, heightDip * 0.7f}};
            if (SUCCEEDED(dcTarget_->CreateLinearGradientBrush(
                    lbp, stopCollection.Get(), sheenBrush.ReleaseAndGetAddressOf()))) {
                dcTarget_->FillRectangle(fullRect, sheenBrush.Get());
            }
        }
    }

    // 2. Header band: Brand + Custom Search Box + Utility Buttons
    solidBrush_->SetColor(ToD2D(pal.textPrimary));
    dcTarget_->DrawTextW(L"GeekDeskCpp", 11, brandFmt_.Get(), ToRect(outLayout.brandRect),
                         solidBrush_.Get());

    solidBrush_->SetColor(ToD2D(pal.searchBoxBg));
    dcTarget_->FillRoundedRectangle(ToRounded(outLayout.searchBoxRect, 8.0f, 8.0f),
                                    solidBrush_.Get());
    solidBrush_->SetColor(
        ToD2D(state.searchFocused ? pal.searchBoxFocusBorder : pal.searchBoxBorder));
    dcTarget_->DrawRoundedRectangle(ToRounded(outLayout.searchBoxRect, 8.0f, 8.0f),
                                    solidBrush_.Get(), state.searchFocused ? 1.5f : 1.0f);

    solidBrush_->SetColor(ToD2D(pal.textSecondary));
    DrawMagnifierIcon(dcTarget_.Get(), solidBrush_.Get(), outLayout.searchBoxRect.left + 15.0f,
                      (outLayout.searchBoxRect.top + outLayout.searchBoxRect.bottom) * 0.5f);

    // Search text + IME composition + caret position computation
    const RectDip searchTextRect{outLayout.searchBoxRect.left + 28.0f, outLayout.searchBoxRect.top,
                                 outLayout.searchBoxRect.right - 10.0f,
                                 outLayout.searchBoxRect.bottom};
    const std::size_t clampedCaret =
        std::min(state.searchCaretIndex, state.searchQueryWide.size());
    std::wstring composedLine = state.searchQueryWide;
    if (!state.imeCompWide.empty()) {
        composedLine.insert(clampedCaret, state.imeCompWide);
    }

    if (composedLine.empty()) {
        constexpr std::wstring_view kPlaceholder =
            L"\u641C\u7D22\u5E94\u7528 / \u62FC\u97F3\u9996\u5B57\u6BCD (Ctrl+F)";
        solidBrush_->SetColor(ToD2D(pal.textSecondary));
        dcTarget_->DrawTextW(kPlaceholder.data(), static_cast<UINT32>(kPlaceholder.size()),
                             searchFmt_.Get(), ToRect(searchTextRect), solidBrush_.Get());
        outLayout.searchCaretX = searchTextRect.left;
    } else {
        platform::ComPtr<IDWriteTextLayout> txtLayout;
        if (SUCCEEDED(dwriteFactory_->CreateTextLayout(
                composedLine.data(), static_cast<UINT32>(composedLine.size()), searchFmt_.Get(),
                std::max(10.0f, searchTextRect.Width()), searchTextRect.Height(),
                txtLayout.ReleaseAndGetAddressOf()))) {
            solidBrush_->SetColor(ToD2D(pal.textPrimary));
            dcTarget_->DrawTextLayout(D2D1_POINT_2F{searchTextRect.left, searchTextRect.top},
                                      txtLayout.Get(), solidBrush_.Get());

            const UINT32 visualCaretPos =
                static_cast<UINT32>(clampedCaret + state.imeCompWide.size());
            float hx = 0.0f;
            float hy = 0.0f;
            DWRITE_HIT_TEST_METRICS htm{};
            if (SUCCEEDED(txtLayout->HitTestTextPosition(visualCaretPos, FALSE, &hx, &hy, &htm))) {
                outLayout.searchCaretX =
                    std::min(searchTextRect.right - 2.0f, searchTextRect.left + hx);
            }
            if (!state.imeCompWide.empty()) {
                // Draw subtle accent underline beneath active IME composition
                float c0x = 0.0f, c0y = 0.0f, c1x = 0.0f, c1y = 0.0f;
                DWRITE_HIT_TEST_METRICS m0{}, m1{};
                txtLayout->HitTestTextPosition(static_cast<UINT32>(clampedCaret), FALSE, &c0x, &c0y,
                                               &m0);
                txtLayout->HitTestTextPosition(visualCaretPos, FALSE, &c1x, &c1y, &m1);
                solidBrush_->SetColor(ToD2D(pal.accent));
                dcTarget_->DrawLine(
                    D2D1_POINT_2F{searchTextRect.left + c0x, searchTextRect.bottom - 5.0f},
                    D2D1_POINT_2F{searchTextRect.left + c1x, searchTextRect.bottom - 5.0f},
                    solidBrush_.Get(), 1.5f);
            }
        }
    }

    if (state.searchFocused && state.caretVisible) {
        solidBrush_->SetColor(ToD2D(pal.textPrimary));
        dcTarget_->DrawLine(
            D2D1_POINT_2F{outLayout.searchCaretX, outLayout.searchCaretY},
            D2D1_POINT_2F{outLayout.searchCaretX,
                          outLayout.searchCaretY + outLayout.searchCaretHeight},
            solidBrush_.Get(), 1.3f);
    }

    // Header utility buttons: Pin, Refresh, Settings, Hide
    for (int i = 0; i < 4; ++i) {
        const auto btn = static_cast<HeaderButton>(i);
        const RectDip& r = outLayout.headerButtonRects[static_cast<std::size_t>(i)];
        const bool active = (btn == HeaderButton::Pin && state.alwaysTopmost);
        const bool hovered = (btn == state.hoveredHeaderButton);
        solidBrush_->SetColor(
            ToD2D(active ? pal.tileSelectedBg
                         : (hovered ? pal.headerButtonHoverBg : pal.headerButtonBg)));
        dcTarget_->FillRoundedRectangle(ToRounded(r, 6.0f, 6.0f), solidBrush_.Get());

        solidBrush_->SetColor(ToD2D(active ? pal.accent : pal.textPrimary));
        const float cx = (r.left + r.right) * 0.5f;
        const float cy = (r.top + r.bottom) * 0.5f;
        if (btn == HeaderButton::Pin) {
            DrawPinIcon(dcTarget_.Get(), solidBrush_.Get(), cx, cy, state.alwaysTopmost);
        } else if (btn == HeaderButton::Refresh) {
            DrawRefreshIcon(dcTarget_.Get(), solidBrush_.Get(), cx, cy);
        } else if (btn == HeaderButton::Settings) {
            DrawSettingsGearIcon(dcTarget_.Get(), solidBrush_.Get(), cx, cy);
        } else {
            DrawCloseIcon(dcTarget_.Get(), solidBrush_.Get(), cx, cy);
        }
    }

    // 3. Left Sidebar (Groups)
    solidBrush_->SetColor(ToD2D(pal.sidebarBg));
    dcTarget_->FillRoundedRectangle(ToRounded(outLayout.sidebarRect, 8.0f, 8.0f),
                                    solidBrush_.Get());

    for (std::size_t i = 0; i < state.groups->size(); ++i) {
        const RectDip& r = outLayout.groupRects[i];
        if (r.Height() <= 0.0f) {
            continue;
        }
        const bool isActive = (!state.isSearchActive && i == state.activeGroupIndex);
        const bool isHovered = (static_cast<int>(i) == state.hoveredGroupIdx);
        const bool isDropHit = (static_cast<int>(i) == state.dropHighlightGroupIdx ||
                                static_cast<int>(i) == state.dragTargetGroupIdx);
        if (isActive || isHovered || isDropHit) {
            solidBrush_->SetColor(
                ToD2D((isActive || isDropHit) ? pal.tileSelectedBg : pal.tileHoverBg));
            dcTarget_->FillRoundedRectangle(ToRounded(r, 6.0f, 6.0f), solidBrush_.Get());
        }
        if (isDropHit) {
            solidBrush_->SetColor(ToD2D(pal.accent));
            dcTarget_->DrawRoundedRectangle(ToRounded(r, 6.0f, 6.0f), solidBrush_.Get(), 1.5f);
        }
        if (isActive) {
            solidBrush_->SetColor(ToD2D(pal.accent));
            dcTarget_->FillRoundedRectangle(
                D2D1_ROUNDED_RECT{D2D1_RECT_F{r.left + 2.0f, r.top + 7.0f, r.left + 5.0f,
                                              r.bottom - 7.0f},
                                  1.5f, 1.5f},
                solidBrush_.Get());
        }

        const auto& grp = (*state.groups)[i];
        const float cy = (r.top + r.bottom) * 0.5f;
        solidBrush_->SetColor(ToD2D(isActive ? pal.accent : pal.textSecondary));
        DrawFolderGlyph(dcTarget_.Get(), solidBrush_.Get(), r.left + 12.0f, cy);
        if (grp.kind == core::GroupKind::LinkedFolder) {
            solidBrush_->SetColor(ToD2D(pal.accent));
            dcTarget_->FillEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{r.left + 24.5f, cy + 4.5f}, 2.2f, 2.2f},
                                   solidBrush_.Get());
        }

        const RectDip nameRect{r.left + 32.0f, r.top, r.right - 34.0f, r.bottom};
        const std::wstring wName = core::utf8::Utf8ToWide(grp.name);
        solidBrush_->SetColor(ToD2D(pal.textPrimary));
        dcTarget_->DrawTextW(wName.data(), static_cast<UINT32>(wName.size()), groupFmt_.Get(),
                             ToRect(nameRect), solidBrush_.Get());

        const RectDip countRect{r.right - 34.0f, r.top, r.right - 8.0f, r.bottom};
        const std::wstring wCount = std::to_wstring(grp.items.size());
        solidBrush_->SetColor(ToD2D(pal.textSecondary));
        dcTarget_->DrawTextW(wCount.data(), static_cast<UINT32>(wCount.size()), countFmt_.Get(),
                             ToRect(countRect), solidBrush_.Get());
    }

    // 4. Right Content Card & Item Grid
    solidBrush_->SetColor(ToD2D(pal.contentCardBg));
    dcTarget_->FillRoundedRectangle(ToRounded(outLayout.contentCardRect, 10.0f, 10.0f),
                                    solidBrush_.Get());
    if (state.dropHighlightGrid) {
        solidBrush_->SetColor(ToD2D(pal.accent));
        dcTarget_->DrawRoundedRectangle(ToRounded(outLayout.contentCardRect, 10.0f, 10.0f),
                                        solidBrush_.Get(), 2.0f);
    } else if (pal.contentCardBorder.a > 0.005f) {
        solidBrush_->SetColor(ToD2D(pal.contentCardBorder));
        dcTarget_->DrawRoundedRectangle(ToRounded(outLayout.contentCardRect, 10.0f, 10.0f),
                                        solidBrush_.Get(), 1.0f);
    }

    dcTarget_->PushAxisAlignedClip(ToRect(outLayout.gridViewportRect),
                                   D2D1_ANTIALIAS_MODE_ALIASED);

    if (state.displayedItems->empty()) {
        solidBrush_->SetColor(ToD2D(pal.textSecondary));
        const std::wstring msg =
            state.isSearchActive
                ? L"\u672A\u627E\u5230\u5339\u914D\u7684\u672C\u5730\u5E94\u7528\u6216\u6587\u4EF6"
                : L"\u5F53\u524D\u5206\u7EC4\u6682\u65E0\u542F\u7528\u9879\u76EE";
        dcTarget_->DrawTextW(msg.data(), static_cast<UINT32>(msg.size()), emptyFmt_.Get(),
                             ToRect(outLayout.gridViewportRect), solidBrush_.Get());
    } else {
        const float iconDrawSize =
            static_cast<float>(std::clamp(app.iconSize, 28, 96));

        for (std::size_t i = 0; i < state.displayedItems->size(); ++i) {
            const RectDip& tile = outLayout.tileRects[i];
            if (tile.bottom < outLayout.gridViewportRect.top ||
                tile.top > outLayout.gridViewportRect.bottom) {
                continue;
            }

            const bool isSelected = (static_cast<int>(i) == state.selectedTileIdx);
            const bool isHovered = (static_cast<int>(i) == state.hoveredTileIdx);
            const bool isDragSrc = (static_cast<int>(i) == state.draggingTileIdx);
            if (isSelected || isHovered || isDragSrc) {
                const ColorRgba fill = isSelected ? pal.tileSelectedBg : pal.tileHoverBg;
                solidBrush_->SetColor(ToD2D(fill, isDragSrc ? 0.55f : 1.0f));
                dcTarget_->FillRoundedRectangle(ToRounded(tile, 8.0f, 8.0f), solidBrush_.Get());
                if (isSelected) {
                    solidBrush_->SetColor(ToD2D(pal.accent, 0.7f));
                    dcTarget_->DrawRoundedRectangle(ToRounded(tile, 8.0f, 8.0f),
                                                    solidBrush_.Get(), 1.2f);
                }
            }
            if (state.dragTargetTileIdx >= 0 && static_cast<int>(i) == state.dragTargetTileIdx &&
                state.draggingTileIdx >= 0 && state.dragTargetTileIdx != state.draggingTileIdx) {
                const float barX =
                    (state.dragTargetTileIdx > state.draggingTileIdx) ? (tile.right + 3.0f)
                                                                      : (tile.left - 3.0f);
                solidBrush_->SetColor(ToD2D(pal.accent));
                dcTarget_->FillRoundedRectangle(
                    D2D1_ROUNDED_RECT{D2D1_RECT_F{barX - 1.5f, tile.top + 6.0f, barX + 1.5f,
                                                  tile.bottom - 6.0f},
                                      1.5f, 1.5f},
                    solidBrush_.Get());
            }

            const auto& view = (*state.displayedItems)[i];
            if (view.item == nullptr) {
                continue;
            }

            const float iconLeft = tile.left + (tile.Width() - iconDrawSize) * 0.5f;
            const float iconTop = tile.top + 8.0f;
            const D2D1_RECT_F iconRect{iconLeft, iconTop, iconLeft + iconDrawSize,
                                       iconTop + iconDrawSize};

            ID2D1Bitmap* bmp = (state.iconCache != nullptr)
                                   ? GetOrUploadIconBitmap(*view.item, *state.iconCache)
                                   : nullptr;
            if (bmp != nullptr) {
                dcTarget_->DrawBitmap(bmp, iconRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                solidBrush_->SetColor(ToD2D(pal.tileHoverBg));
                dcTarget_->FillRoundedRectangle(
                    D2D1_ROUNDED_RECT{iconRect, 10.0f, 10.0f}, solidBrush_.Get());
                const std::wstring badge = BuildInitialBadge(view.item->name);
                solidBrush_->SetColor(ToD2D(pal.accent));
                dcTarget_->DrawTextW(badge.data(), static_cast<UINT32>(badge.size()),
                                     badgeFmt_.Get(), iconRect, solidBrush_.Get());
            }

            if (view.item->runAsAdmin) {
                const D2D1_RECT_F shield{iconRect.right - 12.0f, iconRect.bottom - 12.0f,
                                         iconRect.right, iconRect.bottom};
                solidBrush_->SetColor(ToD2D(pal.badgeBg));
                dcTarget_->FillRoundedRectangle(D2D1_ROUNDED_RECT{shield, 3.0f, 3.0f},
                                                solidBrush_.Get());
                solidBrush_->SetColor(ToD2D(pal.badgeText));
                dcTarget_->DrawLine(D2D1_POINT_2F{shield.left + 6.0f, shield.top + 3.0f},
                                    D2D1_POINT_2F{shield.left + 6.0f, shield.bottom - 3.0f},
                                    solidBrush_.Get(), 1.5f);
                dcTarget_->DrawLine(D2D1_POINT_2F{shield.left + 3.0f, shield.top + 6.0f},
                                    D2D1_POINT_2F{shield.right - 3.0f, shield.top + 6.0f},
                                    solidBrush_.Get(), 1.5f);
            }

            if (app.showItemTitle) {
                const D2D1_RECT_F labelRect{tile.left + 3.0f, iconRect.bottom + 5.0f,
                                            tile.right - 3.0f, tile.bottom - 3.0f};
                const std::wstring wTitle = core::utf8::Utf8ToWide(view.item->name);
                solidBrush_->SetColor(ToD2D(pal.textPrimary));
                dcTarget_->DrawTextW(wTitle.data(), static_cast<UINT32>(wTitle.size()),
                                     itemTitleFmt_.Get(), labelRect, solidBrush_.Get());
            }
        }
    }

    dcTarget_->PopAxisAlignedClip();

    if (outLayout.maxScrollDip > 1.0f) {
        solidBrush_->SetColor(ToD2D(pal.textSecondary, 0.35f));
        dcTarget_->FillRoundedRectangle(ToRounded(outLayout.scrollbarThumbRect, 2.0f, 2.0f),
                                        solidBrush_.Get());
    }

    // 5. Status bar
    const std::wstring wStatus = core::utf8::Utf8ToWide(state.statusText);
    solidBrush_->SetColor(ToD2D(pal.textSecondary));
    dcTarget_->DrawTextW(wStatus.data(), static_cast<UINT32>(wStatus.size()), statusFmt_.Get(),
                         ToRect(outLayout.statusBarRect), solidBrush_.Get());

    if (pushedLayer) {
        dcTarget_->PopLayer();
    }

    // 6. Crisp outer window border (1px) matching the corner radius
    solidBrush_->SetColor(ToD2D(pal.windowBorder));
    if (cornerR > 0.5f) {
        dcTarget_->DrawRoundedRectangle(windowRound, solidBrush_.Get(), 1.0f);
    } else {
        dcTarget_->DrawRectangle(windowRound.rect, solidBrush_.Get(), 1.0f);
    }

    const HRESULT endHr = dcTarget_->EndDraw();
    if (endHr == D2DERR_RECREATE_TARGET) {
        DiscardTargetAndBitmaps();
        return false;
    }
    if (FAILED(endHr)) {
        return false;
    }

    POINT ptSrc{0, 0};
    SIZE sizeWnd{widthPx, heightPx};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL ok = UpdateLayeredWindow(hwnd, screenDc.hdc, nullptr, &sizeWnd, memDc_, &ptSrc, 0,
                                        &blend, ULW_ALPHA);
    return ok != FALSE;
}

} // namespace geekdesk::ui
