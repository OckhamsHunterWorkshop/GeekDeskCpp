#include "ui/icon_cache.hpp"

#include "core/utf8.hpp"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace geekdesk::ui {

namespace {

std::uint64_t Fnv1a64(std::string_view data) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool HasAnyNonZeroAlpha(const std::vector<std::uint8_t>& pixels) noexcept {
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            return true;
        }
    }
    return false;
}

bool HasAnyNonZeroRgb(const std::vector<std::uint8_t>& pixels) noexcept {
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (pixels[i] != 0 || pixels[i + 1] != 0 || pixels[i + 2] != 0) {
            return true;
        }
    }
    return false;
}

bool ConvertWicSourceToPbgra(IWICImagingFactory* factory,
                             IWICBitmapSource* source,
                             int maxDecodeWidth,
                             BgraImage& outImage) {
    if (factory == nullptr || source == nullptr) {
        return false;
    }

    UINT srcW = 0;
    UINT srcH = 0;
    if (FAILED(source->GetSize(&srcW, &srcH)) || srcW == 0 || srcH == 0) {
        return false;
    }

    platform::ComPtr<IWICBitmapSource> current(source);
    current->AddRef();

    if (maxDecodeWidth > 0 && static_cast<int>(srcW) > maxDecodeWidth) {
        const UINT targetW = static_cast<UINT>(maxDecodeWidth);
        const UINT targetH = std::max<UINT>(
            1u, static_cast<UINT>((static_cast<std::uint64_t>(srcH) * targetW) / srcW));
        platform::ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(factory->CreateBitmapScaler(scaler.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(scaler->Initialize(current.Get(), targetW, targetH,
                                         WICBitmapInterpolationModeFant))) {
            platform::ComPtr<IWICBitmapSource> scaledSrc;
            if (SUCCEEDED(scaler->QueryInterface(IID_PPV_ARGS(scaledSrc.ReleaseAndGetAddressOf())))) {
                current = scaledSrc;
                srcW = targetW;
                srcH = targetH;
            }
        }
    }

    platform::ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.ReleaseAndGetAddressOf()))) {
        return false;
    }
    if (FAILED(converter->Initialize(current.Get(), GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return false;
    }

    const UINT stride = srcW * 4u;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(stride) * srcH);
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(buf.size()), buf.data()))) {
        return false;
    }

    outImage.width = static_cast<int>(srcW);
    outImage.height = static_cast<int>(srcH);
    outImage.pixels = std::move(buf);
    return true;
}

bool LoadWithWicFactory(IWICImagingFactory* factory,
                        const std::filesystem::path& filePath,
                        int maxDecodeWidth,
                        BgraImage& outImage) {
    if (factory == nullptr || filePath.empty()) {
        return false;
    }
    platform::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(
            filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
            decoder.ReleaseAndGetAddressOf()))) {
        return false;
    }
    platform::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.ReleaseAndGetAddressOf()))) {
        return false;
    }
    return ConvertWicSourceToPbgra(factory, frame.Get(), maxDecodeWidth, outImage);
}

