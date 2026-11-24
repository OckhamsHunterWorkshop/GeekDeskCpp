#include "platform/file_dialogs.hpp"

#include "core/utf8.hpp"
#include "platform/com_ptr.hpp"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdint>
#include <cstdio>
#include <sstream>

namespace geekdesk::platform {

namespace {

struct HandleGuard {
    HANDLE h = nullptr;
    ~HandleGuard() {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
    void Reset(HANDLE next = nullptr) noexcept {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
        h = next;
    }
};

struct OwnerEnableGuard {
    HWND owner = nullptr;
    bool reenable = false;
    explicit OwnerEnableGuard(HWND w) noexcept : owner(w) {
        if (owner != nullptr && IsWindow(owner) && IsWindowEnabled(owner)) {
            EnableWindow(owner, FALSE);
            reenable = true;
        }
    }
    ~OwnerEnableGuard() {
        if (reenable && owner != nullptr && IsWindow(owner)) {
            EnableWindow(owner, TRUE);
            SetForegroundWindow(owner);
        }
    }
};

std::vector<COMDLG_FILTERSPEC> ToComFilters(const std::vector<FileFilterSpec>& filters) {
    std::vector<COMDLG_FILTERSPEC> out;
    out.reserve(filters.size());
    for (const auto& f : filters) {
        out.push_back(COMDLG_FILTERSPEC{f.label.c_str(), f.pattern.c_str()});
    }
    return out;
}

std::optional<std::filesystem::path> ReadDialogResult(IFileDialog* dialog) {
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.ReleaseAndGetAddressOf())) || !item) {
        return std::nullopt;
    }
    PWSTR rawPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || rawPath == nullptr) {
        return std::nullopt;
    }
    std::filesystem::path result(rawPath);
    CoTaskMemFree(rawPath);
    return result;
}

std::optional<std::filesystem::path> PickFileDialogInProcess(
    HWND owner,
    std::wstring_view title,
    const std::vector<FileFilterSpec>& filters) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.ReleaseAndGetAddressOf())))) {
        return std::nullopt;
    }

    DWORD opts = 0;
    if (SUCCEEDED(dialog->GetOptions(&opts))) {
        dialog->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_NODEREFERENCELINKS);
    }
    if (!title.empty()) {
        std::wstring titleStr(title);
        dialog->SetTitle(titleStr.c_str());
    }
    if (!filters.empty()) {
        const auto specs = ToComFilters(filters);
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        dialog->SetFileTypeIndex(1);
    }

    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return ReadDialogResult(dialog.Get());
}

std::optional<std::filesystem::path> PickFolderDialogInProcess(HWND owner, std::wstring_view title) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.ReleaseAndGetAddressOf())))) {
        return std::nullopt;
    }

    DWORD opts = 0;
    if (SUCCEEDED(dialog->GetOptions(&opts))) {
        dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    if (!title.empty()) {
        std::wstring titleStr(title);
        dialog->SetTitle(titleStr.c_str());
    }

    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return ReadDialogResult(dialog.Get());
}

std::optional<std::filesystem::path> SaveFileDialogInProcess(
    HWND owner,
    std::wstring_view title,
    std::wstring_view defaultFileName,
    std::wstring_view defaultExtension,
    const std::vector<FileFilterSpec>& filters) {
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.ReleaseAndGetAddressOf())))) {
        return std::nullopt;
    }

    DWORD opts = 0;
    if (SUCCEEDED(dialog->GetOptions(&opts))) {
        dialog->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST);
    }
    if (!title.empty()) {
        std::wstring titleStr(title);
        dialog->SetTitle(titleStr.c_str());
    }
    if (!defaultFileName.empty()) {
        std::wstring nameStr(defaultFileName);
        dialog->SetFileName(nameStr.c_str());
    }
    if (!defaultExtension.empty()) {
        std::wstring extStr(defaultExtension);
        dialog->SetDefaultExtension(extStr.c_str());
    }
    if (!filters.empty()) {
        const auto specs = ToComFilters(filters);
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        dialog->SetFileTypeIndex(1);
    }

    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return ReadDialogResult(dialog.Get());
}

