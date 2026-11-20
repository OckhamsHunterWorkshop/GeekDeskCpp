#pragma once

#include "core/models.hpp"
#include "core/search_engine.hpp"
#include "platform/com_ptr.hpp"
#include "ui/icon_cache.hpp"
#include "ui/theme_palette.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace geekdesk::ui {

struct RectDip {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    [[nodiscard]] constexpr float Width() const noexcept {
        return right - left;
    }
    [[nodiscard]] constexpr float Height() const noexcept {
        return bottom - top;
    }
    [[nodiscard]] constexpr bool Contains(float x, float y) const noexcept {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

enum class HeaderButton : int {
    None = -1,
    Pin = 0,
    Refresh = 1,
    Settings = 2,
    Hide = 3,
};

struct DisplayedItemView {
    const core::LaunchItem* item = nullptr;
    std::string groupName;
};

struct LayoutSnapshot {
    float widthDip = 0.0f;
    float heightDip = 0.0f;
    RectDip headerRect;
    RectDip brandRect;
    RectDip searchBoxRect;
    std::array<RectDip, 4> headerButtonRects{};
    RectDip sidebarRect;
    std::vector<RectDip> groupRects;
    RectDip contentCardRect;
    RectDip gridViewportRect;
    RectDip scrollbarTrackRect;
    RectDip scrollbarThumbRect;
    std::vector<RectDip> tileRects;
    RectDip statusBarRect;
    int gridColumns = 1;
    float rowStrideDip = 96.0f;
    float totalContentHeightDip = 0.0f;
    float maxScrollDip = 0.0f;
    float clampedScrollDip = 0.0f;
    float searchCaretX = 0.0f;
    float searchCaretY = 0.0f;
    float searchCaretHeight = 16.0f;
};

struct RenderState {
    const core::AppSettings* settings = nullptr;
    const ThemePalette* palette = nullptr;
    const std::vector<core::CategoryGroup>* groups = nullptr;
    std::size_t activeGroupIndex = 0;
    const std::vector<DisplayedItemView>* displayedItems = nullptr;
    bool isSearchActive = false;
    std::wstring searchQueryWide;
    std::wstring imeCompWide;
    std::size_t searchCaretIndex = 0;
    bool searchFocused = false;
    bool caretVisible = false;
    float scrollOffsetDip = 0.0f;
    int hoveredGroupIdx = -1;
    int hoveredTileIdx = -1;
    int selectedTileIdx = -1;
    int dropHighlightGroupIdx = -1;
    bool dropHighlightGrid = false;
    int draggingTileIdx = -1;
    int dragTargetTileIdx = -1;
    int draggingGroupIdx = -1;
    int dragTargetGroupIdx = -1;
    HeaderButton hoveredHeaderButton = HeaderButton::None;
    bool alwaysTopmost = false;
    std::string statusText;
    std::filesystem::path dataRoot;
    IconCache* iconCache = nullptr;
    RECT windowScreenRect{};
    RECT monitorScreenRect{};
};

/// Mode A Renderer: Win32 DIB Double-Buffer + ID2D1DCRenderTarget (D2D1_RENDER_TARGET_TYPE_SOFTWARE)
/// + DirectWrite grayscale text + UpdateLayeredWindow (ULW_ALPHA). Zero D3D11/DXGI initialization.
class D2DRenderer {
public:
    D2DRenderer() = default;
    ~D2DRenderer();

    D2DRenderer(const D2DRenderer&) = delete;
    D2DRenderer& operator=(const D2DRenderer&) = delete;

    bool Initialize();
    void Shutdown();

    /// Compute pure DIP layout geometry for hit-testing and scrolling.
    [[nodiscard]] static LayoutSnapshot ComputeLayout(const core::AppSettings& settings,
                                                      float widthDip,
                                                      float heightDip,
                                                      std::size_t groupCount,
                                                      std::size_t itemCount,
                                                      float scrollOffsetDip);

    /// Render current state into an offscreen 32-bit PBGRA DIB and present via UpdateLayeredWindow.
    bool RenderToLayeredWindow(HWND hwnd,
                               int widthPx,
                               int heightPx,
                               std::uint32_t dpi,
                               const RenderState& state,
                               LayoutSnapshot& outLayout);

    /// Free transient DIB backbuffer when the window hides to minimize idle memory.
    void ReleaseBackBuffer();

    /// Invalidate cached wallpaper bitmap when appearance changes.
    void InvalidateWallpaperCache();

    /// Invalidate cached desktop Mica backdrop bitmap when OS wallpaper changes.
    void InvalidateDesktopWallpaperCache();

private:
    bool EnsureFactories();
    bool EnsureRenderTarget(std::uint32_t dpi);
    bool EnsureBackBuffer(HDC referenceDc, int widthPx, int heightPx);
    void DiscardTargetAndBitmaps();
    ID2D1Bitmap* GetOrUploadIconBitmap(const core::LaunchItem& item, IconCache& iconCache);
    ID2D1Bitmap* GetOrLoadWallpaperBitmap(const std::filesystem::path& dataRoot,
                                          const std::string& fileName);
    ID2D1Bitmap* GetOrLoadDesktopMicaBitmap();

    platform::ComPtr<ID2D1Factory> factory_;
    platform::ComPtr<IDWriteFactory> dwriteFactory_;
    platform::ComPtr<ID2D1DCRenderTarget> dcTarget_;
    platform::ComPtr<ID2D1SolidColorBrush> solidBrush_;
    platform::ComPtr<ID2D1Layer> clipLayer_;

    platform::ComPtr<IDWriteTextFormat> brandFmt_;
    platform::ComPtr<IDWriteTextFormat> groupFmt_;
    platform::ComPtr<IDWriteTextFormat> countFmt_;
    platform::ComPtr<IDWriteTextFormat> searchFmt_;
    platform::ComPtr<IDWriteTextFormat> itemTitleFmt_;
    platform::ComPtr<IDWriteTextFormat> badgeFmt_;
    platform::ComPtr<IDWriteTextFormat> statusFmt_;
    platform::ComPtr<IDWriteTextFormat> emptyFmt_;

    HDC memDc_ = nullptr;
    HBITMAP memBitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    int backWidthPx_ = 0;
    int backHeightPx_ = 0;

    static constexpr std::size_t kMaxBitmapEntries = 96;
    struct BitmapEntry {
        platform::ComPtr<ID2D1Bitmap> bitmap;
        std::list<std::string>::iterator lruIt;
    };
    std::list<std::string> bitmapLru_;
    std::unordered_map<std::string, BitmapEntry> iconBitmaps_;

    std::string cachedWallpaperKey_;
    platform::ComPtr<ID2D1Bitmap> wallpaperBitmap_;
    bool wallpaperLoadAttempted_ = false;

    std::string cachedDesktopWallpaperKey_;
    platform::ComPtr<ID2D1Bitmap> desktopMicaBitmap_;
    bool desktopMicaLoadAttempted_ = false;
};

} // namespace geekdesk::ui
