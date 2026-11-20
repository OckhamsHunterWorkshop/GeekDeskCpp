#include "storage/backup_manager.hpp"

#include "core/utf8.hpp"
#include "storage/atomic_writer.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

namespace geekdesk::storage {

namespace {

std::string CurrentLocalDateStamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return std::string(buf);
}

std::string CurrentLocalTimestampCompact() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%04u%02u%02u-%02u%02u%02u-%03u", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::string(buf);
}

std::string CurrentUtcIso8601() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

bool IsDigit(char ch) noexcept {
    return ch >= '0' && ch <= '9';
}

bool ParseDailyBackupDateKey(std::string_view filename,
                             std::string_view expectedStem,
                             std::string& outDateKey) {
    // Expected: "<stem>-YYYY-MM-DD.json"
    const std::size_t expectedLen = expectedStem.size() + 1 + 10 + 5;
    if (filename.size() != expectedLen) {
        return false;
    }
    if (!filename.starts_with(expectedStem)) {
        return false;
    }
    if (filename[expectedStem.size()] != '-') {
        return false;
    }
    const std::string_view datePart = filename.substr(expectedStem.size() + 1, 10);
    if (!filename.ends_with(".json")) {
        return false;
    }
    for (std::size_t i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) {
            if (datePart[i] != '-') {
                return false;
            }
        } else if (!IsDigit(datePart[i])) {
            return false;
        }
    }
    outDateKey = std::string(datePart);
    return true;
}

