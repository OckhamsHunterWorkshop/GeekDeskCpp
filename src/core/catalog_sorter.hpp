#pragma once

#include "core/models.hpp"

#include <string_view>
#include <vector>

namespace geekdesk::core {

/// Natural case-insensitive comparison using Windows locale rules (handles digits + CJK).
/// Returns < 0 if a < b, 0 if equal, > 0 if a > b.
[[nodiscard]] int CompareNaturalNames(std::string_view aUtf8, std::string_view bUtf8);

/// Sort category groups in place according to order. SortOrder::Custom is a no-op.
void SortGroups(std::vector<CategoryGroup>& groups, SortOrder order);

/// Sort launch items in place according to order. SortOrder::Custom is a no-op.
void SortItems(std::vector<LaunchItem>& items, SortOrder order);

} // namespace geekdesk::core
