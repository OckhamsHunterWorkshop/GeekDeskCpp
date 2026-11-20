#include "platform/shell_launcher.hpp"

#include "core/utf8.hpp"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace geekdesk::platform {

namespace {

std::wstring ExpandEnvStrings(std::wstring_view input) {
    if (input.empty()) {
        return {};
    }
    std::wstring src(input);
    const DWORD needed = ExpandEnvironmentStringsW(src.c_str(), nullptr, 0);
    if (needed == 0) {
        return src;
    }
    std::wstring out(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(src.c_str(), out.data(), needed);
    if (written == 0) {
        return src;
    }
    if (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
}

std::wstring ChooseLaunchFile(const core::LaunchItem& item) {
    if (item.lnkPath.has_value() && !item.lnkPath->empty() && item.arguments.empty() &&
        !item.runAsAdmin) {
        const std::filesystem::path p = core::utf8::PathFromUtf8(*item.lnkPath);
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            return ExpandEnvStrings(p.wstring());
        }
    }
    return ExpandEnvStrings(core::utf8::Utf8ToWide(item.targetPath));
}

} // namespace

LaunchResult LaunchCatalogItem(const core::LaunchItem& item) {
    LaunchResult result;
    const std::wstring file = ChooseLaunchFile(item);
    if (file.empty()) {
        result.error = "Launch target path is empty";
        return result;
    }

    const std::wstring args = ExpandEnvStrings(core::utf8::Utf8ToWide(item.arguments));
    std::wstring workDir;
    if (item.workingDirectory.has_value() && !item.workingDirectory->empty()) {
        workDir = ExpandEnvStrings(core::utf8::Utf8ToWide(*item.workingDirectory));
    } else if (item.kind == core::ItemKind::File) {
        const std::filesystem::path targetFs(file);
        std::error_code ec;
        const std::filesystem::path parent = targetFs.parent_path();
        if (!parent.empty() && std::filesystem::exists(parent, ec)) {
            workDir = parent.wstring();
        }
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_ASYNCOK;
    sei.lpVerb = item.runAsAdmin ? L"runas" : nullptr;
    sei.lpFile = file.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    sei.lpDirectory = workDir.empty() ? nullptr : workDir.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        if (err == ERROR_CANCELLED && item.runAsAdmin) {
            result.error = "Elevation canceled by user";
        } else {
            result.error = "ShellExecuteExW failed (Win32 error " + std::to_string(err) + ")";
        }
        return result;
    }

    result.ok = true;
    return result;
}

LaunchResult OpenDirectoryInExplorer(const std::filesystem::path& dir) {
    LaunchResult result;
    if (dir.empty()) {
        result.error = "Directory path is empty";
        return result;
    }
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        result.error = "Failed to open folder (Win32 error " + std::to_string(GetLastError()) + ")";
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace geekdesk::platform
