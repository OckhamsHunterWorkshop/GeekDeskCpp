#include "platform/shortcut_resolver.hpp"

#include "core/utf8.hpp"
#include "platform/com_ptr.hpp"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <array>

namespace geekdesk::platform {

ResolvedShortcut ResolveShortcutFile(const std::filesystem::path& shortcutPath) {
    ResolvedShortcut out;
    const std::string extLower =
        core::utf8::ToLowerAscii(core::utf8::PathToUtf8(shortcutPath.extension()));

    if (extLower == ".url") {
        std::array<wchar_t, 2048> urlBuf{};
        const DWORD len = GetPrivateProfileStringW(L"InternetShortcut", L"URL", L"",
                                                   urlBuf.data(), static_cast<DWORD>(urlBuf.size()),
                                                   shortcutPath.c_str());
        if (len > 0) {
            out.ok = true;
            out.isUrl = true;
            out.targetPath = core::utf8::WideToUtf8(std::wstring_view(urlBuf.data(), len));
        }
        return out;
    }

    if (extLower != ".lnk") {
        return out;
    }

    ComPtr<IShellLinkW> shellLink;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                reinterpret_cast<void**>(shellLink.ReleaseAndGetAddressOf())))) {
        return out;
    }

    ComPtr<IPersistFile> persistFile;
    if (FAILED(shellLink->QueryInterface(IID_IPersistFile,
                                         reinterpret_cast<void**>(
                                             persistFile.ReleaseAndGetAddressOf())))) {
        return out;
    }

    if (FAILED(persistFile->Load(shortcutPath.c_str(), STGM_READ))) {
        return out;
    }

    std::array<wchar_t, 32768> wideBuf{};
    if (SUCCEEDED(shellLink->GetPath(wideBuf.data(), static_cast<int>(wideBuf.size()), nullptr,
                                     SLGP_RAWPATH)) &&
        wideBuf[0] != L'\0') {
        out.targetPath = core::utf8::WideToUtf8(wideBuf.data());
    }

    wideBuf[0] = L'\0';
    if (SUCCEEDED(shellLink->GetArguments(wideBuf.data(), static_cast<int>(wideBuf.size()))) &&
        wideBuf[0] != L'\0') {
        out.arguments = core::utf8::WideToUtf8(wideBuf.data());
    }

    wideBuf[0] = L'\0';
    if (SUCCEEDED(shellLink->GetWorkingDirectory(wideBuf.data(),
                                                 static_cast<int>(wideBuf.size()))) &&
        wideBuf[0] != L'\0') {
        out.workingDirectory = core::utf8::WideToUtf8(wideBuf.data());
    }

    wideBuf[0] = L'\0';
    int iconIdx = 0;
    if (SUCCEEDED(shellLink->GetIconLocation(wideBuf.data(), static_cast<int>(wideBuf.size()),
                                             &iconIdx)) &&
        wideBuf[0] != L'\0') {
        out.iconPath = core::utf8::WideToUtf8(wideBuf.data());
        out.iconIndex = iconIdx;
    }

    out.ok = !out.targetPath.empty();
    return out;
}

} // namespace geekdesk::platform
