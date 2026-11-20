#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace geekdesk::core {

struct HotKeySpec {
    /// Bitmask of MOD_ALT (0x1), MOD_CONTROL (0x2), MOD_SHIFT (0x4), MOD_WIN (0x8).
    std::uint32_t modifiers = 0;
    /// Windows virtual-key code (VK_*).
    std::uint32_t virtualKey = 0;
    /// Canonical string representation such as "Ctrl+Q" or "Ctrl+Shift+Space".
    std::string normalized;

    bool operator==(const HotKeySpec& other) const noexcept = default;
};

/// Parse a hotkey string (e.g. "Ctrl+Q", "Ctrl + Alt + Space", "F9") into Win32 codes.
/// When requireModifier is true, single non-function keys without modifiers are rejected.
[[nodiscard]] std::optional<HotKeySpec> ParseHotKey(std::string_view text,
                                                    bool requireModifier = true);

/// Format modifiers and virtual-key code back into canonical text.
[[nodiscard]] std::string FormatHotKey(std::uint32_t modifiers, std::uint32_t virtualKey);

} // namespace geekdesk::core
