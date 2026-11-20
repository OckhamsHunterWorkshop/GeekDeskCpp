#pragma once

namespace geekdesk::platform {

/// Check whether GeekDeskCpp auto-start entry exists in HKCU Run registry key.
[[nodiscard]] bool IsAutoStartEnabled();

/// Enable or disable GeekDeskCpp startup via HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
bool SetAutoStartEnabled(bool enabled);

} // namespace geekdesk::platform
