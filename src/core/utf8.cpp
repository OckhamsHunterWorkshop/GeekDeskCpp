#include "core/utf8.hpp"

#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <cstdio>
#include <limits>

namespace geekdesk::core::utf8 {

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    if (utf8.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return {};
    }
    const int srcLen = static_cast<int>(utf8.size());
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), srcLen, nullptr, 0);
    if (wideLen <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), srcLen, result.data(), wideLen);
    return result;
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    if (wide.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return {};
    }
    const int srcLen = static_cast<int>(wide.size());
    const int utf8Len =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), srcLen, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(utf8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), srcLen, result.data(), utf8Len, nullptr, nullptr);
    return result;
}

std::filesystem::path PathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(Utf8ToWide(utf8));
}

std::string PathToUtf8(const std::filesystem::path& path) {
    return WideToUtf8(path.wstring());
}

std::string ToLowerAscii(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

std::string_view TrimAscii(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
            text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
            text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

char32_t NextUtf8Codepoint(std::string_view text, std::size_t& offset) {
    if (offset >= text.size()) {
        return 0;
    }
    const auto b0 = static_cast<std::uint8_t>(text[offset]);
    if (b0 < 0x80) {
        ++offset;
        return static_cast<char32_t>(b0);
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        if (offset + 1 < text.size()) {
            const auto b1 = static_cast<std::uint8_t>(text[offset + 1]);
            if ((b1 & 0xC0u) == 0x80u) {
                const char32_t cp =
                    ((static_cast<char32_t>(b0 & 0x1Fu)) << 6) | static_cast<char32_t>(b1 & 0x3Fu);
                if (cp >= 0x80u) {
                    offset += 2;
                    return cp;
                }
            }
        }
        ++offset;
        return 0xFFFDu;
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        if (offset + 2 < text.size()) {
            const auto b1 = static_cast<std::uint8_t>(text[offset + 1]);
            const auto b2 = static_cast<std::uint8_t>(text[offset + 2]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
                const char32_t cp = ((static_cast<char32_t>(b0 & 0x0Fu)) << 12) |
                                    ((static_cast<char32_t>(b1 & 0x3Fu)) << 6) |
                                    static_cast<char32_t>(b2 & 0x3Fu);
                if (cp >= 0x800u && !(cp >= 0xD800u && cp <= 0xDFFFu)) {
                    offset += 3;
                    return cp;
                }
            }
        }
        ++offset;
        return 0xFFFDu;
    }
    if ((b0 & 0xF8u) == 0xF0u) {
        if (offset + 3 < text.size()) {
            const auto b1 = static_cast<std::uint8_t>(text[offset + 1]);
            const auto b2 = static_cast<std::uint8_t>(text[offset + 2]);
            const auto b3 = static_cast<std::uint8_t>(text[offset + 3]);
            if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u && (b3 & 0xC0u) == 0x80u) {
                const char32_t cp = ((static_cast<char32_t>(b0 & 0x07u)) << 18) |
                                    ((static_cast<char32_t>(b1 & 0x3Fu)) << 12) |
                                    ((static_cast<char32_t>(b2 & 0x3Fu)) << 6) |
                                    static_cast<char32_t>(b3 & 0x3Fu);
                if (cp >= 0x10000u && cp <= 0x10FFFFu) {
                    offset += 4;
                    return cp;
                }
            }
        }
        ++offset;
        return 0xFFFDu;
    }
    ++offset;
    return 0xFFFDu;
}

std::string_view StripUtf8Bom(std::string_view text) {
    if (text.size() >= 3 && static_cast<std::uint8_t>(text[0]) == 0xEF &&
        static_cast<std::uint8_t>(text[1]) == 0xBB &&
        static_cast<std::uint8_t>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    return text;
}

std::string GenerateUuidV4() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return "00000000-0000-4000-8000-000000000000";
    }
    char buf[40]{};
    std::snprintf(buf, sizeof(buf),
                  "%08lx-%04hx-%04hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
                  static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0],
                  guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
                  guid.Data4[6], guid.Data4[7]);
    return std::string(buf);
}

} // namespace geekdesk::core::utf8
