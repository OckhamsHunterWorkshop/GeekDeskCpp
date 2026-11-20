#include "core/catalog_mutator.hpp"

#include "core/folder_sync.hpp"
#include "core/utf8.hpp"

#include <algorithm>
#include <utility>

namespace geekdesk::core {

namespace {

template <typename T>
bool MoveElement(std::vector<T>& vec, std::size_t fromIndex, std::size_t toIndex) {
    if (fromIndex >= vec.size() || toIndex >= vec.size() || fromIndex == toIndex) {
        return false;
    }
    if (fromIndex < toIndex) {
        std::rotate(vec.begin() + static_cast<std::ptrdiff_t>(fromIndex),
                    vec.begin() + static_cast<std::ptrdiff_t>(fromIndex + 1),
                    vec.begin() + static_cast<std::ptrdiff_t>(toIndex + 1));
    } else {
        std::rotate(vec.begin() + static_cast<std::ptrdiff_t>(toIndex),
                    vec.begin() + static_cast<std::ptrdiff_t>(fromIndex),
                    vec.begin() + static_cast<std::ptrdiff_t>(fromIndex + 1));
    }
    return true;
}

} // namespace

std::size_t AddManualGroup(CatalogDocument& catalog, std::string name) {
    const std::string_view trimmed = utf8::TrimAscii(name);
    CategoryGroup group;
    group.id = utf8::GenerateUuidV4();
    group.name = trimmed.empty() ? std::string("\xE6\x96\xB0\xE5\x88\x86\xE7\xBB\x84")
                                 : std::string(trimmed);
    group.kind = GroupKind::Manual;
    catalog.groups.push_back(std::move(group));
    return catalog.groups.size() - 1;
}

std::size_t AddLinkedFolderGroup(CatalogDocument& catalog,
                                 std::string name,
                                 std::string folderPath) {
    const std::string_view trimmed = utf8::TrimAscii(name);
    CategoryGroup group;
    group.id = utf8::GenerateUuidV4();
    group.name = trimmed.empty() ? std::string("\xE5\x85\xB3\xE8\x81\x94\xE7\x9B\xAE\xE5\xBD\x95")
                                 : std::string(trimmed);
    group.kind = GroupKind::LinkedFolder;
    group.linkedFolderPath = std::move(folderPath);
    (void)SyncLinkedFolderGroup(group);
    catalog.groups.push_back(std::move(group));
    return catalog.groups.size() - 1;
}

bool RenameGroup(CatalogDocument& catalog, std::size_t groupIndex, std::string newName) {
    if (groupIndex >= catalog.groups.size()) {
        return false;
    }
    const std::string_view trimmed = utf8::TrimAscii(newName);
    if (trimmed.empty()) {
        return false;
    }
    catalog.groups[groupIndex].name.assign(trimmed);
    return true;
}

bool DeleteGroup(CatalogDocument& catalog, std::size_t groupIndex) {
    if (groupIndex >= catalog.groups.size()) {
        return false;
    }
    catalog.groups.erase(catalog.groups.begin() + static_cast<std::ptrdiff_t>(groupIndex));
    if (catalog.groups.empty()) {
        catalog = CatalogDocument::CreateDefault();
    }
    return true;
}

bool ReorderGroup(std::vector<CategoryGroup>& groups,
                  std::size_t fromIndex,
                  std::size_t toIndex) {
    return MoveElement(groups, fromIndex, toIndex);
}

bool AddItemToManualGroup(CategoryGroup& group, LaunchItem item, bool deduplicateByTarget) {
    if (group.kind != GroupKind::Manual) {
        return false;
    }
    const std::string_view targetTrim = utf8::TrimAscii(item.targetPath);
    const std::string_view nameTrim = utf8::TrimAscii(item.name);
    if (targetTrim.empty() || nameTrim.empty()) {
        return false;
    }
    item.targetPath.assign(targetTrim);
    item.name.assign(nameTrim);

    if (deduplicateByTarget) {
        const std::string newLower = utf8::ToLowerAscii(item.targetPath);
        for (const auto& existing : group.items) {
            if (utf8::ToLowerAscii(utf8::TrimAscii(existing.targetPath)) == newLower) {
                return false;
            }
        }
    }

    if (item.id.empty()) {
        item.id = utf8::GenerateUuidV4();
    }
    group.items.push_back(std::move(item));
    return true;
}

bool RemoveItemFromManualGroup(CategoryGroup& group, std::size_t itemIndex) {
    if (group.kind != GroupKind::Manual || itemIndex >= group.items.size()) {
        return false;
    }
    group.items.erase(group.items.begin() + static_cast<std::ptrdiff_t>(itemIndex));
    return true;
}

bool ReorderItem(std::vector<LaunchItem>& items, std::size_t fromIndex, std::size_t toIndex) {
    return MoveElement(items, fromIndex, toIndex);
}

bool MoveItemBetweenManualGroups(CatalogDocument& catalog,
                                 std::size_t srcGroupIndex,
                                 std::size_t itemIndex,
                                 std::size_t dstGroupIndex) {
    if (srcGroupIndex >= catalog.groups.size() || dstGroupIndex >= catalog.groups.size() ||
        srcGroupIndex == dstGroupIndex) {
        return false;
    }
    CategoryGroup& src = catalog.groups[srcGroupIndex];
    CategoryGroup& dst = catalog.groups[dstGroupIndex];
    if (src.kind != GroupKind::Manual || dst.kind != GroupKind::Manual ||
        itemIndex >= src.items.size()) {
        return false;
    }

    LaunchItem candidate = src.items[itemIndex];
    if (!AddItemToManualGroup(dst, std::move(candidate), true)) {
        return false;
    }
    src.items.erase(src.items.begin() + static_cast<std::ptrdiff_t>(itemIndex));
    return true;
}

} // namespace geekdesk::core
