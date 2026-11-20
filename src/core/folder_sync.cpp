#include "core/folder_sync.hpp"

#include "core/catalog_sorter.hpp"
#include "core/utf8.hpp"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace geekdesk::core {

namespace {

bool ShouldIgnoreEntry(const std::wstring& fileName, DWORD attributes) {
    if ((attributes & FILE_ATTRIBUTE_HIDDEN) != 0 || (attributes & FILE_ATTRIBUTE_SYSTEM) != 0) {
        return true;
    }
    const std::string lower = utf8::ToLowerAscii(utf8::WideToUtf8(fileName));
    return lower == "desktop.ini" || lower == "thumbs.db" || lower == "$recycle.bin";
}

std::string DeriveDisplayName(const std::filesystem::path& entryPath, bool isDir) {
    if (isDir) {
        return utf8::PathToUtf8(entryPath.filename());
    }
    const std::string extLower = utf8::ToLowerAscii(utf8::PathToUtf8(entryPath.extension()));
    if (extLower == ".lnk" || extLower == ".url" || extLower == ".exe") {
        const std::string stem = utf8::PathToUtf8(entryPath.stem());
        if (!stem.empty()) {
            return stem;
        }
    }
    return utf8::PathToUtf8(entryPath.filename());
}

} // namespace

FolderSyncResult SyncLinkedFolderGroup(CategoryGroup& group) {
    FolderSyncResult summary;
    if (group.kind != GroupKind::LinkedFolder || !group.linkedFolderPath.has_value() ||
        group.linkedFolderPath->empty()) {
        return summary;
    }

    const std::filesystem::path root = utf8::PathFromUtf8(*group.linkedFolderPath);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return summary;
    }
    summary.folderExists = true;

    std::unordered_map<std::string, LaunchItem> existingByRel;
    existingByRel.reserve(group.items.size());
    for (auto& existing : group.items) {
        std::string key;
        if (existing.relativePath.has_value() && !existing.relativePath->empty()) {
            key = utf8::ToLowerAscii(*existing.relativePath);
        } else {
            key = utf8::ToLowerAscii(
                utf8::PathToUtf8(utf8::PathFromUtf8(existing.targetPath).filename()));
        }
        existingByRel.try_emplace(std::move(key), std::move(existing));
    }

    std::vector<LaunchItem> nextItems;
    const std::wstring pattern = (root / L"*").wstring();
    WIN32_FIND_DATAW findData{};
    HANDLE hFind = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &findData,
                                    FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring fileName(findData.cFileName);
            if (fileName == L"." || fileName == L"..") {
                continue;
            }
            if (ShouldIgnoreEntry(fileName, findData.dwFileAttributes)) {
                continue;
            }

            const bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const std::filesystem::path fullPath = root / fileName;
            const std::string relUtf8 = utf8::WideToUtf8(fileName);
            const std::string relKey = utf8::ToLowerAscii(relUtf8);
            const std::string fullUtf8 = utf8::PathToUtf8(fullPath);

            auto found = existingByRel.find(relKey);
            if (found != existingByRel.end()) {
                LaunchItem item = std::move(found->second);
                item.targetPath = fullUtf8;
                item.relativePath = relUtf8;
                item.kind = isDir ? ItemKind::Folder : ItemKind::File;
                if (item.name.empty()) {
                    item.name = DeriveDisplayName(fullPath, isDir);
                }
                nextItems.push_back(std::move(item));
                existingByRel.erase(found);
                ++summary.preserved;
            } else {
                LaunchItem item;
                item.id = utf8::GenerateUuidV4();
                item.name = DeriveDisplayName(fullPath, isDir);
                item.kind = isDir ? ItemKind::Folder : ItemKind::File;
                item.targetPath = fullUtf8;
                item.relativePath = relUtf8;
                nextItems.push_back(std::move(item));
                ++summary.added;
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }

    summary.removed = existingByRel.size();
    std::stable_sort(nextItems.begin(), nextItems.end(),
                     [](const LaunchItem& a, const LaunchItem& b) {
                         if (a.kind != b.kind) {
                             return a.kind == ItemKind::Folder;
                         }
                         return CompareNaturalNames(a.name, b.name) < 0;
                     });
    group.items = std::move(nextItems);
    return summary;
}

} // namespace geekdesk::core
