#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace geekdesk::core::utf8 {

/// Convert a UTF-8 encoded string to a UTF-16 std::wstring using Windows NLS APIs.
[[nodiscard]] std::wstring Utf8ToWide(std::string_view utf8);

/// Convert a UTF-16 std::wstring_view to a UTF-8 std::string using Windows NLS APIs.
[[nodiscard]] std::string WideToUtf8(std::wstring_view wide);

/// Convert a UTF-8 path string to std::filesystem::path without ACP corruption.
[[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view utf8);

/// Convert std::filesystem::path to a UTF-8 string without ACP corruption.
[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);

/// Convert ASCII uppercase characters in a string to lowercase, preserving non-ASCII bytes.
[[nodiscard]] std::string ToLowerAscii(std::string_view text);

/// Trim leading and trailing ASCII whitespace.
[[nodiscard]] std::string_view TrimAscii(std::string_view text);

/// Decode the next Unicode scalar value from a UTF-8 string and advance offset.
/// On malformed bytes, advances by 1 byte and returns U+FFFD.
[[nodiscard]] char32_t NextUtf8Codepoint(std::string_view text, std::size_t& offset);

/// Strip a UTF-8 BOM (EF BB BF) prefix if present.
[[nodiscard]] std::string_view StripUtf8Bom(std::string_view text);

/// Generate a lowercase RFC 4122 UUID string via CoCreateGuid.
[[nodiscard]] std::string GenerateUuidV4();

} // namespace geekdesk::core::utf8
