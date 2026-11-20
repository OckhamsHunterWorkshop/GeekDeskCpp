#include "core/hotkey_parser.hpp"

#include "core/utf8.hpp"

#include <windows.h>

#include <utility>
#include <vector>

namespace geekdesk::core {

namespace {

constexpr std::pair<std::string_view, std::uint32_t> kNamedKeys[] = {
    {"space", VK_SPACE},
    {"tab", VK_TAB},
    {"enter", VK_RETURN},
    {"return", VK_RETURN},
    {"esc", VK_ESCAPE},
    {"escape", VK_ESCAPE},
    {"backspace", VK_BACK},
    {"back", VK_BACK},
    {"delete", VK_DELETE},
    {"del", VK_DELETE},
    {"insert", VK_INSERT},
    {"ins", VK_INSERT},
    {"home", VK_HOME},
    {"end", VK_END},
    {"pageup", VK_PRIOR},
    {"pgup", VK_PRIOR},
    {"prior", VK_PRIOR},
    {"pagedown", VK_NEXT},
    {"pgdn", VK_NEXT},
    {"next", VK_NEXT},
    {"up", VK_UP},
    {"down", VK_DOWN},
    {"left", VK_LEFT},
    {"right", VK_RIGHT},
    {"capslock", VK_CAPITAL},
    {"printscreen", VK_SNAPSHOT},
    {"pause", VK_PAUSE},
    {"tilde", VK_OEM_3},
    {"oemtilde", VK_OEM_3},
    {"oem3", VK_OEM_3},
    {"`", VK_OEM_3},
    {"~", VK_OEM_3},
    {"-", VK_OEM_MINUS},
    {"oemminus", VK_OEM_MINUS},
    {"=", VK_OEM_PLUS},
    {"oemplus", VK_OEM_PLUS},
    {"[", VK_OEM_4},
    {"]", VK_OEM_6},
    {"\\", VK_OEM_5},
    {";", VK_OEM_1},
    {"'", VK_OEM_7},
    {",", VK_OEM_COMMA},
    {".", VK_OEM_PERIOD},
    {"/", VK_OEM_2},
    {"plus", VK_OEM_PLUS},
    {"minus", VK_OEM_MINUS},
    {"comma", VK_OEM_COMMA},
    {"period", VK_OEM_PERIOD},
};

std::optional<std::uint32_t> ParseFunctionKey(std::string_view lowerToken) {
    if (lowerToken.size() < 2 || lowerToken.size() > 3 || lowerToken[0] != 'f') {
        return std::nullopt;
    }
    int num = 0;
    for (std::size_t i = 1; i < lowerToken.size(); ++i) {
        const char ch = lowerToken[i];
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        num = num * 10 + (ch - '0');
    }
    if (num >= 1 && num <= 24) {
        return static_cast<std::uint32_t>(VK_F1 + (num - 1));
    }
    return std::nullopt;
}

std::optional<std::uint32_t> ParseSingleKeyToken(std::string_view token) {
    const std::string lower = utf8::ToLowerAscii(token);
    if (lower.size() == 1) {
        const char ch = lower[0];
        if (ch >= 'a' && ch <= 'z') {
            return static_cast<std::uint32_t>('A' + (ch - 'a'));
        }
        if (ch >= '0' && ch <= '9') {
            return static_cast<std::uint32_t>(ch);
        }
    }
    if (auto fn = ParseFunctionKey(lower); fn.has_value()) {
        return fn;
    }
    for (const auto& [name, vk] : kNamedKeys) {
        if (lower == name) {
            return vk;
        }
    }
    return std::nullopt;
}

bool IsFunctionKeyVk(std::uint32_t vk) noexcept {
    return vk >= VK_F1 && vk <= VK_F24;
}

std::string KeyNameFromVk(std::uint32_t vk) {
    if (vk >= 'A' && vk <= 'Z') {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= '0' && vk <= '9') {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return "F" + std::to_string(1 + (vk - VK_F1));
    }
    switch (vk) {
    case VK_SPACE:
        return "Space";
    case VK_TAB:
        return "Tab";
    case VK_RETURN:
        return "Enter";
    case VK_ESCAPE:
        return "Esc";
    case VK_BACK:
        return "Backspace";
    case VK_DELETE:
        return "Delete";
    case VK_INSERT:
        return "Insert";
    case VK_HOME:
        return "Home";
    case VK_END:
        return "End";
    case VK_PRIOR:
        return "PageUp";
    case VK_NEXT:
        return "PageDown";
    case VK_UP:
        return "Up";
    case VK_DOWN:
        return "Down";
    case VK_LEFT:
        return "Left";
    case VK_RIGHT:
        return "Right";
    case VK_CAPITAL:
        return "CapsLock";
    case VK_SNAPSHOT:
        return "PrintScreen";
    case VK_PAUSE:
        return "Pause";
    case VK_OEM_3:
        return "`";
    case VK_OEM_MINUS:
        return "-";
    case VK_OEM_PLUS:
        return "=";
    case VK_OEM_4:
        return "[";
    case VK_OEM_6:
        return "]";
    case VK_OEM_5:
        return "\\";
    case VK_OEM_1:
        return ";";
    case VK_OEM_7:
        return "'";
    case VK_OEM_COMMA:
        return ",";
    case VK_OEM_PERIOD:
        return ".";
    case VK_OEM_2:
        return "/";
    default:
        return {};
    }
}

} // namespace

std::optional<HotKeySpec> ParseHotKey(std::string_view text, bool requireModifier) {
    const std::string_view trimmed = utf8::TrimAscii(text);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    std::uint32_t modifiers = 0;
    std::optional<std::uint32_t> vk;

    std::size_t start = 0;
    while (start <= trimmed.size()) {
        const std::size_t plusPos = trimmed.find('+', start);
        const std::string_view rawToken =
            (plusPos == std::string_view::npos)
                ? trimmed.substr(start)
                : trimmed.substr(start, plusPos - start);
        const std::string_view token = utf8::TrimAscii(rawToken);

        // Support trailing '+' literal key only if string ends with "++" (e.g. "Ctrl++").
        if (token.empty()) {
            if (plusPos != std::string_view::npos && plusPos + 1 == trimmed.size() && !vk.has_value()) {
                vk = static_cast<std::uint32_t>(VK_OEM_PLUS);
                break;
            }
            return std::nullopt;
        }

        const std::string lower = utf8::ToLowerAscii(token);
        if (lower == "ctrl" || lower == "control" || lower == "lctrl" || lower == "rctrl") {
            modifiers |= MOD_CONTROL;
        } else if (lower == "alt" || lower == "menu" || lower == "lalt" || lower == "ralt") {
            modifiers |= MOD_ALT;
        } else if (lower == "shift" || lower == "lshift" || lower == "rshift") {
            modifiers |= MOD_SHIFT;
        } else if (lower == "win" || lower == "windows" || lower == "super" || lower == "meta" ||
                   lower == "lwin" || lower == "rwin") {
            modifiers |= MOD_WIN;
        } else {
            if (vk.has_value()) {
                return std::nullopt; // Multiple main keys specified.
            }
            vk = ParseSingleKeyToken(token);
            if (!vk.has_value()) {
                return std::nullopt;
            }
        }

        if (plusPos == std::string_view::npos) {
            break;
        }
        start = plusPos + 1;
    }

    if (!vk.has_value()) {
        return std::nullopt;
    }
    if (requireModifier && modifiers == 0 && !IsFunctionKeyVk(*vk)) {
        return std::nullopt;
    }

    HotKeySpec spec;
    spec.modifiers = modifiers;
    spec.virtualKey = *vk;
    spec.normalized = FormatHotKey(modifiers, *vk);
    if (spec.normalized.empty()) {
        return std::nullopt;
    }
    return spec;
}

std::string FormatHotKey(std::uint32_t modifiers, std::uint32_t virtualKey) {
    const std::string keyName = KeyNameFromVk(virtualKey);
    if (keyName.empty()) {
        return {};
    }

    std::vector<std::string> parts;
    if ((modifiers & MOD_CONTROL) != 0) {
        parts.emplace_back("Ctrl");
    }
    if ((modifiers & MOD_ALT) != 0) {
        parts.emplace_back("Alt");
    }
    if ((modifiers & MOD_SHIFT) != 0) {
        parts.emplace_back("Shift");
    }
    if ((modifiers & MOD_WIN) != 0) {
        parts.emplace_back("Win");
    }
    parts.push_back(keyName);

    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out.push_back('+');
        }
        out.append(parts[i]);
    }
    return out;
}

} // namespace geekdesk::core
