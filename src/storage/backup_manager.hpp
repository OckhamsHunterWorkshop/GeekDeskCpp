#pragma once

#include "core/models.hpp"
#include "storage/json_codec.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace geekdesk::storage {

template <typename T>
struct LoadDocumentResult {
    bool ok = false;
    T value{};
    bool recoveredFromBackup = false;
    bool initializedDefault = false;
    std::filesystem::path recoveredBackupPath;
    std::filesystem::path quarantinedCorruptPath;
    std::string error;
};

struct BackupSummary {
    bool ok = false;
    int backupVersion = 0;
    std::string appName;
    std::string createdAt;
    std::size_t groupCount = 0;
    std::size_t itemCount = 0;
    std::string error;
};

struct BackupOperationResult {
    bool ok = false;
    std::filesystem::path outputPath;
    std::filesystem::path rollbackSnapshotPath;
    std::string error;
};

class BackupManager {
public:
    /// Ensure standard portable directory layout under dataRoot exists.
    static bool EnsureDataLayout(const std::filesystem::path& dataRoot, std::string& outError);

    /// Load settings.json, automatically falling back to the latest valid daily snapshot
    /// if settings.json is unreadable or corrupt (quarantining the corrupt file first).
    [[nodiscard]] static LoadDocumentResult<core::AppSettings> LoadSettingsWithRecovery(
        const std::filesystem::path& dataRoot);

    /// Load catalog.json, automatically falling back to the latest valid daily snapshot
    /// if catalog.json is unreadable or corrupt (quarantining the corrupt file first).
    [[nodiscard]] static LoadDocumentResult<core::CatalogDocument> LoadCatalogWithRecovery(
        const std::filesystem::path& dataRoot);

    /// Create daily rolling backups (settings-YYYY-MM-DD.json, catalog-YYYY-MM-DD.json)
    /// if today's files do not exist yet, and prune daily backups older than retentionDays.
    static void CreateDailyBackupsIfNeeded(const std::filesystem::path& dataRoot,
                                           int retentionDays);

    /// Find the newest daily backup matching `<stem>-YYYY-MM-DD.json` under backupsDir.
    [[nodiscard]] static std::optional<std::filesystem::path> FindLatestDailyBackup(
        const std::filesystem::path& backupsDir,
        std::string_view stem);

    /// Export settings.json + catalog.json into a single self-contained JSON bundle (.gdbackup).
    [[nodiscard]] static BackupOperationResult ExportBackupBundle(
        const std::filesystem::path& dataRoot,
        const std::filesystem::path& destinationFile);

    /// Inspect and validate a .gdbackup bundle without altering dataRoot.
    [[nodiscard]] static BackupSummary InspectBackupBundle(
        const std::filesystem::path& backupFile);

    /// Restore settings.json and catalog.json from a validated .gdbackup bundle,
    /// first creating a `pre-restore-*.gdbackup` rollback snapshot under dataRoot/backups/.
    [[nodiscard]] static BackupOperationResult RestoreBackupBundle(
        const std::filesystem::path& dataRoot,
        const std::filesystem::path& backupFile);

    /// Prune daily backup files matching `<stem>-YYYY-MM-DD.json` older than retentionDays.
    /// Does NOT delete manual or pre-restore-*.gdbackup snapshots.
    static std::size_t PruneOldDailyBackups(const std::filesystem::path& backupsDir,
                                            int retentionDays);
};

} // namespace geekdesk::storage
