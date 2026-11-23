#include "platform/shell_launcher.hpp"

#include "core/utf8.hpp"
#include "platform/file_dialogs.hpp"
#include "platform/shortcut_resolver.hpp"

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <filesystem>
#include <string>

namespace geekdesk::platform {

namespace {

std::wstring ChooseLaunchFile(const core::LaunchItem& item) {
    if (item.lnkPath.has_value() && !item.lnkPath->empty() && item.arguments.empty() &&
        !item.runAsAdmin) {
        const std::filesystem::path p =
            core::utf8::NormalizeWindowsPathInput(core::utf8::Utf8ToWide(*item.lnkPath));
        std::error_code ec;
        if (!p.empty() && std::filesystem::exists(p, ec)) {
            return p.wstring();
        }
    }
    return core::utf8::ExpandEnvVarsWide(core::utf8::Utf8ToWide(item.targetPath));
}

std::filesystem::path TryResolveLnkTarget(const std::filesystem::path& lnkPath) {
    if (lnkPath.empty()) {
        return {};
    }
    if (core::utf8::ToLowerAscii(core::utf8::PathToUtf8(lnkPath.extension())) != ".lnk") {
        return {};
    }
    std::error_code ec;
    if (!std::filesystem::exists(lnkPath, ec)) {
        return {};
    }
    const auto resolved = ResolveShortcutFile(lnkPath);
    if (!resolved.ok || resolved.isUrl || resolved.targetPath.empty()) {
        return {};
    }
    return core::utf8::NormalizeWindowsPathInput(core::utf8::Utf8ToWide(resolved.targetPath));
}

std::filesystem::path TrySearchOnPath(const std::filesystem::path& fileName) {
    if (fileName.empty() || fileName.has_parent_path()) {
        return {};
    }
    std::array<wchar_t, 32768> foundBuf{};
    const DWORD len = SearchPathW(nullptr, fileName.c_str(), L".exe",
                                  static_cast<DWORD>(foundBuf.size()), foundBuf.data(), nullptr);
    if (len == 0 || len >= foundBuf.size()) {
        return {};
    }
    return std::filesystem::path(std::wstring_view(foundBuf.data(), len));
}

} // namespace

LaunchResult LaunchCatalogItem(const core::LaunchItem& item) {
    LaunchResult result;
    const std::wstring file = ChooseLaunchFile(item);
    if (file.empty()) {
        result.error = "Launch target path is empty";
        return result;
    }

    const std::wstring args = core::utf8::ExpandEnvVarsWide(core::utf8::Utf8ToWide(item.arguments));
    std::wstring workDir;
    if (item.workingDirectory.has_value() && !item.workingDirectory->empty()) {
        workDir = core::utf8::ExpandEnvVarsWide(core::utf8::Utf8ToWide(*item.workingDirectory));
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

LaunchResult RevealItemLocation(const core::LaunchItem& item) {
    LaunchResult result;
    std::error_code ec;

    const auto tryReveal = [&](const std::filesystem::path& candidate) -> bool {
        if (candidate.empty()) {
            return false;
        }
        result.resolvedPath = core::utf8::PathToUtf8(candidate);
        if (RevealInExplorer(candidate)) {
            result.ok = true;
            return true;
        }
        return false;
    };

    const std::filesystem::path lnkPath =
        (item.lnkPath.has_value() && !item.lnkPath->empty())
            ? core::utf8::NormalizeWindowsPathInput(core::utf8::Utf8ToWide(*item.lnkPath))
            : std::filesystem::path{};

    if (item.kind == core::ItemKind::Url) {
        if (!lnkPath.empty() && std::filesystem::exists(lnkPath, ec) && tryReveal(lnkPath)) {
            return result;
        }
        result.error = "URL items have no local file location";
        return result;
    }

    std::filesystem::path target =
        core::utf8::NormalizeWindowsPathInput(core::utf8::Utf8ToWide(item.targetPath));

    if (!target.empty()) {
        if (const auto resolvedLnkTarget = TryResolveLnkTarget(target);
            !resolvedLnkTarget.empty() && std::filesystem::exists(resolvedLnkTarget, ec)) {
            target = resolvedLnkTarget;
        }
        if (std::filesystem::exists(target, ec) && tryReveal(target)) {
            return result;
        }
        if (const auto onPath = TrySearchOnPath(target);
            !onPath.empty() && std::filesystem::exists(onPath, ec) && tryReveal(onPath)) {
            return result;
        }
    }

    if (!lnkPath.empty()) {
        if (const auto resolvedFromLnk = TryResolveLnkTarget(lnkPath);
            !resolvedFromLnk.empty() && std::filesystem::exists(resolvedFromLnk, ec) &&
            tryReveal(resolvedFromLnk)) {
            return result;
        }
        if (std::filesystem::exists(lnkPath, ec) && tryReveal(lnkPath)) {
            return result;
        }
    }

    if (!target.empty() && tryReveal(target)) {
        return result;
    }

    result.error = "Target path does not exist";
    return result;
}

} // namespace geekdesk::platform
