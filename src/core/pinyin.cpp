#include "core/pinyin.hpp"

#include "core/pinyin_table.hpp"
#include "core/utf8.hpp"

namespace geekdesk::core::pinyin {

namespace {

bool IsAsciiAlpha(char32_t cp) noexcept {
    return (cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z');
}

bool IsAsciiUpper(char32_t cp) noexcept {
    return cp >= U'A' && cp <= U'Z';
}

bool IsAsciiDigit(char32_t cp) noexcept {
    return cp >= U'0' && cp <= U'9';
}

char ToLowerAsciiChar(char32_t cp) noexcept {
    if (cp >= U'A' && cp <= U'Z') {
        return static_cast<char>(cp - U'A' + U'a');
    }
    return static_cast<char>(cp);
}

} // namespace

std::string_view LookupSyllable(char32_t codepoint) noexcept {
    if (codepoint < detail::kPinyinRangeStart || codepoint > detail::kPinyinRangeEnd) {
        return {};
    }
    const auto offset = static_cast<std::size_t>(codepoint - detail::kPinyinRangeStart);
    const std::uint16_t syllableIdx = detail::kCodepointToSyllable[offset];
    if (syllableIdx >= detail::kPinyinSyllables.size()) {
        return {};
    }
    return detail::kPinyinSyllables[syllableIdx];
}

PinyinKeys BuildPinyinKeys(std::string_view utf8Text) {
    PinyinKeys result;
    result.full.reserve(utf8Text.size() * 2);
    result.initials.reserve(utf8Text.size());

    enum class PrevKind { None, NonAlnum, LowerOrDigit, Upper, Cjk };
    PrevKind prev = PrevKind::None;

    std::size_t pos = 0;
    while (pos < utf8Text.size()) {
        const char32_t cp = utf8::NextUtf8Codepoint(utf8Text, pos);
        if (cp >= detail::kPinyinRangeStart && cp <= detail::kPinyinRangeEnd) {
            const std::string_view syl = LookupSyllable(cp);
            if (!syl.empty()) {
                result.full.append(syl);
                result.initials.push_back(syl.front());
            }
            prev = PrevKind::Cjk;
            continue;
        }

        if (IsAsciiAlpha(cp)) {
            const char lower = ToLowerAsciiChar(cp);
            result.full.push_back(lower);

            const bool isUpper = IsAsciiUpper(cp);
            const bool isWordStart =
                (prev == PrevKind::None || prev == PrevKind::NonAlnum || prev == PrevKind::Cjk ||
                 (isUpper && prev == PrevKind::LowerOrDigit));
            if (isWordStart) {
                result.initials.push_back(lower);
            }
            prev = isUpper ? PrevKind::Upper : PrevKind::LowerOrDigit;
            continue;
        }

        if (IsAsciiDigit(cp)) {
            const char ch = static_cast<char>(cp);
            result.full.push_back(ch);
            if (prev == PrevKind::None || prev == PrevKind::NonAlnum || prev == PrevKind::Cjk) {
                result.initials.push_back(ch);
            }
            prev = PrevKind::LowerOrDigit;
            continue;
        }

        prev = PrevKind::NonAlnum;
    }

    return result;
}

} // namespace geekdesk::core::pinyin
