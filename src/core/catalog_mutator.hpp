#pragma once

#include "core/models.hpp"

#include <cstddef>
#include <string>

namespace geekdesk::core {

/// Append a new empty Manual category group and return its index.
std::size_t AddManualGroup(CatalogDocument& catalog, std::string name);

/// Append a new LinkedFolder category group, immediately run SyncLinkedFolderGroup,
/// and return its index.
std::size_t AddLinkedFolderGroup(CatalogDocument& catalog,
                                 std::string name,
                                 std::string folderPath);

/// Rename an existing group. Returns false if groupIndex is invalid or trimmed name is empty.
bool RenameGroup(CatalogDocument& catalog, std::size_t groupIndex, std::string newName);

/// Remove group at groupIndex. If the catalog becomes empty, creates a fallback default group
/// so the UI always has at least one valid category.
bool DeleteGroup(CatalogDocument& catalog, std::size_t groupIndex);

/// Move a group from fromIndex to toIndex.
bool ReorderGroup(std::vector<CategoryGroup>& groups,
                  std::size_t fromIndex,
                  std::size_t toIndex);

/// Add item into a Manual group. Rejects LinkedFolder groups or empty names/targets.
/// When deduplicateByTarget is true, skips adding if a case-insensitive matching targetPath exists.
bool AddItemToManualGroup(CategoryGroup& group,
                          LaunchItem item,
                          bool deduplicateByTarget = true);

/// Remove item at itemIndex from a Manual group. Returns false for LinkedFolder or out-of-range.
bool RemoveItemFromManualGroup(CategoryGroup& group, std::size_t itemIndex);

/// Move an item within its items vector from fromIndex to toIndex.
bool ReorderItem(std::vector<LaunchItem>& items, std::size_t fromIndex, std::size_t toIndex);

/// Move an item from one Manual group to another Manual group (appends & deduplicates).
bool MoveItemBetweenManualGroups(CatalogDocument& catalog,
                                 std::size_t srcGroupIndex,
                                 std::size_t itemIndex,
                                 std::size_t dstGroupIndex);

} // namespace geekdesk::core
