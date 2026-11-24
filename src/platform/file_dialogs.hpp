#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace geekdesk::platform {

struct FileFilterSpec {
    std::wstring label;
    std::wstring pattern;
};

/// Show modern Windows COM Open File dialog (IFileOpenDialog).
[[nodiscard]] std::optional<std::filesystem::path> PickFileDialog(
    HWND owner,
    std::wstring_view title,
    const std::vector<FileFilterSpec>& filters = {});

/// Show modern Windows COM Folder picker dialog (IFileOpenDialog with FOS_PICKFOLDERS).
[[nodiscard]] std::optional<std::filesystem::path> PickFolderDialog(HWND owner,
                                                                    std::wstring_view title);

/// Show modern Windows COM Save File dialog (IFileSaveDialog).
[[nodiscard]] std::optional<std::filesystem::path> SaveFileDialog(
    HWND owner,
    std::wstring_view title,
    std::wstring_view defaultFileName,
    std::wstring_view defaultExtension,
    const std::vector<FileFilterSpec>& filters = {});

/// Open Windows Explorer selecting the given file or folder.
bool RevealInExplorer(const std::filesystem::path& path);

/// Entry point for `--dialog-helper` isolated child process that hosts Windows COM file/folder
/// dialogs via stdin/stdout pipes so shell extensions stay out of the main launcher process.
[[nodiscard]] int RunDialogHelperProcess();

} // namespace geekdesk::platform