bool SavePngAtomicWithWic(IWICImagingFactory* factory,
                          const BgraImage& img,
                          const std::filesystem::path& destPath) {
    if (factory == nullptr || !img.IsValid() || destPath.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(destPath.parent_path(), ec);
    const std::filesystem::path tmpPath = destPath.wstring() + L".tmp";

    bool saved = false;
    {
        platform::ComPtr<IWICStream> stream;
        platform::ComPtr<IWICBitmapEncoder> encoder;
        platform::ComPtr<IWICBitmapFrameEncode> frame;
        if (SUCCEEDED(factory->CreateStream(stream.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(stream->InitializeFromFilename(tmpPath.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                             encoder.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(frame.ReleaseAndGetAddressOf(), nullptr)) &&
            SUCCEEDED(frame->Initialize(nullptr))) {
            UINT w = static_cast<UINT>(img.width);
            UINT h = static_cast<UINT>(img.height);
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
            if (SUCCEEDED(frame->SetSize(w, h)) && SUCCEEDED(frame->SetPixelFormat(&fmt))) {
                std::vector<std::uint8_t> mutablePixels = img.pixels;
                const UINT stride = w * 4u;
                if (SUCCEEDED(frame->WritePixels(h, stride, static_cast<UINT>(mutablePixels.size()),
                                                 mutablePixels.data())) &&
                    SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
                    saved = true;
                }
            }
        }
    }

    if (!saved) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

    if (!MoveFileExW(tmpPath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    return true;
}

bool ExtractViaShellImageFactory(IWICImagingFactory* factory,
                                 const std::wstring& parsingPath,
                                 BgraImage& outImage) {
    if (factory == nullptr || parsingPath.empty()) {
        return false;
    }
    platform::ComPtr<IShellItem> shellItem;
    if (FAILED(SHCreateItemFromParsingName(parsingPath.c_str(), nullptr,
                                           IID_PPV_ARGS(shellItem.ReleaseAndGetAddressOf())))) {
        return false;
    }
    platform::ComPtr<IShellItemImageFactory> imageFactory;
    if (FAILED(shellItem->QueryInterface(IID_PPV_ARGS(imageFactory.ReleaseAndGetAddressOf())))) {
        return false;
    }
    HBITMAP hBitmap = nullptr;
    if (FAILED(imageFactory->GetImage(SIZE{96, 96}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK,
                                      &hBitmap)) ||
        hBitmap == nullptr) {
        return false;
    }

    bool ok = false;
    platform::ComPtr<IWICBitmap> wicBitmap;
    if (SUCCEEDED(factory->CreateBitmapFromHBITMAP(hBitmap, nullptr, WICBitmapUseAlpha,
                                                   wicBitmap.ReleaseAndGetAddressOf()))) {
        if (ConvertWicSourceToPbgra(factory, wicBitmap.Get(), 0, outImage)) {
            if (HasAnyNonZeroAlpha(outImage.pixels)) {
                ok = true;
            } else if (HasAnyNonZeroRgb(outImage.pixels)) {
                // Retry ignoring alpha if GDI returned a non-alpha RGB DIB.
                platform::ComPtr<IWICBitmap> wicOpaque;
                if (SUCCEEDED(factory->CreateBitmapFromHBITMAP(
                        hBitmap, nullptr, WICBitmapIgnoreAlpha,
                        wicOpaque.ReleaseAndGetAddressOf()))) {
                    ok = ConvertWicSourceToPbgra(factory, wicOpaque.Get(), 0, outImage);
                }
            }
        }
    }
    DeleteObject(hBitmap);
    return ok;
}

bool ExtractFallbackHIcon(IWICImagingFactory* factory,
                          const std::wstring& pathOrExt,
                          bool useFileAttributes,
                          bool isDirectory,
                          BgraImage& outImage) {
    if (factory == nullptr || pathOrExt.empty()) {
        return false;
    }
    SHFILEINFOW sfi{};
    UINT flags = SHGFI_ICON | SHGFI_LARGEICON;
    DWORD attrs = 0;
    if (useFileAttributes) {
        flags |= SHGFI_USEFILEATTRIBUTES;
        attrs = isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    }
    if (SHGetFileInfoW(pathOrExt.c_str(), attrs, &sfi, sizeof(sfi), flags) == 0 ||
        sfi.hIcon == nullptr) {
        return false;
    }

    bool ok = false;
    platform::ComPtr<IWICBitmap> wicBitmap;
    if (SUCCEEDED(factory->CreateBitmapFromHICON(sfi.hIcon, wicBitmap.ReleaseAndGetAddressOf()))) {
        ok = ConvertWicSourceToPbgra(factory, wicBitmap.Get(), 0, outImage);
    }
    DestroyIcon(sfi.hIcon);
    return ok;
}

bool ExtractLaunchItemIcon(IWICImagingFactory* factory,
                           const std::filesystem::path& dataRoot,
                           const core::LaunchItem& item,
                           const std::optional<std::filesystem::path>& diskCachePath,
                           BgraImage& outImage) {
    // 1. Custom icon file inside data/assets/custom-icons/ or data/assets/
    if (item.customIconFile.has_value() && !item.customIconFile->empty()) {
        const std::filesystem::path rel = core::utf8::PathFromUtf8(*item.customIconFile);
        const std::filesystem::path nameOnly = rel.filename();
        const std::filesystem::path candidates[] = {
            dataRoot / L"assets" / L"custom-icons" / nameOnly,
            dataRoot / L"assets" / rel,
        };
        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) &&
                LoadWithWicFactory(factory, candidate, 128, outImage)) {
                return true;
            }
        }
    }

    // 2. Disk PNG cache hit
    if (diskCachePath.has_value()) {
        std::error_code ec;
        if (std::filesystem::exists(*diskCachePath, ec) &&
            LoadWithWicFactory(factory, *diskCachePath, 0, outImage)) {
            return true;
        }
    }

    // 3. Extract via Shell APIs
    bool extracted = false;
    if (item.kind == core::ItemKind::Url) {
        extracted = ExtractFallbackHIcon(factory, L".html", true, false, outImage);
    } else if (!item.targetPath.empty()) {
        const std::wstring targetW = core::utf8::Utf8ToWide(item.targetPath);
        const std::filesystem::path targetFs(targetW);
        std::error_code ec;
        const bool existsOnDisk = std::filesystem::exists(targetFs, ec) ||
                                  targetW.starts_with(L"::") || targetW.starts_with(L"shell:");

        if (existsOnDisk && ExtractViaShellImageFactory(factory, targetW, outImage)) {
            extracted = true;
        }
        if (!extracted && item.lnkPath.has_value() && !item.lnkPath->empty()) {
            const std::wstring lnkW = core::utf8::Utf8ToWide(*item.lnkPath);
            if (std::filesystem::exists(std::filesystem::path(lnkW), ec)) {
                extracted = ExtractViaShellImageFactory(factory, lnkW, outImage) ||
                            ExtractFallbackHIcon(factory, lnkW, false, false, outImage);
            }
        }
        if (!extracted && existsOnDisk) {
            extracted = ExtractFallbackHIcon(factory, targetW, false,
                                             item.kind == core::ItemKind::Folder, outImage);
        }
        if (!extracted) {
            // Virtual fallback icon by extension / folder attribute
            const std::wstring fallbackSpec =
                (item.kind == core::ItemKind::Folder)
                    ? L"folder"
                    : (targetFs.has_extension() ? targetFs.extension().wstring() : L".exe");
            extracted = ExtractFallbackHIcon(factory, fallbackSpec, true,
                                             item.kind == core::ItemKind::Folder, outImage);
        }
    }

    if (!extracted || !outImage.IsValid()) {
        return false;
    }

    outImage = TrimIconPadding(outImage);
    if (diskCachePath.has_value()) {
        (void)SavePngAtomicWithWic(factory, outImage, *diskCachePath);
    }
    return true;
}

} // namespace

IconCache::~IconCache() {
    Shutdown();
}

void IconCache::Initialize(std::filesystem::path dataRoot, HWND notifyHwnd, UINT notifyMsg) {
    Shutdown();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dataRoot_ = std::move(dataRoot);
        notifyHwnd_ = notifyHwnd;
        notifyMsg_ = notifyMsg;
        stopRequested_ = false;
        initialized_ = true;
    }
    worker_ = std::thread(&IconCache::WorkerLoop, this);
}

void IconCache::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return;
        }
        stopRequested_ = true;
        queue_.clear();
        inFlight_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
}

