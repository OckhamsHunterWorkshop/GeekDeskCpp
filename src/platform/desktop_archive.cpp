#include "platform/desktop_archive.hpp"

#include "core/utf8.hpp"
#include "platform/com_ptr.hpp"
#include "platform/file_dialogs.hpp"

#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <system_error>

namespace geekdesk::platform {

namespace {

std::filesystem::path QueryKnownFolder(const KNOWNFOLDERID& folderId) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &raw)) || raw == nullptr) {
        return {};
    }
    std::filesystem::path out(raw);
    CoTaskMemFree(raw);
    out.make_preferred();
    return out;
}

bool PathsMatchIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    if (a.empty()) {
        return true;
    }
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

class MoveProgressSink final : public IFileOperationProgressSink {
public:
    MoveProgressSink(const std::vector<std::filesystem::path>& sources,
                     const std::filesystem::path& destDir,
                     std::vector<std::filesystem::path>& outFinalPaths,
                     std::vector<bool>& outMoved) noexcept
        : sources_(sources),
          destDir_(destDir),
          finalPaths_(outFinalPaths),
          moved_(outMoved) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
            *ppv = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        return 1;
    }

    HRESULT STDMETHODCALLTYPE StartOperations() override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT,
                                             IShellItem*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD,
                                           IShellItem* psiItem,
                                           IShellItem*,
                                           LPCWSTR pszNewName,
                                           HRESULT hrMove,
                                           IShellItem* psiNewlyCreated) override {
        if (FAILED(hrMove) || psiItem == nullptr) {
            return S_OK;
        }

        PWSTR srcRaw = nullptr;
        if (FAILED(psiItem->GetDisplayName(SIGDN_FILESYSPATH, &srcRaw)) || srcRaw == nullptr) {
            return S_OK;
        }
        std::filesystem::path srcNorm(srcRaw);
        CoTaskMemFree(srcRaw);
        srcNorm.make_preferred();

        std::filesystem::path dstNorm;
        if (psiNewlyCreated != nullptr) {
            PWSTR dstRaw = nullptr;
            if (SUCCEEDED(psiNewlyCreated->GetDisplayName(SIGDN_FILESYSPATH, &dstRaw)) &&
                dstRaw != nullptr) {
                dstNorm = std::filesystem::path(dstRaw);
                CoTaskMemFree(dstRaw);
                dstNorm.make_preferred();
            }
        }
        if (dstNorm.empty() && pszNewName != nullptr && pszNewName[0] != L'\0') {
            dstNorm = (destDir_ / pszNewName).make_preferred();
        }
        if (dstNorm.empty()) {
            dstNorm = (destDir_ / srcNorm.filename()).make_preferred();
        }

        const std::wstring srcW = srcNorm.wstring();
        for (std::size_t i = 0; i < sources_.size(); ++i) {
            if (!moved_[i] && PathsMatchIgnoreCase(sources_[i].wstring(), srcW)) {
                moved_[i] = true;
                finalPaths_[i] = std::move(dstNorm);
                break;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT,
                                           IShellItem*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT, IShellItem*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT,
                                          IShellItem*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResetTimer() override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PauseTimer() override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override {
        return S_OK;
    }

private:
    const std::vector<std::filesystem::path>& sources_;
    const std::filesystem::path& destDir_;
    std::vector<std::filesystem::path>& finalPaths_;
    std::vector<bool>& moved_;
};

} // namespace

core::DesktopKnownFolders GetSystemDesktopFolders() {
    core::DesktopKnownFolders folders;
    folders.userDesktop = QueryKnownFolder(FOLDERID_Desktop);
    folders.publicDesktop = QueryKnownFolder(FOLDERID_PublicDesktop);
    return folders;
}

ShellMoveBatchResult MovePathsWithShellOperation(
    HWND ownerHwnd,
    const std::vector<std::filesystem::path>& sourcePaths,
    const std::filesystem::path& destDir) {
    ShellMoveBatchResult result;
    result.finalPaths.reserve(sourcePaths.size());
    std::vector<std::filesystem::path> normalizedSources;
    normalizedSources.reserve(sourcePaths.size());

    for (const auto& p : sourcePaths) {
        std::filesystem::path norm = core::utf8::NormalizeWindowsPathInput(p.wstring());
        if (norm.empty()) {
            norm = p;
        }
        normalizedSources.push_back(norm);
        result.finalPaths.push_back(norm);
    }

    if (normalizedSources.empty()) {
        return result;
    }

    const std::filesystem::path normDest =
        core::utf8::NormalizeWindowsPathInput(destDir.wstring());
    if (normDest.empty()) {
        result.fallbackCount = normalizedSources.size();
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(normDest, ec);
    if (!std::filesystem::exists(normDest, ec) || !std::filesystem::is_directory(normDest, ec)) {
        result.fallbackCount = normalizedSources.size();
        return result;
    }

    ComPtr<IFileOperation> fileOp;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(fileOp.ReleaseAndGetAddressOf()))) ||
        !fileOp) {
        result.fallbackCount = normalizedSources.size();
        return result;
    }

    if (ownerHwnd != nullptr) {
        (void)fileOp->SetOwnerWindow(ownerHwnd);
    }
    DWORD opFlags = FOF_ALLOWUNDO | FOF_RENAMEONCOLLISION | FOF_NOCONFIRMMKDIR;
