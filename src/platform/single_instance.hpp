#pragma once

#include <windows.h>

namespace geekdesk::platform {

/// Single-instance mutex guard with registered broadcast message to summon the running instance.
class SingleInstanceGuard {
public:
    SingleInstanceGuard() = default;
    ~SingleInstanceGuard();

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    /// Acquire the instance mutex. Returns true if this process is the primary instance.
    bool TryAcquire();

    /// Broadcast an activation message to the existing primary instance.
    void NotifyExistingInstance() const;

    [[nodiscard]] UINT ActivateMessage() const noexcept {
        return activateMsg_;
    }

private:
    HANDLE mutex_ = nullptr;
    UINT activateMsg_ = 0;
    bool isPrimary_ = false;
};

} // namespace geekdesk::platform