std::string SanitizeSingleLineUtf8(std::wstring_view wide) {
    std::string utf8 = core::utf8::WideToUtf8(wide);
    for (char& ch : utf8) {
        if (ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    return utf8;
}

std::string SerializeDialogRequest(std::string_view mode,
                                   HWND owner,
                                   std::wstring_view title,
                                   std::wstring_view defaultFileName,
                                   std::wstring_view defaultExtension,
                                   const std::vector<FileFilterSpec>& filters) {
    std::string out;
    out.append(mode);
    out.push_back('\n');
    const auto ownerVal =
        (owner != nullptr && IsWindow(owner))
            ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(owner))
            : 0ULL;
    out.append(std::to_string(ownerVal));
    out.push_back('\n');
    out.append(SanitizeSingleLineUtf8(title));
    out.push_back('\n');
    out.append(SanitizeSingleLineUtf8(defaultFileName));
    out.push_back('\n');
    out.append(SanitizeSingleLineUtf8(defaultExtension));
    out.push_back('\n');
    out.append(std::to_string(filters.size()));
    out.push_back('\n');
    for (const auto& f : filters) {
        out.append(SanitizeSingleLineUtf8(f.label));
        out.push_back('\n');
        out.append(SanitizeSingleLineUtf8(f.pattern));
        out.push_back('\n');
    }
    return out;
}

bool WriteAllToHandle(HANDLE h, std::string_view bytes) {
    std::size_t writtenTotal = 0;
    while (writtenTotal < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(bytes.size() - writtenTotal, 65536));
        DWORD writtenNow = 0;
        if (!WriteFile(h, bytes.data() + writtenTotal, chunk, &writtenNow, nullptr) ||
            writtenNow == 0) {
            return false;
        }
        writtenTotal += writtenNow;
    }
    return true;
}

std::string ReadAllFromHandle(HANDLE h) {
    std::string out;
    char buf[4096];
    for (;;) {
        DWORD readNow = 0;
        if (!ReadFile(h, buf, sizeof(buf), &readNow, nullptr) || readNow == 0) {
            break;
        }
        out.append(buf, readNow);
        if (out.size() > 1024 * 1024) {
            break;
        }
    }
    return out;
}

std::vector<std::string> SplitLinesPreserveEmpty(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            std::string_view part = text.substr(start);
            if (!part.empty() && part.back() == '\r') {
                part.remove_suffix(1);
            }
            lines.emplace_back(part);
            break;
        }
        std::string_view part = text.substr(start, nl - start);
        if (!part.empty() && part.back() == '\r') {
            part.remove_suffix(1);
        }
        lines.emplace_back(part);
        start = nl + 1;
    }
    return lines;
}

std::filesystem::path GetCurrentExePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            return {};
        }
        if (len < buf.size() - 1) {
            buf.resize(len);
            return std::filesystem::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
}

enum class HelperOutcome {
    Selected,
    Canceled,
    Failed,
};

struct HelperDialogResult {
    HelperOutcome outcome = HelperOutcome::Failed;
    std::filesystem::path path;
};