#ifdef FOFX_SHOWELEVATIONPROMPT
    opFlags |= FOFX_SHOWELEVATIONPROMPT;
#endif
    (void)fileOp->SetOperationFlags(opFlags);

    ComPtr<IShellItem> destItem;
    if (FAILED(SHCreateItemFromParsingName(normDest.c_str(), nullptr,
                                           IID_PPV_ARGS(destItem.ReleaseAndGetAddressOf()))) ||
        !destItem) {
        result.fallbackCount = normalizedSources.size();
        return result;
    }

    std::vector<bool> moved(normalizedSources.size(), false);
    MoveProgressSink sink(normalizedSources, normDest, result.finalPaths, moved);
    DWORD cookie = 0;
    const bool advised = SUCCEEDED(fileOp->Advise(&sink, &cookie));

    std::size_t queuedCount = 0;
    for (const auto& src : normalizedSources) {
        if (!std::filesystem::exists(src, ec)) {
            continue;
        }
        ComPtr<IShellItem> srcItem;
        if (FAILED(SHCreateItemFromParsingName(src.c_str(), nullptr,
                                               IID_PPV_ARGS(srcItem.ReleaseAndGetAddressOf()))) ||
            !srcItem) {
            continue;
        }
        if (SUCCEEDED(fileOp->MoveItem(srcItem.Get(), destItem.Get(), nullptr, nullptr))) {
            ++queuedCount;
        }
    }

    if (queuedCount > 0) {
        (void)fileOp->PerformOperations();
    }

    if (advised) {
        (void)fileOp->Unadvise(cookie);
    }

    for (const bool m : moved) {
        if (m) {
            ++result.movedCount;
        } else {
            ++result.fallbackCount;
        }
    }
    return result;
}

RestoreDesktopResult RestoreArchivedItemToDesktop(HWND ownerHwnd,
                                                  const std::filesystem::path& archivedPath) {
    RestoreDesktopResult out;
    const std::filesystem::path cleanArchived =
        core::utf8::NormalizeWindowsPathInput(archivedPath.wstring());
    std::error_code ec;
    if (cleanArchived.empty() || !std::filesystem::exists(cleanArchived, ec)) {
        out.error = "Archived file does not exist on disk";
        return out;
    }

    const core::DesktopKnownFolders desktops = GetSystemDesktopFolders();
    if (desktops.userDesktop.empty()) {
        out.error = "Unable to locate current user Desktop folder";
        return out;
    }

    const ShellMoveBatchResult moveRes =
        MovePathsWithShellOperation(ownerHwnd, {cleanArchived}, desktops.userDesktop);
    if (moveRes.movedCount != 1 || moveRes.finalPaths.empty()) {
        out.error = "Move to Desktop was canceled or failed";
        return out;
    }

    out.ok = true;
    out.restoredPath = moveRes.finalPaths[0];
    return out;
}