void IconCache::SetDataRoot(std::filesystem::path dataRoot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dataRoot_ != dataRoot) {
        dataRoot_ = std::move(dataRoot);
        memoryCache_.clear();
        lruList_.clear();
        queue_.clear();
        inFlight_.clear();
    }
}

void IconCache::ClearMemoryCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    memoryCache_.clear();
    lruList_.clear();
}

void IconCache::WaitUntilIdle(int timeoutMs) const {
    std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    idleCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                     [this]() { return !initialized_ || stopRequested_ || (queue_.empty() && !workerBusy_); });
}

std::string IconCache::BuildMemoryKey(const core::LaunchItem& item) {
    if (item.customIconFile.has_value() && !item.customIconFile->empty()) {
        const std::string file =
            core::utf8::PathToUtf8(core::utf8::PathFromUtf8(*item.customIconFile).filename());
        return "custom:" + core::utf8::ToLowerAscii(file);
    }
    if (item.kind == core::ItemKind::Url) {
        return "url:default";
    }
    if (item.targetPath.empty()) {
        return {};
    }
    return "path:" + core::utf8::ToLowerAscii(core::utf8::TrimAscii(item.targetPath));
}

std::optional<std::filesystem::path> IconCache::ResolveDiskCachePath(
    const std::filesystem::path& dataRoot,
    const core::LaunchItem& item) {
    if (dataRoot.empty()) {
        return std::nullopt;
    }
    if (item.customIconFile.has_value() && !item.customIconFile->empty()) {
        return std::nullopt;
    }

    std::string identity;
    if (item.kind == core::ItemKind::Url) {
        identity = "v1_cpp|url:default";
    } else if (item.targetPath.empty()) {
        return std::nullopt;
    } else {
        std::uint64_t stamp = 0;
        const std::filesystem::path p = core::utf8::PathFromUtf8(item.targetPath);
        std::error_code ec;
        if (item.kind == core::ItemKind::File && std::filesystem::exists(p, ec)) {
            const auto ftime = std::filesystem::last_write_time(p, ec);
            if (!ec) {
                stamp = static_cast<std::uint64_t>(ftime.time_since_epoch().count());
            }
        }
        identity = "v1_cpp|" + std::string(core::ToString(item.kind)) + "|" +
                   core::utf8::ToLowerAscii(core::utf8::TrimAscii(item.targetPath)) + "|" +
                   std::to_string(stamp);
    }

    const std::uint64_t hash = Fnv1a64(identity);
    char nameBuf[32]{};
    std::snprintf(nameBuf, sizeof(nameBuf), "cpp_%016llx.png",
                  static_cast<unsigned long long>(hash));
    return dataRoot / L"cache" / L"icons" / core::utf8::PathFromUtf8(nameBuf);
}

