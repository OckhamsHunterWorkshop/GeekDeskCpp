#include "core/desktop_archive_policy.hpp"

#include "core/utf8.hpp"

#include <windows.h>

#include <array>
#include <string>
#include <string_view>

namespace geekdesk::core {

namespace {

std::wstring NormalizeCompareString(const std::filesystem::path& rawPath) {
    if (rawPath.empty()) {
        return {};
    }
    std::filesystem::path norm =
        utf8::NormalizeWindowsPathInput(rawPath.wstring()).lexically_normal().make_preferred();
    std::wstring w = norm.wstring();
    while (w.size() > 3 && w.back() == L'\\') {
        w.pop_back();
    }
    if (w.size() == 2 && w[0] == L'\\' && w[1] == L'\\') {
        return {};
    }
    return w;
}

bool EqualsOrdinalIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    if (a.empty()) {
        return true;
    }
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool IsReservedDeviceName(std::wstring_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    const std::size_t dotPos = name.find(L'.');
    const std::wstring_view stem = (dotPos == std::wstring_view::npos) ? name : name.substr(0, dotPos);
    constexpr std::array<std::wstring_view, 22> kReserved = {
        L"CON",  L"PRN",  L"AUX",  L"NUL",  L"COM1", L"COM2", L"COM3", L"COM4",
        L"COM5", L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2", L"LPT3",
        L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9",
    };
    for (const auto r : kReserved) {
        if (EqualsOrdinalIgnoreCase(stem, r)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool IsPathEqualOrUnder(const std::filesystem::path& candidate,
                        const std::filesystem::path& root) {
    const std::wstring c = NormalizeCompareString(candidate);
    const std::wstring r = NormalizeCompareString(root);
    if (c.empty() || r.empty()) {
        return false;
    }
    if (EqualsOrdinalIgnoreCase(c, r)) {
        return true;
    }
    if (c.size() <= r.size()) {
        return false;
    }
    if (!EqualsOrdinalIgnoreCase(std::wstring_view(c).substr(0, r.size()), r)) {
        return false;
    }
    if (r.back() == L'\\') {
        return true;
    }
    return c[r.size()] == L'\\';
}

bool IsDirectChildOfDesktop(const std::filesystem::path& itemPath,
                            const DesktopKnownFolders& desktops) {
    const std::filesystem::path clean = utf8::NormalizeWindowsPathInput(itemPath.wstring());
    if (clean.empty()) {
        return false;
    }
    const std::wstring parentStr = NormalizeCompareString(clean.parent_path());
    if (parentStr.empty()) {
        return false;
    }
    const std::wstring userDesk = NormalizeCompareString(desktops.userDesktop);
    const std::wstring pubDesk = NormalizeCompareString(desktops.publicDesktop);
    return (!userDesk.empty() && EqualsOrdinalIgnoreCase(parentStr, userDesk)) ||
           (!pubDesk.empty() && EqualsOrdinalIgnoreCase(parentStr, pubDesk));
}

ArchiveDirValidation ValidateArchiveDirectory(const std::filesystem::path& archiveDir,
                                              const DesktopKnownFolders& desktops,
                                              const std::filesystem::path& appDataRoot) {
    const std::filesystem::path clean = utf8::NormalizeWindowsPathInput(archiveDir.wstring());
    if (clean.empty()) {
        return ArchiveDirValidation::Empty;
    }

    auto overlaps = [&](const std::filesystem::path& target) -> bool {
        if (target.empty()) {
            return false;
        }
        return IsPathEqualOrUnder(clean, target) || IsPathEqualOrUnder(target, clean);
    };

    if (overlaps(desktops.userDesktop) || overlaps(desktops.publicDesktop)) {
        return ArchiveDirValidation::InsideDesktop;
    }
    if (overlaps(appDataRoot)) {
        return ArchiveDirValidation::InsideAppData;
    }
    return ArchiveDirValidation::Ok;
}

std::optional<std::filesystem::path> DetectOneDriveRoot(const std::filesystem::path& path) {
    const std::filesystem::path norm =
        utf8::NormalizeWindowsPathInput(path.wstring()).lexically_normal().make_preferred();
    if (norm.empty()) {
        return std::nullopt;
    }

    std::filesystem::path prefix;
    for (const auto& part : norm) {
        prefix /= part;
        const std::string lower = utf8::ToLowerAscii(utf8::WideToUtf8(part.wstring()));
        if (lower.find("onedrive") != std::string::npos) {
            return prefix;
        }
    }
    return std::nullopt;
}

bool WouldLeaveOneDrive(const std::filesystem::path& userDesktop,
                        const std::filesystem::path& archiveDir) {
    const auto odRoot = DetectOneDriveRoot(userDesktop);
    if (!odRoot.has_value()) {
        return false;
    }
    return !IsPathEqualOrUnder(archiveDir, *odRoot);
}

std::wstring SanitizeGroupFolderName(std::string_view groupName) {
    std::wstring wide = utf8::Utf8ToWide(groupName);
    for (wchar_t& ch : wide) {
        if (ch < 0x20 || ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' || ch == L'/' ||
            ch == L'\\' || ch == L'|' || ch == L'?' || ch == L'*') {
            ch = L'_';
        }
    }
    while (!wide.empty() && (wide.front() == L' ' || wide.front() == L'\t')) {
        wide.erase(wide.begin());
    }
    while (!wide.empty() && (wide.back() == L' ' || wide.back() == L'\t' || wide.back() == L'.')) {
        wide.pop_back();
    }
    if (wide.empty()) {
        return L"Default";
    }
    if (IsReservedDeviceName(wide)) {
        wide.insert(wide.begin(), L'_');
    }
    return wide;
}

std::optional<std::filesystem::path> FindArchivedPhysicalPath(
    const LaunchItem& item,
    const std::filesystem::path& archiveRoot) {
    const std::wstring cleanRoot = NormalizeCompareString(archiveRoot);
    if (cleanRoot.empty()) {
        return std::nullopt;
    }

    if (item.lnkPath.has_value() && !item.lnkPath->empty()) {
        const std::filesystem::path lnk =
            utf8::NormalizeWindowsPathInput(utf8::Utf8ToWide(*item.lnkPath));
        if (!lnk.empty() && IsPathEqualOrUnder(lnk, archiveRoot)) {
            return lnk;
        }
    }

    if (!item.targetPath.empty()) {
        const std::filesystem::path target =
            utf8::NormalizeWindowsPathInput(utf8::Utf8ToWide(item.targetPath));
        if (!target.empty() && IsPathEqualOrUnder(target, archiveRoot)) {
            return target;
        }
    }

    return std::nullopt;
}

} // namespace geekdesk::core
