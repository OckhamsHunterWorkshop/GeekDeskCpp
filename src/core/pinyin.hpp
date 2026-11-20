#pragma once

#include <string>
#include <string_view>

namespace geekdesk::core::pinyin {

struct PinyinKeys {
    /// Concatenated lowercase pinyin syllables + lowercase ASCII alphanumerics (e.g. "yuanbaoai").
    std::string full;
    /// Initials of each CJK character and each ASCII/CamelCase word token (e.g. "ybai", "vsc").
    std::string initials;
};

/// Look up the offline pinyin syllable for a Unicode codepoint in U+4E00..U+9FA5.
/// Returns an empty string_view for non-CJK or unmapped codepoints.
[[nodiscard]] std::string_view LookupSyllable(char32_t codepoint) noexcept;

/// Build full-pinyin and initials search keys for a UTF-8 display string.
[[nodiscard]] PinyinKeys BuildPinyinKeys(std::string_view utf8Text);

} // namespace geekdesk::core::pinyin