std::optional<std::filesystem::path> PickAndValidateDesktopArchiveDir(
    HWND ownerHwnd,
    const std::filesystem::path& appDataRoot,
    bool englishUi) {
    auto chosen = PickFolderDialog(
        ownerHwnd,
        englishUi ? L"Choose Desktop Archive Folder" : L"\u9009\u62E9\u684C\u9762\u6536\u7EB3\u76EE\u5F55");
    if (!chosen.has_value()) {
        return std::nullopt;
    }

    const core::DesktopKnownFolders desktops = GetSystemDesktopFolders();
    const core::ArchiveDirValidation valid =
        core::ValidateArchiveDirectory(*chosen, desktops, appDataRoot);
    if (valid == core::ArchiveDirValidation::InsideDesktop) {
        MessageBoxW(
            ownerHwnd,
            englishUi
                ? L"Cannot choose Desktop, a subfolder of Desktop, or a parent containing Desktop.\nPlease choose an independent folder."
                : L"\u4E0D\u80FD\u9009\u62E9\u684C\u9762\u76EE\u5F55\u672C\u8EAB\u3001\u684C\u9762\u5B50\u76EE\u5F55\u6216\u5305\u542B\u684C\u9762\u7684\u76EE\u5F55\uFF0C\u8BF7\u9009\u62E9\u5176\u4ED6\u72EC\u7ACB\u6587\u4EF6\u5939\u3002",
            englishUi ? L"Invalid Folder" : L"\u76EE\u5F55\u4E0D\u53EF\u7528", MB_OK | MB_ICONWARNING);
        return std::nullopt;
    }
    if (valid == core::ArchiveDirValidation::InsideAppData ||
        valid == core::ArchiveDirValidation::Empty) {
        MessageBoxW(
            ownerHwnd,
            englishUi
                ? L"Cannot choose GeekDeskCpp's data folder so updating or uninstalling the launcher will never affect your files.\nPlease choose an independent folder."
                : L"\u4E0D\u80FD\u9009\u62E9 GeekDeskCpp \u7A0B\u5E8F data \u76EE\u5F55\uFF0C\u4EE5\u514D\u66F4\u65B0\u6216\u5220\u9664\u8F6F\u4EF6\u65F6\u8BEF\u5220\u6587\u4EF6\uFF0C\u8BF7\u9009\u62E9\u72EC\u7ACB\u6587\u4EF6\u5939\u3002",
            englishUi ? L"Invalid Folder" : L"\u76EE\u5F55\u4E0D\u53EF\u7528", MB_OK | MB_ICONWARNING);
        return std::nullopt;
    }

    if (core::WouldLeaveOneDrive(desktops.userDesktop, *chosen)) {
        MessageBoxW(
            ownerHwnd,
            englishUi
                ? L"Notice: Your Desktop is inside OneDrive, while the chosen archive folder is outside OneDrive.\nFiles moved here will leave OneDrive cloud sync (they remain safely on your local disk)."
                : L"\u63D0\u793A\uFF1A\u4F60\u5F53\u524D\u7684\u684C\u9762\u4F4D\u4E8E OneDrive \u540C\u6B65\u76EE\u5F55\u4E0B\uFF0C\u800C\u6240\u9009\u6536\u7EB3\u76EE\u5F55\u4E0D\u5728 OneDrive \u5185\u3002\n\u79FB\u5165\u6B64\u5904\u7684\u6587\u4EF6\u5C06\u9000\u51FA OneDrive \u4E91\u540C\u6B65\uFF08\u672C\u5730\u6587\u4EF6\u4F1A\u5B89\u5168\u4FDD\u7559\uFF09\u3002",
            englishUi ? L"OneDrive Notice" : L"OneDrive \u540C\u6B65\u63D0\u793A",
            MB_OK | MB_ICONINFORMATION);
    }

    return chosen->lexically_normal().make_preferred();
}

} // namespace geekdesk::platform
