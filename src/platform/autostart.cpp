#include "platform/autostart.hpp"

#include <windows.h>

#include <string>

namespace geekdesk::platform {

namespace {

constexpr const wchar_t* kRunSubKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kValueName = L"GeekDeskCpp";

std::wstring GetCurrentExecutablePath() {
    std::wstring buf(MAX_PATH, L'\0');
    while (true) {
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            return {};
        }
        if (len < buf.size() - 1) {
            buf.resize(len);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

} // namespace

bool IsAutoStartEnabled() {
    DWORD type = 0;
    DWORD bytes = 0;
    const LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, kRunSubKey, kValueName, RRF_RT_REG_SZ, &type,
                                    nullptr, &bytes);
    return st == ERROR_SUCCESS && bytes > sizeof(wchar_t);
}

bool SetAutoStartEnabled(bool enabled) {
    HKEY hKey = nullptr;
    const LSTATUS openSt = RegCreateKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, nullptr, 0,
                                           KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (openSt != ERROR_SUCCESS || hKey == nullptr) {
        return false;
    }

    bool ok = false;
    if (enabled) {
        const std::wstring exePath = GetCurrentExecutablePath();
        if (!exePath.empty()) {
            const std::wstring cmdLine = L"\"" + exePath + L"\"";
            const DWORD byteSize = static_cast<DWORD>((cmdLine.size() + 1) * sizeof(wchar_t));
            ok = (RegSetValueExW(hKey, kValueName, 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(cmdLine.c_str()),
                                 byteSize) == ERROR_SUCCESS);
        }
    } else {
        const LSTATUS delSt = RegDeleteValueW(hKey, kValueName);
        ok = (delSt == ERROR_SUCCESS || delSt == ERROR_FILE_NOT_FOUND);
    }

    RegCloseKey(hKey);
    return ok;
}

} // namespace geekdesk::platform
