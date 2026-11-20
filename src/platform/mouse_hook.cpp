#include "platform/mouse_hook.hpp"

#include <atomic>

namespace geekdesk::platform {

namespace {

std::atomic<HWND> gTargetHwnd{nullptr};
std::atomic<UINT> gCallbackMsg{0};

} // namespace

MouseHook::~MouseHook() {
    Stop();
}

bool MouseHook::Start(HWND targetHwnd, UINT callbackMsg) {
    if (targetHwnd == nullptr || callbackMsg == 0) {
        return false;
    }
    if (hook_ != nullptr && targetHwnd_ == targetHwnd && callbackMsg_ == callbackMsg) {
        return true;
    }
    Stop();

    targetHwnd_ = targetHwnd;
    callbackMsg_ = callbackMsg;
    gTargetHwnd.store(targetHwnd, std::memory_order_release);
    gCallbackMsg.store(callbackMsg, std::memory_order_release);

    hook_ = SetWindowsHookExW(WH_MOUSE_LL, &MouseHook::LowLevelMouseProc,
                              GetModuleHandleW(nullptr), 0);
    if (hook_ == nullptr) {
        gTargetHwnd.store(nullptr, std::memory_order_release);
        gCallbackMsg.store(0, std::memory_order_release);
        targetHwnd_ = nullptr;
        callbackMsg_ = 0;
        return false;
    }
    return true;
}

void MouseHook::Stop() {
    if (hook_ != nullptr) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    gTargetHwnd.store(nullptr, std::memory_order_release);
    gCallbackMsg.store(0, std::memory_order_release);
}

bool MouseHook::Restart() {
    const HWND hwnd = targetHwnd_;
    const UINT msg = callbackMsg_;
    Stop();
    if (hwnd == nullptr || msg == 0) {
        return false;
    }
    return Start(hwnd, msg);
}

LRESULT CALLBACK MouseHook::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_MBUTTONDOWN) {
        const HWND hwnd = gTargetHwnd.load(std::memory_order_acquire);
        const UINT msg = gCallbackMsg.load(std::memory_order_acquire);
        if (hwnd != nullptr && msg != 0) {
            PostMessageW(hwnd, msg, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

} // namespace geekdesk::platform
