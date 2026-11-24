#include "core/file_identity.hpp"

#include "core/utf8.hpp"

#include <windows.h>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <system_error>
#include <vector>

namespace geekdesk::core {

namespace {

struct HandleGuard {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~HandleGuard() {
        if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
            CloseHandle(handle);
        }
    }
    [[nodiscard]] bool Valid() const noexcept {
        return handle != INVALID_HANDLE_VALUE && handle != nullptr;
    }
};

struct ParsedFileId {
    std::uint32_t volumeSerial = 0;
    std::uint64_t fileIndex = 0;
};

std::optional<ParsedFileId> ParseFileIdentity(std::string_view encoded) noexcept {
    const std::size_t colon = encoded.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= encoded.size()) {
        return std::nullopt;
    }
    const std::string_view volView = encoded.substr(0, colon);
    const std::string_view idxView = encoded.substr(colon + 1);
    if (volView.size() > 8 || idxView.size() > 16) {
        return std::nullopt;
    }

    ParsedFileId parsed;
    const auto rVol = std::from_chars(volView.data(), volView.data() + volView.size(),
                                      parsed.volumeSerial, 16);
    const auto rIdx = std::from_chars(idxView.data(), idxView.data() + idxView.size(),
                                      parsed.fileIndex, 16);
    if (rVol.ec != std::errc{} || rVol.ptr != volView.data() + volView.size() ||
        rIdx.ec != std::errc{} || rIdx.ptr != idxView.data() + idxView.size()) {
        return std::nullopt;
    }
    return parsed;
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

std::filesystem::path StripExtendedPathPrefix(std::wstring_view rawPath) {
    constexpr std::wstring_view kUncPrefix = L"\\\\?\\UNC\\";
    constexpr std::wstring_view kExtPrefix = L"\\\\?\\";
    std::wstring cleaned;
    if (rawPath.size() >= kUncPrefix.size() &&
        EqualsOrdinalIgnoreCase(rawPath.substr(0, kUncPrefix.size()), kUncPrefix)) {
        cleaned = L"\\\\";
        cleaned.append(rawPath.substr(kUncPrefix.size()));
    } else if (rawPath.size() >= kExtPrefix.size() &&
               rawPath.substr(0, kExtPrefix.size()) == kExtPrefix) {
        cleaned.assign(rawPath.substr(kExtPrefix.size()));
    } else {
        cleaned.assign(rawPath);
    }
    std::filesystem::path out(std::move(cleaned));
    out.make_preferred();
    return out;
}

} // namespace

std::optional<std::string> CaptureFileIdentity(const std::filesystem::path& path) {
    const std::filesystem::path norm = utf8::NormalizeWindowsPathInput(path.wstring());
    if (norm.empty()) {
        return std::nullopt;
    }

    HandleGuard file{CreateFileW(norm.c_str(), 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (!file.Valid()) {
        return std::nullopt;
    }

    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.handle, &info)) {
        return std::nullopt;
    }
    if (info.dwVolumeSerialNumber == 0 && info.nFileIndexHigh == 0 && info.nFileIndexLow == 0) {
        return std::nullopt;
    }

    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%08lx:%08lx%08lx",
                  static_cast<unsigned long>(info.dwVolumeSerialNumber),
                  static_cast<unsigned long>(info.nFileIndexHigh),
                  static_cast<unsigned long>(info.nFileIndexLow));
    return std::string(buf);
}

bool IsPathInRecycleBin(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }
    for (const auto& part : path) {
        const std::string lower = utf8::ToLowerAscii(utf8::WideToUtf8(part.wstring()));
        if (lower == "$recycle.bin" || lower == "recycler") {
            return true;
        }
    }
    return false;
}

FileIdRecoveryResult TryRecoverPathByFileId(std::string_view encodedFileId,
                                            const std::filesystem::path& originalPathHint) {
    FileIdRecoveryResult result;
    const auto parsed = ParseFileIdentity(encodedFileId);
    if (!parsed.has_value()) {
        return result;
    }

    const std::filesystem::path normHint =
        utf8::NormalizeWindowsPathInput(originalPathHint.wstring());
    const std::filesystem::path rootDir = normHint.root_path();
    if (rootDir.empty()) {
        return result;
    }

    HandleGuard vol{CreateFileW(rootDir.c_str(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (!vol.Valid()) {
        return result;
    }

    DWORD actualSerial = 0;
    if (!GetVolumeInformationByHandleW(vol.handle, nullptr, 0, &actualSerial, nullptr, nullptr,
                                       nullptr, 0) ||
        actualSerial != parsed->volumeSerial) {
        return result;
    }

    FILE_ID_DESCRIPTOR desc{};
    desc.dwSize = sizeof(desc);
    desc.Type = FileIdType;
    desc.FileId.QuadPart = static_cast<LONGLONG>(parsed->fileIndex);

    HandleGuard found{OpenFileById(vol.handle, &desc, FILE_READ_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   FILE_FLAG_BACKUP_SEMANTICS)};
    if (!found.Valid()) {
        return result;
    }

    std::vector<wchar_t> pathBuf(512, L'\0');
    DWORD len = GetFinalPathNameByHandleW(found.handle, pathBuf.data(),
                                          static_cast<DWORD>(pathBuf.size()),
                                          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (len >= pathBuf.size()) {
        pathBuf.assign(static_cast<std::size_t>(len) + 1, L'\0');
        len = GetFinalPathNameByHandleW(found.handle, pathBuf.data(),
                                        static_cast<DWORD>(pathBuf.size()),
                                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    }
    if (len == 0 || len >= pathBuf.size()) {
        return result;
    }

    const std::filesystem::path resolved =
        StripExtendedPathPrefix(std::wstring_view(pathBuf.data(), len));
    if (resolved.empty()) {
        return result;
    }

    if (IsPathInRecycleBin(resolved)) {
        result.status = FileIdRecoveryStatus::InRecycleBin;
        return result;
    }

    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec)) {
        return result;
    }

    result.status = FileIdRecoveryStatus::Recovered;
    result.recoveredPath = resolved;
    return result;
}

bool ShouldAutoUpdateDisplayName(std::string_view currentName,
                                 const std::filesystem::path& oldPath) {
    const std::string_view trimmed = utf8::TrimAscii(currentName);
    if (trimmed.empty() || oldPath.empty()) {
        return false;
    }
    const std::wstring curW = utf8::Utf8ToWide(trimmed);
    const std::wstring fileW = oldPath.filename().wstring();
    const std::wstring stemW = oldPath.stem().wstring();
    return (!fileW.empty() && EqualsOrdinalIgnoreCase(curW, fileW)) ||
           (!stemW.empty() && EqualsOrdinalIgnoreCase(curW, stemW));
}

std::string DeriveDisplayNameFromPath(const std::filesystem::path& newPath, ItemKind kind) {
    if (kind == ItemKind::Folder) {
        return utf8::PathToUtf8(newPath.filename());
    }
    const std::string extLower = utf8::ToLowerAscii(utf8::PathToUtf8(newPath.extension()));
    if (extLower == ".lnk" || extLower == ".url" || extLower == ".exe") {
        const std::string stem = utf8::PathToUtf8(newPath.stem());
        if (!stem.empty()) {
            return stem;
        }
    }
    return utf8::PathToUtf8(newPath.filename());
}

} // namespace geekdesk::core
