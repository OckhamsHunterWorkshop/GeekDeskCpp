#include "core/catalog_sorter.hpp"

#include "core/utf8.hpp"

#include <windows.h>

#include <algorithm>
#include <limits>

namespace geekdesk::core {

int CompareNaturalNames(std::string_view aUtf8, std::string_view bUtf8) {
    const std::wstring wa = utf8::Utf8ToWide(aUtf8);
    const std::wstring wb = utf8::Utf8ToWide(bUtf8);

    const int cmp = CompareStringEx(
        LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, wa.data(),
        static_cast<int>(wa.size()), wb.data(), static_cast<int>(wb.size()), nullptr, nullptr, 0);
    if (cmp == 0) {
        if (wa < wb) {
            return -1;
        }
        if (wa > wb) {
            return 1;
        }
        return 0;
    }
    return cmp - CSTR_EQUAL;
}

void SortGroups(std::vector<CategoryGroup>& groups, SortOrder order) {
    switch (order) {
    case SortOrder::Custom:
        return;
    case SortOrder::NameAsc:
        std::stable_sort(groups.begin(), groups.end(),
                         [](const CategoryGroup& a, const CategoryGroup& b) {
                             return CompareNaturalNames(a.name, b.name) < 0;
                         });
        return;
    case SortOrder::NameDesc:
        std::stable_sort(groups.begin(), groups.end(),
                         [](const CategoryGroup& a, const CategoryGroup& b) {
                             return CompareNaturalNames(a.name, b.name) > 0;
                         });
        return;
    case SortOrder::LaunchCountAsc:
    case SortOrder::LaunchCountDesc: {
        auto sumLaunches = [](const CategoryGroup& g) -> long long {
            long long total = 0;
            for (const auto& item : g.items) {
                total += item.launchCount;
            }
            return total;
        };
        const bool desc = (order == SortOrder::LaunchCountDesc);
        std::stable_sort(groups.begin(), groups.end(),
                         [desc, &sumLaunches](const CategoryGroup& a, const CategoryGroup& b) {
                             const long long sa = sumLaunches(a);
                             const long long sb = sumLaunches(b);
                             if (sa != sb) {
                                 return desc ? (sa > sb) : (sa < sb);
                             }
                             return CompareNaturalNames(a.name, b.name) < 0;
                         });
        return;
    }
    }
}

void SortItems(std::vector<LaunchItem>& items, SortOrder order) {
    switch (order) {
    case SortOrder::Custom:
        return;
    case SortOrder::NameAsc:
        std::stable_sort(items.begin(), items.end(), [](const LaunchItem& a, const LaunchItem& b) {
            return CompareNaturalNames(a.name, b.name) < 0;
        });
        return;
    case SortOrder::NameDesc:
        std::stable_sort(items.begin(), items.end(), [](const LaunchItem& a, const LaunchItem& b) {
            return CompareNaturalNames(a.name, b.name) > 0;
        });
        return;
    case SortOrder::LaunchCountAsc:
        std::stable_sort(items.begin(), items.end(), [](const LaunchItem& a, const LaunchItem& b) {
            if (a.launchCount != b.launchCount) {
                return a.launchCount < b.launchCount;
            }
            return CompareNaturalNames(a.name, b.name) < 0;
        });
        return;
    case SortOrder::LaunchCountDesc:
        std::stable_sort(items.begin(), items.end(), [](const LaunchItem& a, const LaunchItem& b) {
            if (a.launchCount != b.launchCount) {
                return a.launchCount > b.launchCount;
            }
            return CompareNaturalNames(a.name, b.name) < 0;
        });
        return;
    }
}

} // namespace geekdesk::core