HelperDialogResult RunDialogInHelperProcess(std::string_view mode,
                                            HWND owner,
                                            std::wstring_view title,
                                            std::wstring_view defaultFileName,
                                            std::wstring_view defaultExtension,
                                            const std::vector<FileFilterSpec>& filters) {
    HelperDialogResult result;
    const std::filesystem::path exePath = GetCurrentExePath();
    if (exePath.empty()) {
        return result;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HandleGuard stdinRead;
    HandleGuard stdinWrite;
    HandleGuard stdoutRead;
    HandleGuard stdoutWrite;

    if (!CreatePipe(&stdinRead.h, &stdinWrite.h, &sa, 0) ||
        !CreatePipe(&stdoutRead.h, &stdoutWrite.h, &sa, 65536)) {
        return result;
    }
    if (!SetHandleInformation(stdinWrite.h, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(stdoutRead.h, HANDLE_FLAG_INHERIT, 0)) {
        return result;
    }

    std::wstring cmdLine = L"\"" + exePath.wstring() + L"\" --dialog-helper";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinRead.h;
    si.hStdOutput = stdoutWrite.h;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exePath.c_str(), cmdLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        return result;
    }

    HandleGuard procGuard{pi.hProcess};
    HandleGuard threadGuard{pi.hThread};
    stdinRead.Reset();
    stdoutWrite.Reset();

    AllowSetForegroundWindow(pi.dwProcessId);

    const std::string payload = SerializeDialogRequest(mode, owner, title, defaultFileName,
                                                       defaultExtension, filters);
    const bool wrote = WriteAllToHandle(stdinWrite.h, payload);
    stdinWrite.Reset();
    if (!wrote) {
        TerminateProcess(procGuard.h, 1);
        return result;
    }

    {
        OwnerEnableGuard ownerGuard(owner);
        bool quitReceived = false;
        WPARAM quitCode = 0;
        for (;;) {
            const DWORD wait =
                MsgWaitForMultipleObjects(1, &procGuard.h, FALSE, INFINITE, QS_ALLINPUT);
            if (wait == WAIT_OBJECT_0) {
                break;
            }
            if (wait == WAIT_OBJECT_0 + 1) {
                MSG msg{};
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) {
                        quitReceived = true;
                        quitCode = msg.wParam;
                        break;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                if (quitReceived) {
                    break;
                }
            } else {
                break;
            }
        }
        if (quitReceived) {
            PostQuitMessage(static_cast<int>(quitCode));
        }
    }

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(procGuard.h, &exitCode) || exitCode != 0) {
        return result;
    }

    const std::string reply = ReadAllFromHandle(stdoutRead.h);
    const auto lines = SplitLinesPreserveEmpty(reply);
    if (lines.empty()) {
        return result;
    }
    if (lines[0] == "CANCEL") {
        result.outcome = HelperOutcome::Canceled;
        return result;
    }
    if (lines[0] == "OK" && lines.size() >= 2 && !lines[1].empty()) {
        result.outcome = HelperOutcome::Selected;
        result.path = core::utf8::PathFromUtf8(lines[1]);
        return result;
    }
    return result;
}

} // namespace

std::optional<std::filesystem::path> PickFileDialog(HWND owner,
                                                    std::wstring_view title,
                                                    const std::vector<FileFilterSpec>& filters) {
    const HelperDialogResult helper =
        RunDialogInHelperProcess("pick-file", owner, title, L"", L"", filters);
    if (helper.outcome == HelperOutcome::Selected) {
        return helper.path;
    }
    if (helper.outcome == HelperOutcome::Canceled) {
        return std::nullopt;
    }
    return PickFileDialogInProcess(owner, title, filters);
}

std::optional<std::filesystem::path> PickFolderDialog(HWND owner, std::wstring_view title) {
    const HelperDialogResult helper =
        RunDialogInHelperProcess("pick-folder", owner, title, L"", L"", {});
    if (helper.outcome == HelperOutcome::Selected) {
        return helper.path;
    }
    if (helper.outcome == HelperOutcome::Canceled) {
        return std::nullopt;
    }
    return PickFolderDialogInProcess(owner, title);
}

std::optional<std::filesystem::path> SaveFileDialog(HWND owner,
                                                    std::wstring_view title,
                                                    std::wstring_view defaultFileName,
                                                    std::wstring_view defaultExtension,
                                                    const std::vector<FileFilterSpec>& filters) {
    const HelperDialogResult helper = RunDialogInHelperProcess(
        "save-file", owner, title, defaultFileName, defaultExtension, filters);
    if (helper.outcome == HelperOutcome::Selected) {
        return helper.path;
    }
    if (helper.outcome == HelperOutcome::Canceled) {
        return std::nullopt;
    }
    return SaveFileDialogInProcess(owner, title, defaultFileName, defaultExtension, filters);
}

