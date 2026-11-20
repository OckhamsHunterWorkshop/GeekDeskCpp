#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace geekdesk::platform {

class HotKeyManager {
public:
    static constexpr int kDefaultHotKeyId = 1;

    HotKeyManager() = default;
    ~HotKeyManager();

    HotKeyManager(const HotKeyManager&) = delete;
    HotKeyManager& operator=(const HotKeyManager&) = delete;

    /// Parse and register the global hotkey against targetHwnd. Unregisters any previous one.
    bool Register(HWND targetHwnd,
                  std::string_view hotKeyText,
                  std::string& outError,
                  int id = kDefaultHotKeyId);

    void Unregister();

    [[nodiscard]] bool IsRegistered() const noexcept {
        return registered_;
    }

    [[nodiscard]] const std::string& ActiveNormalizedText() const noexcept {
        return activeText_;
    }

private:
    HWND targetHwnd_ = nullptr;
    int activeId_ = kDefaultHotKeyId;
    bool registered_ = false;
    std::string activeText_;
};

} // namespace geekdesk::platform
