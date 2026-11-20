#include "platform/hotkey_manager.hpp"

#include "core/hotkey_parser.hpp"
#include "core/utf8.hpp"

namespace geekdesk::platform {

HotKeyManager::~HotKeyManager() {
    Unregister();
}

bool HotKeyManager::Register(HWND targetHwnd,
                             std::string_view hotKeyText,
                             std::string& outError,
                             int id) {
    Unregister();
    outError.clear();

    if (targetHwnd == nullptr) {
        outError = "HotKey target HWND is null";
        return false;
    }

    const auto spec = core::ParseHotKey(hotKeyText);
    if (!spec.has_value()) {
        outError = "Invalid hotkey specification: " + std::string(hotKeyText);
        return false;
    }

    const UINT mods = static_cast<UINT>(spec->modifiers) | MOD_NOREPEAT;
    if (!RegisterHotKey(targetHwnd, id, mods, static_cast<UINT>(spec->virtualKey))) {
        const DWORD err = GetLastError();
        outError = "RegisterHotKey failed for '" + spec->normalized + "' (Win32 error " +
                   std::to_string(err) + ")";
        return false;
    }

    targetHwnd_ = targetHwnd;
    activeId_ = id;
    registered_ = true;
    activeText_ = spec->normalized;
    return true;
}

void HotKeyManager::Unregister() {
    if (registered_ && targetHwnd_ != nullptr) {
        UnregisterHotKey(targetHwnd_, activeId_);
    }
    registered_ = false;
    targetHwnd_ = nullptr;
    activeText_.clear();
}

} // namespace geekdesk::platform
