#include "platform/single_instance.hpp"

namespace geekdesk::platform {

SingleInstanceGuard::~SingleInstanceGuard() {
    if (mutex_ != nullptr) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

bool SingleInstanceGuard::TryAcquire() {
    if (activateMsg_ == 0) {
        activateMsg_ = RegisterWindowMessageW(L"GeekDeskCpp_ActivateExistingInstance");
    }
    mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\GeekDeskCpp_SingleInstance_Mutex");
    if (mutex_ == nullptr) {
        return true;
    }
    isPrimary_ = (GetLastError() != ERROR_ALREADY_EXISTS);
    return isPrimary_;
}

void SingleInstanceGuard::NotifyExistingInstance() const {
    if (activateMsg_ != 0) {
        SendNotifyMessageW(HWND_BROADCAST, activateMsg_, 0, 0);
    }
}

} // namespace geekdesk::platform
