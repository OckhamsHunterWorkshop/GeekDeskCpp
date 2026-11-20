#include "core/hotkey_parser.hpp"
#include "core/models.hpp"
#include "core/search_engine.hpp"
#include "core/utf8.hpp"
#include "storage/atomic_writer.hpp"
#include "storage/backup_manager.hpp"
#include "storage/json_codec.hpp"
#include "storage/logger.hpp"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

class ConsoleOutput {
public:
    ConsoleOutput() {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut == nullptr || hOut == INVALID_HANDLE_VALUE ||
            GetFileType(hOut) == FILE_TYPE_UNKNOWN) {
            if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                HANDLE hCon = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                                          OPEN_EXISTING, 0, nullptr);
                if (hCon != INVALID_HANDLE_VALUE) {
                    handle_ = hCon;
                    ownsHandle_ = true;
                    isConsole_ = (GetFileType(hCon) == FILE_TYPE_CHAR);
                }
            }
        } else {
            handle_ = hOut;
            isConsole_ = (GetFileType(hOut) == FILE_TYPE_CHAR);
        }
    }

    ~ConsoleOutput() {
        if (ownsHandle_ && handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    ConsoleOutput(const ConsoleOutput&) = delete;
    ConsoleOutput& operator=(const ConsoleOutput&) = delete;

    void WriteLine(std::string_view utf8Line) const {
        if (handle_ == nullptr || handle_ == INVALID_HANDLE_VALUE) {
            return;
        }
        std::string line(utf8Line);
        line.push_back('\n');
        if (isConsole_) {
            const std::wstring wide = geekdesk::core::utf8::Utf8ToWide(line);
            DWORD written = 0;
            WriteConsoleW(handle_, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
        } else {
            DWORD written = 0;
            WriteFile(handle_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        }
    }

private:
    HANDLE handle_ = nullptr;
    bool ownsHandle_ = false;
    bool isConsole_ = false;
};

struct ComInitGuard {
    HRESULT hr = E_FAIL;
    ComInitGuard() {
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    }
    ~ComInitGuard() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
};

std::filesystem::path GetExecutableDirectory() {
    std::wstring buf(MAX_PATH, L'\0');
    while (true) {
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            return std::filesystem::current_path();
        }
        if (len < buf.size() - 1) {
            buf.resize(len);
            return std::filesystem::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2);
    }
}

int RunVerifyData(const std::filesystem::path& dataDir, const ConsoleOutput& out) {
    // Strictly READ-ONLY verification against an existing GeekDeskNext / GeekDeskCpp data dir.
    const std::filesystem::path settingsPath = dataDir / L"settings.json";
    const std::filesystem::path catalogPath = dataDir / L"catalog.json";

    std::string settingsBytes;
    std::string readErr;
    if (!geekdesk::storage::ReadFileUtf8(settingsPath, settingsBytes, readErr)) {
        out.WriteLine("ERROR: Failed to read settings.json: " + readErr);
        return 2;
    }
    const auto settingsRes = geekdesk::storage::DeserializeSettings(settingsBytes);
    if (!settingsRes.ok) {
        out.WriteLine("ERROR: Failed to parse settings.json: " + settingsRes.error);
        return 3;
    }

    std::string catalogBytes;
    if (!geekdesk::storage::ReadFileUtf8(catalogPath, catalogBytes, readErr)) {
        out.WriteLine("ERROR: Failed to read catalog.json: " + readErr);
        return 4;
    }
    const auto catalogRes = geekdesk::storage::DeserializeCatalog(catalogBytes);
    if (!catalogRes.ok) {
        out.WriteLine("ERROR: Failed to parse catalog.json: " + catalogRes.error);
        return 5;
    }

    geekdesk::core::SearchEngine search;
    search.Rebuild(catalogRes.value);

    const auto parsedHotKey =
        geekdesk::core::ParseHotKey(settingsRes.value.activation.hotKey);

    out.WriteLine("GeekDeskCpp Schema v1 Read-Only Verification: OK");
    out.WriteLine("  Data Directory   : " + geekdesk::core::utf8::PathToUtf8(dataDir));
    out.WriteLine("  Settings Version : " + std::to_string(settingsRes.value.schemaVersion));
    out.WriteLine("  Theme / Backdrop : " +
                  std::string(geekdesk::core::ToString(settingsRes.value.appearance.theme)) +
                  " / " +
                  std::string(geekdesk::core::ToString(settingsRes.value.appearance.backdrop)));
    out.WriteLine("  HotKey           : " + settingsRes.value.activation.hotKey + " -> " +
                  (parsedHotKey.has_value() ? parsedHotKey->normalized : std::string("INVALID")));
    out.WriteLine("  Catalog Version  : " + std::to_string(catalogRes.value.schemaVersion));
    out.WriteLine("  Groups           : " + std::to_string(catalogRes.value.groups.size()));
    out.WriteLine("  Indexed Items    : " + std::to_string(search.IndexedCount()));
    for (const auto& w : settingsRes.warnings) {
        out.WriteLine("  Settings Warning : " + w);
    }
    for (const auto& w : catalogRes.warnings) {
        out.WriteLine("  Catalog Warning  : " + w);
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    ComInitGuard comGuard;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (argv != nullptr) {
        for (int i = 0; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
        LocalFree(argv);
    }

    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == L"--help" || args[i] == L"-h") {
            ConsoleOutput out;
            out.WriteLine("GeekDeskCpp Phase 1 Core & Storage CLI");
            out.WriteLine("Usage:");
            out.WriteLine("  GeekDeskCpp.exe --verify-data <data-dir>   Validate Schema v1 JSONs read-only");
            out.WriteLine("  GeekDeskCpp.exe                            Phase 1 bootstrap smoke check");
            return 0;
        }
        if (args[i] == L"--verify-data") {
            ConsoleOutput out;
            if (i + 1 >= args.size()) {
                out.WriteLine("ERROR: --verify-data requires a directory argument");
                return 1;
            }
            return RunVerifyData(std::filesystem::path(args[i + 1]), out);
        }
    }

    // Phase 1 bootstrap: initialize portable data layout, auto-recovery, daily backups, and log.
    // Phase 2 will construct the Win32 + DWM + Direct2D Software MainWindow and enter GetMessageW loop.
    const std::filesystem::path dataDir = GetExecutableDirectory() / L"data";
    std::string layoutErr;
    if (!geekdesk::storage::BackupManager::EnsureDataLayout(dataDir, layoutErr)) {
        return 10;
    }

    const auto settingsRes =
        geekdesk::storage::BackupManager::LoadSettingsWithRecovery(dataDir);
    const auto catalogRes =
        geekdesk::storage::BackupManager::LoadCatalogWithRecovery(dataDir);

    if (settingsRes.ok) {
        geekdesk::storage::BackupManager::CreateDailyBackupsIfNeeded(
            dataDir, settingsRes.value.behavior.backupRetentionDays);
    }

    geekdesk::storage::FileLogger logger(dataDir / L"logs", 14);
    logger.Info("GeekDeskCpp Phase 1 bootstrap OK (groups=" +
                std::to_string(catalogRes.value.groups.size()) + ")");
    if (settingsRes.recoveredFromBackup) {
        logger.Warn("Recovered settings.json from backup");
    }
    if (catalogRes.recoveredFromBackup) {
        logger.Warn("Recovered catalog.json from backup");
    }
    logger.Flush();

    return (settingsRes.ok && catalogRes.ok) ? 0 : 11;
}
