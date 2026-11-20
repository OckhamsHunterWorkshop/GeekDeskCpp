#pragma once

#include <windows.h>

namespace geekdesk::platform {

/// Low-level global WH_MOUSE_LL listener that posts callbackMsg to targetHwnd
/// when the middle mouse button is pressed without blocking Windows input dispatch.
class MouseHook {
public:
    MouseHook() = default;
    ~MouseHook();

    MouseHook(const MouseHook&) = delete;
    MouseHook& operator=(const MouseHook&) = delete;

    bool Start(HWND targetHwnd, UINT callbackMsg);
    void Stop();
    bool Restart();

    [[nodiscard]] bool IsRunning() const noexcept {
        return hook_ != nullptr;
    }

private:
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    HHOOK hook_ = nullptr;
    HWND targetHwnd_ = nullptr;
    UINT callbackMsg_ = 0;
};

} // namespace geekdesk::platform
