#pragma once

#include "core/models.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace geekdesk::storage {

template <typename T>
struct ParseResult {
    bool ok = false;
    T value{};
    std::string error;
    std::vector<std::string> warnings;
};

struct BackupBundle {
    int backupVersion = 1;
    std::string appName = "GeekDeskCpp";
    std::string createdAt;
    core::AppSettings settings;
    core::CatalogDocument catalog;
};

[[nodiscard]] std::string SerializeSettings(const core::AppSettings& settings);
[[nodiscard]] ParseResult<core::AppSettings> DeserializeSettings(std::string_view jsonText);

[[nodiscard]] std::string SerializeCatalog(const core::CatalogDocument& catalog);
[[nodiscard]] ParseResult<core::CatalogDocument> DeserializeCatalog(std::string_view jsonText);

[[nodiscard]] std::string SerializeBackupBundle(const BackupBundle& bundle);
[[nodiscard]] ParseResult<BackupBundle> DeserializeBackupBundle(std::string_view jsonText);

} // namespace geekdesk::storage
