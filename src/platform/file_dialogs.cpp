#include "platform/file_dialogs.hpp"

#include "core/utf8.hpp"
#include "platform/com_ptr.hpp"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace geekdesk::platform {

namespace {

std::vector<COMDLG_FILTERSPEC> ToComFilters(const std::vector<FileFilterSpec>& filters) {
    std::vector<COMDLG_FILTERSPEC> out;
    out.reserve(filters.size());
    for (const auto& f : filters) {
        out.push_back(COMDLG_FILTERSPEC{f.label.c_str(), f.pattern.c_str()});
    }
    return out;
}

std::optional<std::filesystem::path> ReadDialogResult(IFileDialog* dialog) {
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.ReleaseAndGetAddressOf())) || !item) {
        return std::nullopt;
    }
    PWSTR rawPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || rawPath == nullptr) {
        return std::nullopt;
    }
    std::filesystem::path result(rawPath);
    CoTaskMemFree(rawPath);
    return result;
}

} // namespace

std::optional<std::filesystem::path> PickFileDialog(HWND owner,
                                                    std::wstring_view title,
                                                    const std::vector<FileFilterSpec>& filters) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.ReleaseAndGetAddressOf())))) {
        return std::nullopt;
    }

    DWORD opts = 0;
    if (SUCCEEDED(dialog->GetOptions(&opts))) {
        dialog->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_NODEREFERENCELINKS);
    }
    if (!title.empty()) {
        std::wstring titleStr(title);
        dialog->SetTitle(titleStr.c_str());
    }
    if (!filters.empty()) {
        const auto specs = ToComFilters(filters);
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        dialog->SetFileTypeIndex(1);
    }

    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return ReadDialogResult(dialog.Get());
}

std::optional<std::filesystem::path> PickFolderDialog(HWND owner, std::wstring_view title) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.ReleaseAndGetAddressOf())))) {
        return std::nullopt;
    }

    DWORD opts = 0;
    if (SUCCEEDED(dialog->GetOptions(&opts))) {
        dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    if (!title.empty()) {
        std::wstring titleStr(title);
        dialog->SetTitle(titleStr.c_str());
    }

    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return ReadDialogResult(dialog.Get());
}

std::optional<std::filesystem::path> SaveFileDialog(HWND owner,
                                                    std::wstring_view title,
                                                    std::wstring_view defaultFileName,
                                                    std::wstring_view defaultExtension,
                                                    const std::vector<FileFilterSpec>& filters) {
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.ReleaseAndGetAddressOf())))) {
        return std::nullopt;
    }

    DWORD opts = 0;
    if (SUCCEEDED(dialog->GetOptions(&opts))) {
        dialog->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST);
    }
    if (!title.empty()) {
        std::wstring titleStr(title);
        dialog->SetTitle(titleStr.c_str());
    }
    if (!defaultFileName.empty()) {
        std::wstring nameStr(defaultFileName);
        dialog->SetFileName(nameStr.c_str());
    }
    if (!defaultExtension.empty()) {
        std::wstring extStr(defaultExtension);
        dialog->SetDefaultExtension(extStr.c_str());
    }
    if (!filters.empty()) {
        const auto specs = ToComFilters(filters);
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        dialog->SetFileTypeIndex(1);
    }

    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return ReadDialogResult(dialog.Get());
}

namespace {

bool SelectPathWithShellApi(const std::filesystem::path& normalized) {
    PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(normalized.c_str());
    if (itemPidl == nullptr) {
        return false;
    }

    HRESULT hr = E_FAIL;
    PIDLIST_ABSOLUTE parentPidl = ILClone(itemPidl);
    if (parentPidl != nullptr) {
        if (ILRemoveLastID(parentPidl)) {
            PCUITEMID_CHILD childPidl = ILFindLastID(itemPidl);
            hr = SHOpenFolderAndSelectItems(parentPidl, 1, &childPidl, 0);
        }
        ILFree(parentPidl);
    }
    if (FAILED(hr)) {
        hr = SHOpenFolderAndSelectItems(itemPidl, 0, nullptr, 0);
    }
    ILFree(itemPidl);
    return SUCCEEDED(hr);
}

} // namespace

bool RevealInExplorer(const std::filesystem::path& path) {
    const std::filesystem::path normalized =
        core::utf8::NormalizeWindowsPathInput(path.wstring());
    if (normalized.empty()) {
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(normalized, ec)) {
        if (SelectPathWithShellApi(normalized)) {
            return true;
        }
        const std::wstring params = L"/select,\"" + normalized.wstring() + L"\"";
        const HINSTANCE h =
            ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(h) > 32;
    }

    const std::filesystem::path parent = normalized.parent_path();
    if (!parent.empty() && std::filesystem::exists(parent, ec)) {
        const HINSTANCE h =
            ShellExecuteW(nullptr, L"open", parent.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(h) > 32;
    }
    return false;
}

} // namespace geekdesk::platform