int RunDialogHelperProcess() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hIn == nullptr || hIn == INVALID_HANDLE_VALUE || hOut == nullptr ||
        hOut == INVALID_HANDLE_VALUE) {
        return 1;
    }

    const std::string request = ReadAllFromHandle(hIn);
    const auto lines = SplitLinesPreserveEmpty(request);
    if (lines.size() < 6) {
        return 1;
    }

    const std::string& mode = lines[0];
    HWND owner = nullptr;
    try {
        const std::uint64_t rawHwnd = std::stoull(lines[1]);
        if (rawHwnd != 0) {
            HWND candidate = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(rawHwnd));
            if (IsWindow(candidate)) {
                owner = candidate;
            }
        }
    } catch (...) {
        owner = nullptr;
    }

    const std::wstring title = core::utf8::Utf8ToWide(lines[2]);
    const std::wstring defName = core::utf8::Utf8ToWide(lines[3]);
    const std::wstring defExt = core::utf8::Utf8ToWide(lines[4]);
    std::size_t filterCount = 0;
    try {
        filterCount = static_cast<std::size_t>(std::stoull(lines[5]));
    } catch (...) {
        return 1;
    }
    if (lines.size() < 6 + filterCount * 2) {
        return 1;
    }

    std::vector<FileFilterSpec> filters;
    filters.reserve(filterCount);
    for (std::size_t i = 0; i < filterCount; ++i) {
        filters.push_back(FileFilterSpec{core::utf8::Utf8ToWide(lines[6 + i * 2]),
                                         core::utf8::Utf8ToWide(lines[6 + i * 2 + 1])});
    }

    std::optional<std::filesystem::path> chosen;
    if (mode == "pick-file") {
        chosen = PickFileDialogInProcess(owner, title, filters);
    } else if (mode == "pick-folder") {
        chosen = PickFolderDialogInProcess(owner, title);
    } else if (mode == "save-file") {
        chosen = SaveFileDialogInProcess(owner, title, defName, defExt, filters);
    } else {
        return 1;
    }

    const std::string reply =
        chosen.has_value() ? ("OK\n" + core::utf8::PathToUtf8(*chosen) + "\n") : "CANCEL\n";
    (void)WriteAllToHandle(hOut, reply);
    return 0;
}

namespace {

bool SelectPathWithShellApi(const std::filesystem::path& normalized) {
    PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(normalized.c_str());
    if (itemPidl == nullptr) {
        return false;
    }

    HRESULT hr = E_FAIL;
    PIDLIST_ABSOLUTE parentPidl = ILClone(itemPidl);
    if (parentPidl != nullptr) {
        if (ILRemoveLastID(parentPidl)) {
            PCUITEMID_CHILD childPidl = ILFindLastID(itemPidl);
            hr = SHOpenFolderAndSelectItems(parentPidl, 1, &childPidl, 0);
        }
        ILFree(parentPidl);
    }
    if (FAILED(hr)) {
        hr = SHOpenFolderAndSelectItems(itemPidl, 0, nullptr, 0);
    }
    ILFree(itemPidl);
    return SUCCEEDED(hr);
}

} // namespace

bool RevealInExplorer(const std::filesystem::path& path) {
    const std::filesystem::path normalized =
        core::utf8::NormalizeWindowsPathInput(path.wstring());
    if (normalized.empty()) {
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(normalized, ec)) {
        if (SelectPathWithShellApi(normalized)) {
            return true;
        }
        const std::wstring params = L"/select,\"" + normalized.wstring() + L"\"";
        const HINSTANCE h =
            ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(h) > 32;
    }

    const std::filesystem::path parent = normalized.parent_path();
    if (!parent.empty() && std::filesystem::exists(parent, ec)) {
        const HINSTANCE h =
            ShellExecuteW(nullptr, L"open", parent.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(h) > 32;
    }
    return false;
}

} // namespace geekdesk::platform
