#include "core/search_engine.hpp"

#include "core/catalog_sorter.hpp"
#include "core/pinyin.hpp"
#include "core/utf8.hpp"

#include <algorithm>
#include <optional>

namespace geekdesk::core {

namespace {

/// Compute a subsequence fuzzy match score; returns nullopt if query is not a subsequence.
std::optional<int> ScoreSubsequence(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) {
        return std::nullopt;
    }
    std::size_t hi = 0;
    std::size_t firstMatch = std::string_view::npos;
    std::size_t prevMatch = 0;
    int totalGaps = 0;

    for (std::size_t ni = 0; ni < needle.size(); ++ni) {
        const std::size_t found = haystack.find(needle[ni], hi);
        if (found == std::string_view::npos) {
            return std::nullopt;
        }
        if (firstMatch == std::string_view::npos) {
            firstMatch = found;
        } else {
            totalGaps += static_cast<int>(found - prevMatch - 1);
        }
        prevMatch = found;
        hi = found + 1;
    }

    const int penalty = static_cast<int>(firstMatch) * 2 + totalGaps * 4;
    return std::max(100, 1500 - penalty);
}

int ScoreEntry(const auto& entry, std::string_view queryLower) {
    int baseScore = 0;

    if (entry.nameLower == queryLower) {
        baseScore = 10000;
    } else if (entry.nameLower.rfind(queryLower, 0) == 0) {
        baseScore = 8000;
    } else if (const auto pos = entry.nameLower.find(queryLower); pos != std::string::npos) {
        baseScore = 6000 - static_cast<int>(std::min<std::size_t>(pos, 500));
    } else if (!entry.pinyinInitials.empty() && entry.pinyinInitials.rfind(queryLower, 0) == 0) {
        baseScore = 5000;
    } else if (const auto posIni = entry.pinyinInitials.find(queryLower);
               posIni != std::string::npos) {
        baseScore = 4000 - static_cast<int>(std::min<std::size_t>(posIni, 500));
    } else if (!entry.pinyinFull.empty() && entry.pinyinFull.rfind(queryLower, 0) == 0) {
        baseScore = 3500;
    } else if (const auto posFull = entry.pinyinFull.find(queryLower);
               posFull != std::string::npos) {
        baseScore = 2500 - static_cast<int>(std::min<std::size_t>(posFull, 500));
    } else if (auto subName = ScoreSubsequence(entry.nameLower, queryLower); subName.has_value()) {
        baseScore = *subName;
    } else if (auto subIni = ScoreSubsequence(entry.pinyinInitials, queryLower);
               subIni.has_value()) {
        baseScore = *subIni - 100;
    } else if (auto subFull = ScoreSubsequence(entry.pinyinFull, queryLower);
               subFull.has_value()) {
        baseScore = *subFull - 200;
    } else if (entry.targetLower.find(queryLower) != std::string::npos) {
        baseScore = 1000;
    } else {
        return 0;
    }

    const int frequencyBoost = std::min(entry.launchCount, 200) * 5;
    return baseScore + frequencyBoost;
}

} // namespace

void SearchEngine::Rebuild(const CatalogDocument& catalog) {
    entries_.clear();
    std::size_t totalItems = 0;
    for (const auto& group : catalog.groups) {
        totalItems += group.items.size();
    }
    entries_.reserve(totalItems);

    for (const auto& group : catalog.groups) {
        for (const auto& item : group.items) {
            Entry e;
            e.groupId = group.id;
            e.groupName = group.name;
            e.itemId = item.id;
            e.itemName = item.name;
            e.targetPath = item.targetPath;
            e.launchCount = item.launchCount;

            e.nameLower = utf8::ToLowerAscii(item.name);
            auto py = pinyin::BuildPinyinKeys(item.name);
            e.pinyinFull = std::move(py.full);
            e.pinyinInitials = std::move(py.initials);
            e.targetLower = utf8::ToLowerAscii(item.targetPath);

            entries_.push_back(std::move(e));
        }
    }
}

std::vector<SearchMatch> SearchEngine::Search(std::string_view query,
                                              std::size_t maxResults) const {
    const std::string_view trimmed = utf8::TrimAscii(query);
    if (trimmed.empty() || maxResults == 0) {
        return {};
    }
    const std::string queryLower = utf8::ToLowerAscii(trimmed);

    std::vector<SearchMatch> matches;
    matches.reserve(std::min<std::size_t>(entries_.size(), 64));

    for (const auto& entry : entries_) {
        const int score = ScoreEntry(entry, queryLower);
        if (score > 0) {
            SearchMatch m;
            m.groupId = entry.groupId;
            m.groupName = entry.groupName;
            m.itemId = entry.itemId;
            m.itemName = entry.itemName;
            m.targetPath = entry.targetPath;
            m.launchCount = entry.launchCount;
            m.score = score;
            matches.push_back(std::move(m));
        }
    }

    std::sort(matches.begin(), matches.end(), [](const SearchMatch& a, const SearchMatch& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.launchCount != b.launchCount) {
            return a.launchCount > b.launchCount;
        }
        return CompareNaturalNames(a.itemName, b.itemName) < 0;
    });

    if (matches.size() > maxResults) {
        matches.resize(maxResults);
    }
    return matches;
}

std::vector<SearchMatch> SearchEngine::TopFrequent(std::size_t maxResults) const {
    if (maxResults == 0 || entries_.empty()) {
        return {};
    }
    std::vector<SearchMatch> out;
    out.reserve(entries_.size());
    for (const auto& entry : entries_) {
        SearchMatch m;
        m.groupId = entry.groupId;
        m.groupName = entry.groupName;
        m.itemId = entry.itemId;
        m.itemName = entry.itemName;
        m.targetPath = entry.targetPath;
        m.launchCount = entry.launchCount;
        m.score = entry.launchCount;
        out.push_back(std::move(m));
    }

    std::stable_sort(out.begin(), out.end(), [](const SearchMatch& a, const SearchMatch& b) {
        if (a.launchCount != b.launchCount) {
            return a.launchCount > b.launchCount;
        }
        return CompareNaturalNames(a.itemName, b.itemName) < 0;
    });
    if (out.size() > maxResults) {
        out.resize(maxResults);
    }
    return out;
}

} // namespace geekdesk::core