bool IconCache::TryGetOrRequest(const core::LaunchItem& item, BgraImage& outImage) {
    const std::string key = BuildMemoryKey(item);
    if (key.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = memoryCache_.find(key); it != memoryCache_.end()) {
        lruList_.splice(lruList_.begin(), lruList_, it->second.lruIt);
        outImage = it->second.image;
        return outImage.IsValid();
    }

    if (!initialized_ || stopRequested_ || inFlight_.contains(key)) {
        return false;
    }

    if (queue_.size() >= kMaxQueueCapacity) {
        inFlight_.erase(queue_.front().memoryKey);
        queue_.pop_front();
    }
    inFlight_.insert(key);
    queue_.push_back(IconRequest{key, item, dataRoot_});
    cv_.notify_one();
    return false;
}

bool IconCache::LoadImageFileToPbgra(const std::filesystem::path& filePath,
                                     int maxDecodeWidth,
                                     BgraImage& outImage) {
    platform::ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())))) {
        return false;
    }
    return LoadWithWicFactory(factory.Get(), filePath, maxDecodeWidth, outImage);
}

void IconCache::PutInMemoryCache(const std::string& key, BgraImage image) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = memoryCache_.find(key); it != memoryCache_.end()) {
        it->second.image = std::move(image);
        lruList_.splice(lruList_.begin(), lruList_, it->second.lruIt);
        return;
    }
    while (memoryCache_.size() >= kMaxMemoryEntries && !lruList_.empty()) {
        const std::string& evictKey = lruList_.back();
        memoryCache_.erase(evictKey);
        lruList_.pop_back();
    }
    lruList_.push_front(key);
    memoryCache_.emplace(key, CacheEntry{std::move(image), lruList_.begin()});
}

void IconCache::WorkerLoop() {
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    platform::ComPtr<IWICImagingFactory> factory;
    if (SUCCEEDED(coHr) || coHr == RPC_E_CHANGED_MODE) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
    }

    while (true) {
        IconRequest req;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopRequested_ || !queue_.empty(); });
            if (stopRequested_) {
                break;
            }
            workerBusy_ = true;
            // LIFO ordering ensures newly selected groups render first.
            req = std::move(queue_.back());
            queue_.pop_back();
        }

        const auto diskPath = ResolveDiskCachePath(req.dataRoot, req.item);
        BgraImage img;
        const bool ok = ExtractLaunchItemIcon(factory.Get(), req.dataRoot, req.item, diskPath, img);
        if (ok && img.IsValid()) {
            PutInMemoryCache(req.memoryKey, std::move(img));
        } else {
            // Negative entry avoids infinite re-extraction loops for broken paths during this run.
            PutInMemoryCache(req.memoryKey, BgraImage{});
        }

        HWND notifyHwnd = nullptr;
        UINT notifyMsg = 0;
        bool isNowIdle = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            inFlight_.erase(req.memoryKey);
            notifyHwnd = notifyHwnd_;
            notifyMsg = notifyMsg_;
            workerBusy_ = false;
            isNowIdle = queue_.empty();
        }
        if (isNowIdle) {
            idleCv_.notify_all();
        }

        if (ok && notifyHwnd != nullptr && notifyMsg != 0) {
            PostMessageW(notifyHwnd, notifyMsg, 0, 0);
        }
    }

    factory.Reset();
    if (SUCCEEDED(coHr)) {
        CoUninitialize();
    }
}

} // namespace geekdesk::ui
