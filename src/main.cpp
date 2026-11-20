#include "core/hotkey_parser.hpp"
#include "core/models.hpp"
#include "core/search_engine.hpp"
#include "core/utf8.hpp"
#include "platform/single_instance.hpp"
#include "storage/atomic_writer.hpp"
#include "storage/backup_manager.hpp"
#include "storage/json_codec.hpp"
#include "storage/logger.hpp"
#include "ui/main_window.hpp"

#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <shellapi.h>

#include <cstdio>
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

std::string FormatMegabytes(SIZE_T bytes) {
    char buf[32]{};
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::snprintf(buf, sizeof(buf), "%.2f MB", mb);
    return std::string(buf);
}

int RunVerifyData(const std::filesystem::path& dataDir, const ConsoleOutput& out) {
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

    const auto parsedHotKey = geekdesk::core::ParseHotKey(settingsRes.value.activation.hotKey);

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

int RunSmokeUi(HINSTANCE hInstance,
               const std::filesystem::path& dataDir,
               const ConsoleOutput& out) {
    std::string layoutErr;
    if (!geekdesk::storage::BackupManager::EnsureDataLayout(dataDir, layoutErr)) {
        out.WriteLine("ERROR: EnsureDataLayout failed: " + layoutErr);
        return 10;
    }

    const auto settingsRes = geekdesk::storage::BackupManager::LoadSettingsWithRecovery(dataDir);
    const auto catalogRes = geekdesk::storage::BackupManager::LoadCatalogWithRecovery(dataDir);
    if (!settingsRes.ok || !catalogRes.ok) {
        out.WriteLine("ERROR: Failed to load settings or catalog for smoke UI");
        return 11;
    }

    geekdesk::storage::FileLogger logger(dataDir / L"logs", 14);
    geekdesk::ui::MainWindow window(dataDir, settingsRes.value, catalogRes.value, &logger, 0);
    if (!window.Create(hInstance, false)) {
        out.WriteLine("ERROR: MainWindow::Create failed");
        return 12;
    }

    const bool rendered = window.RenderSmokeFrame();
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                         sizeof(pmc));

    bool warpLoaded = false;
    bool gpuUmdLoaded = false;
    bool everythingLoaded = false;
    std::vector<HMODULE> modules(256);
    DWORD bytesNeeded = 0;
    if (EnumProcessModules(GetCurrentProcess(), modules.data(),
                           static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &bytesNeeded)) {
        const std::size_t count =
            std::min<std::size_t>(modules.size(), bytesNeeded / sizeof(HMODULE));
        for (std::size_t idx = 0; idx < count; ++idx) {
            wchar_t baseName[MAX_PATH]{};
            const DWORD len =
                GetModuleBaseNameW(GetCurrentProcess(), modules[idx], baseName, MAX_PATH);
            if (len == 0) {
                continue;
            }
            const std::string lower = geekdesk::core::utf8::ToLowerAscii(
                geekdesk::core::utf8::WideToUtf8(std::wstring_view(baseName, len)));
            if (lower == "d3d10warp.dll") {
                warpLoaded = true;
            }
            if (lower.find("everything") != std::string::npos) {
                everythingLoaded = true;
            }
            if (lower.starts_with("nvwgf") || lower.starts_with("nvldumd") ||
                lower.starts_with("nvoglv") || lower.starts_with("amdxc") ||
                lower.starts_with("aticfx") || lower.starts_with("atiumd") ||
                lower.starts_with("igd10") || lower.starts_with("igd12") ||
                lower.starts_with("igc64") || lower.starts_with("igdgmm") ||
                lower.starts_with("qcdx")) {
                gpuUmdLoaded = true;
            }
        }
    }

    const std::size_t displayed = window.DisplayedItemCount();
    const bool underMemoryBudget = (pmc.PrivateUsage < (25ULL * 1024ULL * 1024ULL));

    window.Destroy();
    logger.Flush();

    out.WriteLine("GeekDeskCpp Phase 2 Mode A UI Smoke Test: " +
                  std::string(rendered ? "OK" : "FAILED"));
    out.WriteLine("  Data Directory    : " + geekdesk::core::utf8::PathToUtf8(dataDir));
    out.WriteLine("  Groups / Shown    : " + std::to_string(catalogRes.value.groups.size()) +
                  " / " + std::to_string(displayed));
    out.WriteLine("  Working Set       : " + FormatMegabytes(pmc.WorkingSetSize));
    out.WriteLine("  Private Bytes     : " + FormatMegabytes(pmc.PrivateUsage));
    out.WriteLine("  WARP Loaded       : " + std::string(warpLoaded ? "true" : "false"));
    out.WriteLine("  GPU UMD Loaded    : " + std::string(gpuUmdLoaded ? "true" : "false"));
    out.WriteLine("  Everything Loaded : " + std::string(everythingLoaded ? "true" : "false"));

    return (rendered && !gpuUmdLoaded && !everythingLoaded && underMemoryBudget) ? 0 : 13;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
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

    std::filesystem::path dataDir = GetExecutableDirectory() / L"data";
    std::optional<std::filesystem::path> verifyDir;
    bool smokeUi = false;

    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == L"--help" || args[i] == L"-h") {
            ConsoleOutput out;
            out.WriteLine("GeekDeskCpp Native Desktop Launcher (Mode A Direct2D Software)");
            out.WriteLine("Usage:");
            out.WriteLine("  GeekDeskCpp.exe                           Launch desktop launcher UI");
            out.WriteLine("  GeekDeskCpp.exe --data-dir <dir>          Override portable data directory");
            out.WriteLine("  GeekDeskCpp.exe --verify-data <data-dir>  Validate Schema v1 JSONs read-only");
            out.WriteLine("  GeekDeskCpp.exe --smoke-ui [--data-dir <dir>]  Run offscreen/hidden D2D paint smoke test");
            return 0;
        }
        if (args[i] == L"--verify-data") {
            ConsoleOutput out;
            if (i + 1 >= args.size()) {
                out.WriteLine("ERROR: --verify-data requires a directory argument");
                return 1;
            }
            verifyDir = std::filesystem::path(args[++i]);
            return RunVerifyData(*verifyDir, out);
        }
        if (args[i] == L"--data-dir") {
            if (i + 1 >= args.size()) {
                ConsoleOutput out;
                out.WriteLine("ERROR: --data-dir requires a directory argument");
                return 1;
            }
            dataDir = std::filesystem::path(args[++i]);
        } else if (args[i] == L"--smoke-ui") {
            smokeUi = true;
        }
    }

    if (smokeUi) {
        ConsoleOutput out;
        return RunSmokeUi(hInstance, dataDir, out);
    }

    geekdesk::platform::SingleInstanceGuard singleInstance;
    if (!singleInstance.TryAcquire()) {
        singleInstance.NotifyExistingInstance();
        return 0;
    }

    std::string layoutErr;
    if (!geekdesk::storage::BackupManager::EnsureDataLayout(dataDir, layoutErr)) {
        return 10;
    }

    const auto settingsRes = geekdesk::storage::BackupManager::LoadSettingsWithRecovery(dataDir);
    const auto catalogRes = geekdesk::storage::BackupManager::LoadCatalogWithRecovery(dataDir);
    if (!settingsRes.ok || !catalogRes.ok) {
        return 11;
    }

    geekdesk::storage::BackupManager::CreateDailyBackupsIfNeeded(
        dataDir, settingsRes.value.behavior.backupRetentionDays);

    geekdesk::storage::FileLogger logger(dataDir / L"logs", 14);
    logger.Info("GeekDeskCpp started (groups=" +
                std::to_string(catalogRes.value.groups.size()) + ")");
    if (settingsRes.recoveredFromBackup) {
        logger.Warn("Recovered settings.json from daily backup");
    }
    if (catalogRes.recoveredFromBackup) {
        logger.Warn("Recovered catalog.json from daily backup");
    }

    geekdesk::ui::MainWindow mainWindow(dataDir, settingsRes.value, catalogRes.value, &logger,
                                        singleInstance.ActivateMessage());
    if (!mainWindow.Create(hInstance, true)) {
        logger.Error("MainWindow::Create failed");
        logger.Flush();
        return 12;
    }

    if (settingsRes.value.window.startVisible) {
        mainWindow.ShowAndPosition();
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    mainWindow.Destroy();
    logger.Info("GeekDeskCpp exited cleanly");
    logger.Flush();
    return 0;
}
