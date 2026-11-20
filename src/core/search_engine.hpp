#pragma once

#include "core/models.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace geekdesk::core {

struct SearchMatch {
    std::string groupId;
    std::string groupName;
    std::string itemId;
    std::string itemName;
    std::string targetPath;
    int launchCount = 0;
    int score = 0;
};

class SearchEngine {
public:
    /// Rebuild search index from the active catalog document.
    void Rebuild(const CatalogDocument& catalog);

    /// Search local catalog items by name, pinyin full/initials, fuzzy subsequence, or path.
    /// Does NOT search external filesystem (no Everything).
    [[nodiscard]] std::vector<SearchMatch> Search(std::string_view query,
                                                  std::size_t maxResults = 50) const;

    /// Return most frequently launched items when no query is active.
    [[nodiscard]] std::vector<SearchMatch> TopFrequent(std::size_t maxResults = 16) const;

    [[nodiscard]] std::size_t IndexedCount() const noexcept {
        return entries_.size();
    }

private:
    struct Entry {
        std::string groupId;
        std::string groupName;
        std::string itemId;
        std::string itemName;
        std::string targetPath;
        int launchCount = 0;

        std::string nameLower;
        std::string pinyinFull;
        std::string pinyinInitials;
        std::string targetLower;
    };

    std::vector<Entry> entries_;
};

} // namespace geekdesk::core