std::vector<std::pair<std::string, std::filesystem::path>> ListDailyBackupsDesc(
    const std::filesystem::path& backupsDir,
    std::string_view stem) {
    std::vector<std::pair<std::string, std::filesystem::path>> matches;
    std::error_code ec;
    if (!std::filesystem::exists(backupsDir, ec) || !std::filesystem::is_directory(backupsDir, ec)) {
        return matches;
    }
    for (const auto& entry : std::filesystem::directory_iterator(backupsDir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string name = core::utf8::PathToUtf8(entry.path().filename());
        std::string dateKey;
        if (ParseDailyBackupDateKey(name, stem, dateKey)) {
            matches.emplace_back(std::move(dateKey), entry.path());
        }
    }
    std::sort(matches.begin(), matches.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    return matches;
}

template <typename DocType, typename DeserializeFn, typename SerializeFn, typename DefaultFn>
LoadDocumentResult<DocType> LoadWithRecoveryImpl(const std::filesystem::path& dataRoot,
                                                 std::string_view stem,
                                                 DeserializeFn deserializeFn,
                                                 SerializeFn serializeFn,
                                                 DefaultFn defaultFn) {
    LoadDocumentResult<DocType> res;
    std::string layoutErr;
    if (!BackupManager::EnsureDataLayout(dataRoot, layoutErr)) {
        res.error = layoutErr;
        return res;
    }

    const std::filesystem::path primaryPath =
        dataRoot / core::utf8::PathFromUtf8(std::string(stem) + ".json");
    std::error_code ec;
    if (std::filesystem::exists(primaryPath, ec)) {
        std::string bytes;
        std::string readErr;
        if (ReadFileUtf8(primaryPath, bytes, readErr)) {
            auto parsed = deserializeFn(bytes);
            if (parsed.ok) {
                res.ok = true;
                res.value = std::move(parsed.value);
                return res;
            }
            res.error = parsed.error;
        } else {
            res.error = readErr;
        }

        // Preserve corrupted primary file before attempting recovery.
        const std::filesystem::path corruptPath =
            dataRoot / core::utf8::PathFromUtf8(std::string(stem) + ".corrupt." +
                                                CurrentLocalTimestampCompact() + ".bak");
        std::filesystem::rename(primaryPath, corruptPath, ec);
        if (!ec) {
            res.quarantinedCorruptPath = corruptPath;
        }

        const auto candidates = ListDailyBackupsDesc(dataRoot / L"backups", stem);
        for (const auto& [dateKey, backupPath] : candidates) {
            (void)dateKey;
            std::string bBytes;
            std::string bErr;
            if (!ReadFileUtf8(backupPath, bBytes, bErr)) {
                continue;
            }
            auto parsedBackup = deserializeFn(bBytes);
            if (parsedBackup.ok) {
                const WriteResult wr = WriteFileAtomic(primaryPath, bBytes);
                if (wr.ok) {
                    res.ok = true;
                    res.value = std::move(parsedBackup.value);
                    res.recoveredFromBackup = true;
                    res.recoveredBackupPath = backupPath;
                    res.error.clear();
                    return res;
                }
            }
        }

        // Neither primary nor backup could recover; initialize defaults safely.
        res.value = defaultFn();
        res.initializedDefault = true;
        res.ok = WriteFileAtomic(primaryPath, serializeFn(res.value)).ok;
        return res;
    }

    // Missing primary file: recover from backup if available, otherwise write clean default.
    const auto candidates = ListDailyBackupsDesc(dataRoot / L"backups", stem);
    for (const auto& [dateKey, backupPath] : candidates) {
        (void)dateKey;
        std::string bBytes;
        std::string bErr;
        if (!ReadFileUtf8(backupPath, bBytes, bErr)) {
            continue;
        }
        auto parsedBackup = deserializeFn(bBytes);
        if (parsedBackup.ok) {
            if (WriteFileAtomic(primaryPath, bBytes).ok) {
                res.ok = true;
                res.value = std::move(parsedBackup.value);
                res.recoveredFromBackup = true;
                res.recoveredBackupPath = backupPath;
                return res;
            }
        }
    }

    res.value = defaultFn();
    res.initializedDefault = true;
    const WriteResult wr = WriteFileAtomic(primaryPath, serializeFn(res.value));
    res.ok = wr.ok;
    if (!wr.ok) {
        res.error = wr.error;
    }
    return res;
}

void CreateDailyCopyIfMissing(const std::filesystem::path& primaryFile,
                              const std::filesystem::path& dailyFile) {
    std::error_code ec;
    if (!std::filesystem::exists(primaryFile, ec) || std::filesystem::exists(dailyFile, ec)) {
        return;
    }
    std::string bytes;
    std::string err;
    if (ReadFileUtf8(primaryFile, bytes, err)) {
        (void)WriteFileAtomic(dailyFile, bytes);
    }
}

} // namespace

bool BackupManager::EnsureDataLayout(const std::filesystem::path& dataRoot, std::string& outError) {
    if (dataRoot.empty()) {
        outError = "dataRoot is empty";
        return false;
    }
    const std::filesystem::path dirs[] = {
        dataRoot,
        dataRoot / L"backups",
        dataRoot / L"logs",
        dataRoot / L"assets",
        dataRoot / L"cache" / L"icons",
    };
    for (const auto& d : dirs) {
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        if (ec) {
            outError = "Failed to create directory '" + core::utf8::PathToUtf8(d) +
                       "': " + ec.message();
            return false;
        }
    }
    return true;
}

LoadDocumentResult<core::AppSettings> BackupManager::LoadSettingsWithRecovery(
    const std::filesystem::path& dataRoot) {
    return LoadWithRecoveryImpl<core::AppSettings>(dataRoot, "settings", DeserializeSettings,
                                                   SerializeSettings, core::AppSettings::CreateDefault);
}

LoadDocumentResult<core::CatalogDocument> BackupManager::LoadCatalogWithRecovery(
    const std::filesystem::path& dataRoot) {
    return LoadWithRecoveryImpl<core::CatalogDocument>(
        dataRoot, "catalog", DeserializeCatalog, SerializeCatalog,
        core::CatalogDocument::CreateDefault);
}

void BackupManager::CreateDailyBackupsIfNeeded(const std::filesystem::path& dataRoot,
                                               int retentionDays) {
    std::string layoutErr;
    if (!EnsureDataLayout(dataRoot, layoutErr)) {
        return;
    }
    const std::string today = CurrentLocalDateStamp();
    const std::filesystem::path backupsDir = dataRoot / L"backups";

    CreateDailyCopyIfMissing(
        dataRoot / L"settings.json",
        backupsDir / core::utf8::PathFromUtf8("settings-" + today + ".json"));
    CreateDailyCopyIfMissing(
        dataRoot / L"catalog.json",
        backupsDir / core::utf8::PathFromUtf8("catalog-" + today + ".json"));

    (void)PruneOldDailyBackups(backupsDir, retentionDays);
}

std::optional<std::filesystem::path> BackupManager::FindLatestDailyBackup(
    const std::filesystem::path& backupsDir,
    std::string_view stem) {
    const auto list = ListDailyBackupsDesc(backupsDir, stem);
    if (list.empty()) {
        return std::nullopt;
    }
    return list.front().second;
}

BackupOperationResult BackupManager::ExportBackupBundle(
    const std::filesystem::path& dataRoot,
    const std::filesystem::path& destinationFile) {
    BackupOperationResult result;
    const auto settingsRes = LoadSettingsWithRecovery(dataRoot);
    if (!settingsRes.ok) {
        result.error = "Failed to load settings for export: " + settingsRes.error;
        return result;
    }
    const auto catalogRes = LoadCatalogWithRecovery(dataRoot);
    if (!catalogRes.ok) {
        result.error = "Failed to load catalog for export: " + catalogRes.error;
        return result;
    }

    BackupBundle bundle;
    bundle.backupVersion = 1;
    bundle.appName = "GeekDeskCpp";
    bundle.createdAt = CurrentUtcIso8601();
    bundle.settings = settingsRes.value;
    bundle.catalog = catalogRes.value;

    const std::string payload = SerializeBackupBundle(bundle);
    const WriteResult wr = WriteFileAtomic(destinationFile, payload);
    if (!wr.ok) {
        result.error = wr.error;
        return result;
    }

    result.ok = true;
    result.outputPath = destinationFile;
    return result;
}

BackupSummary BackupManager::InspectBackupBundle(const std::filesystem::path& backupFile) {
    BackupSummary summary;
    std::string bytes;
    if (!ReadFileUtf8(backupFile, bytes, summary.error)) {
        return summary;
    }
    const auto parsed = DeserializeBackupBundle(bytes);
    if (!parsed.ok) {
        summary.error = parsed.error;
        return summary;
    }
    summary.ok = true;
    summary.backupVersion = parsed.value.backupVersion;
    summary.appName = parsed.value.appName;
    summary.createdAt = parsed.value.createdAt;
    summary.groupCount = parsed.value.catalog.groups.size();
    for (const auto& group : parsed.value.catalog.groups) {
        summary.itemCount += group.items.size();
    }
    return summary;
}

BackupOperationResult BackupManager::RestoreBackupBundle(
    const std::filesystem::path& dataRoot,
    const std::filesystem::path& backupFile) {
    BackupOperationResult result;
    std::string bytes;
    if (!ReadFileUtf8(backupFile, bytes, result.error)) {
        return result;
    }
    const auto parsed = DeserializeBackupBundle(bytes);
    if (!parsed.ok) {
        result.error = "Invalid backup bundle: " + parsed.error;
        return result;
    }

    std::string layoutErr;
    if (!EnsureDataLayout(dataRoot, layoutErr)) {
        result.error = layoutErr;
        return result;
    }

    // Create a pre-restore rollback snapshot if existing primary files are present.
    std::error_code ec;
    const std::filesystem::path settingsPath = dataRoot / L"settings.json";
    const std::filesystem::path catalogPath = dataRoot / L"catalog.json";
    if (std::filesystem::exists(settingsPath, ec) || std::filesystem::exists(catalogPath, ec)) {
        const std::filesystem::path rollbackPath =
            dataRoot / L"backups" /
            core::utf8::PathFromUtf8("pre-restore-" + CurrentLocalTimestampCompact() + ".gdbackup");
        const auto rollbackExport = ExportBackupBundle(dataRoot, rollbackPath);
        if (!rollbackExport.ok) {
            result.error = "Failed to create pre-restore rollback snapshot: " + rollbackExport.error;
            return result;
        }
        result.rollbackSnapshotPath = rollbackPath;
    }

    const WriteResult wrSettings =
        WriteFileAtomic(settingsPath, SerializeSettings(parsed.value.settings));
    if (!wrSettings.ok) {
        result.error = "Failed to restore settings.json: " + wrSettings.error;
        return result;
    }
    const WriteResult wrCatalog =
        WriteFileAtomic(catalogPath, SerializeCatalog(parsed.value.catalog));
    if (!wrCatalog.ok) {
        result.error = "Failed to restore catalog.json: " + wrCatalog.error;
        return result;
    }

    result.ok = true;
    result.outputPath = dataRoot;
    return result;
}

std::size_t BackupManager::PruneOldDailyBackups(const std::filesystem::path& backupsDir,
                                                int retentionDays) {
    const int safeDays = std::max(1, retentionDays);
    std::error_code ec;
    if (!std::filesystem::exists(backupsDir, ec) || !std::filesystem::is_directory(backupsDir, ec)) {
        return 0;
    }

    const auto now = std::filesystem::file_time_type::clock::now();
    const auto cutoff = now - std::chrono::hours(24LL * safeDays);

    std::size_t removed = 0;
    for (const auto& entry : std::filesystem::directory_iterator(backupsDir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string name = core::utf8::PathToUtf8(entry.path().filename());
        std::string unusedDate;
        const bool isDaily = ParseDailyBackupDateKey(name, "settings", unusedDate) ||
                             ParseDailyBackupDateKey(name, "catalog", unusedDate);
        if (!isDaily) {
            continue;
        }
        const auto mtime = entry.last_write_time(ec);
        if (!ec && mtime < cutoff) {
            if (std::filesystem::remove(entry.path(), ec)) {
                ++removed;
            }
        }
    }
    return removed;
}

} // namespace geekdesk::storage
