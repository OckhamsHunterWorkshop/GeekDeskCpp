#include "storage/json_codec.hpp"

#include "core/utf8.hpp"

#include <yyjson.h>

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace geekdesk::storage {

namespace {

using DocPtr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using MutDocPtr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

void AddStr(yyjson_mut_doc* doc, yyjson_mut_val* obj, const char* key, std::string_view val) {
    yyjson_mut_val* v = yyjson_mut_strncpy(doc, val.data(), val.size());
    yyjson_mut_obj_add_val(doc, obj, key, v);
}

void AddOptStr(yyjson_mut_doc* doc,
               yyjson_mut_val* obj,
               const char* key,
               const std::optional<std::string>& val) {
    if (val.has_value()) {
        AddStr(doc, obj, key, *val);
    } else {
        yyjson_mut_obj_add_null(doc, obj, key);
    }
}

void ReadBool(yyjson_val* obj, const char* key, bool& out) {
    if (yyjson_val* v = yyjson_obj_get(obj, key); v && yyjson_is_bool(v)) {
        out = yyjson_get_bool(v);
    }
}

void ReadInt(yyjson_val* obj, const char* key, int& out) {
    if (yyjson_val* v = yyjson_obj_get(obj, key); v && yyjson_is_int(v)) {
        out = static_cast<int>(yyjson_get_sint(v));
    }
}

void ReadDouble(yyjson_val* obj, const char* key, double& out) {
    if (yyjson_val* v = yyjson_obj_get(obj, key); v && yyjson_is_num(v)) {
        out = yyjson_get_num(v);
    }
}

void ReadString(yyjson_val* obj, const char* key, std::string& out) {
    if (yyjson_val* v = yyjson_obj_get(obj, key); v && yyjson_is_str(v)) {
        out.assign(yyjson_get_str(v), yyjson_get_len(v));
    }
}

void ReadOptString(yyjson_val* obj, const char* key, std::optional<std::string>& out) {
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v) {
        return;
    }
    if (yyjson_is_null(v)) {
        out = std::nullopt;
    } else if (yyjson_is_str(v)) {
        out.emplace(yyjson_get_str(v), yyjson_get_len(v));
    }
}

template <typename Enum, typename ParseFn>
void ReadEnum(yyjson_val* obj,
              const char* key,
              Enum& out,
              ParseFn parseFn,
              std::vector<std::string>& warnings) {
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v || yyjson_is_null(v)) {
        return;
    }
    if (yyjson_is_str(v)) {
        const std::string_view sv(yyjson_get_str(v), yyjson_get_len(v));
        if (auto parsed = parseFn(sv); parsed.has_value()) {
            out = *parsed;
        } else {
            warnings.push_back(std::string("Unknown enum value for '") + key + "': " +
                               std::string(sv));
        }
        return;
    }
    if (yyjson_is_int(v)) {
        out = static_cast<Enum>(yyjson_get_sint(v));
    }
}

void WriteSettingsObject(yyjson_mut_doc* doc,
                         yyjson_mut_val* root,
                         const core::AppSettings& settings) {
    yyjson_mut_obj_add_int(doc, root, "schemaVersion", settings.schemaVersion);

    yyjson_mut_val* win = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, win, "width", settings.window.width);
    yyjson_mut_obj_add_real(doc, win, "height", settings.window.height);
    yyjson_mut_obj_add_real(doc, win, "sidebarWidth", settings.window.sidebarWidth);
    yyjson_mut_obj_add_bool(doc, win, "alwaysTopmost", settings.window.alwaysTopmost);
    yyjson_mut_obj_add_bool(doc, win, "followMouse", settings.window.followMouse);
    yyjson_mut_obj_add_bool(doc, win, "startVisible", settings.window.startVisible);
    AddStr(doc, win, "hidePolicy", core::ToString(settings.window.hidePolicy));
    yyjson_mut_obj_add_val(doc, root, "window", win);

    yyjson_mut_val* act = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, act, "enableHotKey", settings.activation.enableHotKey);
    AddStr(doc, act, "hotKey", settings.activation.hotKey);
    yyjson_mut_obj_add_bool(doc, act, "enableMiddleMouse", settings.activation.enableMiddleMouse);
    yyjson_mut_obj_add_bool(doc, act, "ignoreWhenFullScreen",
                            settings.activation.ignoreWhenFullScreen);
    yyjson_mut_obj_add_bool(doc, act, "edgeDockEnabled", settings.activation.edgeDockEnabled);
    yyjson_mut_obj_add_val(doc, root, "activation", act);

    yyjson_mut_val* beh = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, beh, "doubleClickToLaunch",
                            settings.behavior.doubleClickToLaunch);
    yyjson_mut_obj_add_bool(doc, beh, "hoverSwitchGroup", settings.behavior.hoverSwitchGroup);
    AddStr(doc, beh, "searchActivation", core::ToString(settings.behavior.searchActivation));
    yyjson_mut_obj_add_bool(doc, beh, "autoStart", settings.behavior.autoStart);
    AddStr(doc, beh, "groupSort", core::ToString(settings.behavior.groupSort));
    AddStr(doc, beh, "itemSort", core::ToString(settings.behavior.itemSort));
    yyjson_mut_obj_add_int(doc, beh, "backupRetentionDays",
                           settings.behavior.backupRetentionDays);
    yyjson_mut_obj_add_val(doc, root, "behavior", beh);

    yyjson_mut_val* app = yyjson_mut_obj(doc);
    AddStr(doc, app, "theme", core::ToString(settings.appearance.theme));
    AddStr(doc, app, "backdrop", core::ToString(settings.appearance.backdrop));
    AddOptStr(doc, app, "wallpaperFileName", settings.appearance.wallpaperFileName);
    yyjson_mut_obj_add_bool(doc, app, "wallpaperBlurEnabled",
                            settings.appearance.wallpaperBlurEnabled);
    AddStr(doc, app, "gradientColor1", settings.appearance.gradientColor1);
    AddStr(doc, app, "gradientColor2", settings.appearance.gradientColor2);
    yyjson_mut_obj_add_real(doc, app, "backgroundOpacity", settings.appearance.backgroundOpacity);
    yyjson_mut_obj_add_real(doc, app, "cardOpacity", settings.appearance.cardOpacity);
    yyjson_mut_obj_add_int(doc, app, "cornerRadius", settings.appearance.cornerRadius);
    yyjson_mut_obj_add_int(doc, app, "iconSize", settings.appearance.iconSize);
    yyjson_mut_obj_add_bool(doc, app, "showItemTitle", settings.appearance.showItemTitle);
    yyjson_mut_obj_add_int(doc, app, "fontSize", settings.appearance.fontSize);
    AddStr(doc, app, "fontFamily", settings.appearance.fontFamily);
    AddStr(doc, app, "textColor", settings.appearance.textColor);
    yyjson_mut_obj_add_val(doc, root, "appearance", app);

    yyjson_mut_val* st = yyjson_mut_obj(doc);
    AddOptStr(doc, st, "lastSelectedGroupId", settings.state.lastSelectedGroupId);
    yyjson_mut_obj_add_bool(doc, st, "showTrayIcon", settings.state.showTrayIcon);
    yyjson_mut_obj_add_val(doc, root, "state", st);
}

bool ParseSettingsObject(yyjson_val* root,
                         core::AppSettings& out,
                         std::string& outError,
                         std::vector<std::string>& warnings) {
    if (!root || !yyjson_is_obj(root)) {
        outError = "Settings JSON root must be an object";
        return false;
    }

    out = core::AppSettings::CreateDefault();
    if (yyjson_val* sv = yyjson_obj_get(root, "schemaVersion")) {
        if (!yyjson_is_int(sv)) {
            outError = "Settings 'schemaVersion' must be an integer";
            return false;
        }
        out.schemaVersion = static_cast<int>(yyjson_get_sint(sv));
        if (out.schemaVersion != 1) {
            outError = "Unsupported settings schemaVersion: " + std::to_string(out.schemaVersion);
            return false;
        }
    } else {
        warnings.emplace_back("Missing 'schemaVersion' in settings; assuming version 1");
    }

    if (yyjson_val* win = yyjson_obj_get(root, "window"); win && yyjson_is_obj(win)) {
        ReadDouble(win, "width", out.window.width);
        ReadDouble(win, "height", out.window.height);
        ReadDouble(win, "sidebarWidth", out.window.sidebarWidth);
        ReadBool(win, "alwaysTopmost", out.window.alwaysTopmost);
        ReadBool(win, "followMouse", out.window.followMouse);
        ReadBool(win, "startVisible", out.window.startVisible);
        ReadEnum(win, "hidePolicy", out.window.hidePolicy, core::ParseHidePolicy, warnings);
    }

    if (yyjson_val* act = yyjson_obj_get(root, "activation"); act && yyjson_is_obj(act)) {
        ReadBool(act, "enableHotKey", out.activation.enableHotKey);
        ReadString(act, "hotKey", out.activation.hotKey);
        ReadBool(act, "enableMiddleMouse", out.activation.enableMiddleMouse);
        ReadBool(act, "ignoreWhenFullScreen", out.activation.ignoreWhenFullScreen);
        ReadBool(act, "edgeDockEnabled", out.activation.edgeDockEnabled);
    }

    if (yyjson_val* beh = yyjson_obj_get(root, "behavior"); beh && yyjson_is_obj(beh)) {
        ReadBool(beh, "doubleClickToLaunch", out.behavior.doubleClickToLaunch);
        ReadBool(beh, "hoverSwitchGroup", out.behavior.hoverSwitchGroup);
        ReadEnum(beh, "searchActivation", out.behavior.searchActivation,
                 core::ParseSearchActivationMode, warnings);
        ReadBool(beh, "autoStart", out.behavior.autoStart);
        ReadEnum(beh, "groupSort", out.behavior.groupSort, core::ParseSortOrder, warnings);
        ReadEnum(beh, "itemSort", out.behavior.itemSort, core::ParseSortOrder, warnings);
        ReadInt(beh, "backupRetentionDays", out.behavior.backupRetentionDays);
        // Explicitly ignore deprecated fields such as "enableExternalEverything".
    }

    if (yyjson_val* app = yyjson_obj_get(root, "appearance"); app && yyjson_is_obj(app)) {
        ReadEnum(app, "theme", out.appearance.theme, core::ParseThemeMode, warnings);
        ReadEnum(app, "backdrop", out.appearance.backdrop, core::ParseBackdropMode, warnings);
        ReadOptString(app, "wallpaperFileName", out.appearance.wallpaperFileName);
        ReadBool(app, "wallpaperBlurEnabled", out.appearance.wallpaperBlurEnabled);
        ReadString(app, "gradientColor1", out.appearance.gradientColor1);
        ReadString(app, "gradientColor2", out.appearance.gradientColor2);
        ReadDouble(app, "backgroundOpacity", out.appearance.backgroundOpacity);
        ReadDouble(app, "cardOpacity", out.appearance.cardOpacity);
        ReadInt(app, "cornerRadius", out.appearance.cornerRadius);
        ReadInt(app, "iconSize", out.appearance.iconSize);
        ReadBool(app, "showItemTitle", out.appearance.showItemTitle);
        ReadInt(app, "fontSize", out.appearance.fontSize);
        out.appearance.fontSize = std::clamp(out.appearance.fontSize, 10, 20);
        ReadString(app, "fontFamily", out.appearance.fontFamily);
        if (core::utf8::TrimAscii(out.appearance.fontFamily).empty()) {
            out.appearance.fontFamily = "Microsoft YaHei UI";
        }
        ReadString(app, "textColor", out.appearance.textColor);
    }

    if (yyjson_val* st = yyjson_obj_get(root, "state"); st && yyjson_is_obj(st)) {
        ReadOptString(st, "lastSelectedGroupId", out.state.lastSelectedGroupId);
        ReadBool(st, "showTrayIcon", out.state.showTrayIcon);
    }

    return true;
}

void WriteCatalogObject(yyjson_mut_doc* doc,
                        yyjson_mut_val* root,
                        const core::CatalogDocument& catalog) {
    yyjson_mut_obj_add_int(doc, root, "schemaVersion", catalog.schemaVersion);

    yyjson_mut_val* groupsArr = yyjson_mut_arr(doc);
    for (const auto& group : catalog.groups) {
        yyjson_mut_val* gObj = yyjson_mut_obj(doc);
        AddStr(doc, gObj, "id", group.id);
        AddStr(doc, gObj, "name", group.name);
        AddStr(doc, gObj, "kind", core::ToString(group.kind));
        AddOptStr(doc, gObj, "linkedFolderPath", group.linkedFolderPath);
        AddOptStr(doc, gObj, "iconGlyph", group.iconGlyph);
        AddOptStr(doc, gObj, "iconColor", group.iconColor);
        AddOptStr(doc, gObj, "legacyGeometryPath", group.legacyGeometryPath);

        yyjson_mut_val* itemsArr = yyjson_mut_arr(doc);
        for (const auto& item : group.items) {
            yyjson_mut_val* iObj = yyjson_mut_obj(doc);
            AddStr(doc, iObj, "id", item.id);
            AddStr(doc, iObj, "name", item.name);
            AddStr(doc, iObj, "kind", core::ToString(item.kind));
            AddStr(doc, iObj, "targetPath", item.targetPath);
            AddOptStr(doc, iObj, "lnkPath", item.lnkPath);
            AddOptStr(doc, iObj, "relativePath", item.relativePath);
            AddStr(doc, iObj, "arguments", item.arguments);
            AddOptStr(doc, iObj, "workingDirectory", item.workingDirectory);
            yyjson_mut_obj_add_bool(doc, iObj, "runAsAdmin", item.runAsAdmin);
            yyjson_mut_obj_add_int(doc, iObj, "launchCount", item.launchCount);
            AddOptStr(doc, iObj, "lastLaunchedAt", item.lastLaunchedAt);
            AddOptStr(doc, iObj, "customIconFile", item.customIconFile);
            AddOptStr(doc, iObj, "tooltip", item.tooltip);
            yyjson_mut_arr_append(itemsArr, iObj);
        }
        yyjson_mut_obj_add_val(doc, gObj, "items", itemsArr);
        yyjson_mut_arr_append(groupsArr, gObj);
    }
    yyjson_mut_obj_add_val(doc, root, "groups", groupsArr);
}

bool ParseCatalogObject(yyjson_val* root,
                        core::CatalogDocument& out,
                        std::string& outError,
                        std::vector<std::string>& warnings) {
    if (!root || !yyjson_is_obj(root)) {
        outError = "Catalog JSON root must be an object";
        return false;
    }

    out = core::CatalogDocument{};
    if (yyjson_val* sv = yyjson_obj_get(root, "schemaVersion")) {
        if (!yyjson_is_int(sv)) {
            outError = "Catalog 'schemaVersion' must be an integer";
            return false;
        }
        out.schemaVersion = static_cast<int>(yyjson_get_sint(sv));
        if (out.schemaVersion != 1) {
            outError = "Unsupported catalog schemaVersion: " + std::to_string(out.schemaVersion);
            return false;
        }
    } else {
        warnings.emplace_back("Missing 'schemaVersion' in catalog; assuming version 1");
    }

    yyjson_val* groupsVal = yyjson_obj_get(root, "groups");
    if (!groupsVal || !yyjson_is_arr(groupsVal)) {
        outError = "Catalog missing 'groups' array";
        return false;
    }

    std::size_t idx = 0;
    std::size_t max = 0;
    yyjson_val* gVal = nullptr;
    yyjson_arr_foreach(groupsVal, idx, max, gVal) {
        if (!yyjson_is_obj(gVal)) {
            warnings.emplace_back("Skipped non-object group entry");
            continue;
        }
        core::CategoryGroup group;
        ReadString(gVal, "id", group.id);
        if (group.id.empty()) {
            group.id = core::utf8::GenerateUuidV4();
            warnings.emplace_back("Generated UUID for group missing 'id'");
        }
        ReadString(gVal, "name", group.name);
        ReadEnum(gVal, "kind", group.kind, core::ParseGroupKind, warnings);
        ReadOptString(gVal, "linkedFolderPath", group.linkedFolderPath);
        ReadOptString(gVal, "iconGlyph", group.iconGlyph);
        ReadOptString(gVal, "iconColor", group.iconColor);
        ReadOptString(gVal, "legacyGeometryPath", group.legacyGeometryPath);

        if (yyjson_val* itemsVal = yyjson_obj_get(gVal, "items");
            itemsVal && yyjson_is_arr(itemsVal)) {
            std::size_t itemIdx = 0;
            std::size_t itemMax = 0;
            yyjson_val* iVal = nullptr;
            yyjson_arr_foreach(itemsVal, itemIdx, itemMax, iVal) {
                if (!yyjson_is_obj(iVal)) {
                    warnings.emplace_back("Skipped non-object item entry");
                    continue;
                }
                core::LaunchItem item;
                ReadString(iVal, "id", item.id);
                if (item.id.empty()) {
                    item.id = core::utf8::GenerateUuidV4();
                    warnings.emplace_back("Generated UUID for item missing 'id'");
                }
                ReadString(iVal, "name", item.name);
                ReadEnum(iVal, "kind", item.kind, core::ParseItemKind, warnings);
                ReadString(iVal, "targetPath", item.targetPath);
                ReadOptString(iVal, "lnkPath", item.lnkPath);
                ReadOptString(iVal, "relativePath", item.relativePath);
                ReadString(iVal, "arguments", item.arguments);
                ReadOptString(iVal, "workingDirectory", item.workingDirectory);
                ReadBool(iVal, "runAsAdmin", item.runAsAdmin);
                ReadInt(iVal, "launchCount", item.launchCount);
                ReadOptString(iVal, "lastLaunchedAt", item.lastLaunchedAt);
                ReadOptString(iVal, "customIconFile", item.customIconFile);
                ReadOptString(iVal, "tooltip", item.tooltip);
                group.items.push_back(std::move(item));
            }
        }
        out.groups.push_back(std::move(group));
    }

    return true;
}

std::string WriteMutDoc(yyjson_mut_doc* doc) {
    std::size_t outLen = 0;
    yyjson_write_err err{};
    const yyjson_write_flag flags = YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END;
    char* raw = yyjson_mut_write_opts(doc, flags, nullptr, &outLen, &err);
    if (!raw) {
        return {};
    }
    std::string result(raw, outLen);
    std::free(raw);
    return result;
}

DocPtr ParseJsonDocument(std::string_view jsonText, std::string& outError) {
    const std::string_view clean = core::utf8::StripUtf8Bom(jsonText);
    std::string mutableCopy(clean);
    yyjson_read_err err{};
    const yyjson_read_flag flags =
        YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS;
    yyjson_doc* rawDoc =
        yyjson_read_opts(mutableCopy.data(), mutableCopy.size(), flags, nullptr, &err);
    if (!rawDoc) {
        outError = std::string(err.msg ? err.msg : "JSON parse error") + " at byte " +
                   std::to_string(err.pos);
    }
    return DocPtr(rawDoc, &yyjson_doc_free);
}

} // namespace

std::string SerializeSettings(const core::AppSettings& settings) {
    MutDocPtr doc(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);
    WriteSettingsObject(doc.get(), root, settings);
    return WriteMutDoc(doc.get());
}

ParseResult<core::AppSettings> DeserializeSettings(std::string_view jsonText) {
    ParseResult<core::AppSettings> result;
    DocPtr doc = ParseJsonDocument(jsonText, result.error);
    if (!doc) {
        return result;
    }
    result.ok = ParseSettingsObject(yyjson_doc_get_root(doc.get()), result.value, result.error,
                                    result.warnings);
    return result;
}

std::string SerializeCatalog(const core::CatalogDocument& catalog) {
    MutDocPtr doc(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);
    WriteCatalogObject(doc.get(), root, catalog);
    return WriteMutDoc(doc.get());
}

ParseResult<core::CatalogDocument> DeserializeCatalog(std::string_view jsonText) {
    ParseResult<core::CatalogDocument> result;
    DocPtr doc = ParseJsonDocument(jsonText, result.error);
    if (!doc) {
        return result;
    }
    result.ok = ParseCatalogObject(yyjson_doc_get_root(doc.get()), result.value, result.error,
                                   result.warnings);
    return result;
}

std::string SerializeBackupBundle(const BackupBundle& bundle) {
    MutDocPtr doc(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);

    yyjson_mut_obj_add_int(doc.get(), root, "backupVersion", bundle.backupVersion);
    AddStr(doc.get(), root, "appName", bundle.appName);
    AddStr(doc.get(), root, "createdAt", bundle.createdAt);

    yyjson_mut_val* settingsObj = yyjson_mut_obj(doc.get());
    WriteSettingsObject(doc.get(), settingsObj, bundle.settings);
    yyjson_mut_obj_add_val(doc.get(), root, "settings", settingsObj);

    yyjson_mut_val* catalogObj = yyjson_mut_obj(doc.get());
    WriteCatalogObject(doc.get(), catalogObj, bundle.catalog);
    yyjson_mut_obj_add_val(doc.get(), root, "catalog", catalogObj);

    return WriteMutDoc(doc.get());
}

ParseResult<BackupBundle> DeserializeBackupBundle(std::string_view jsonText) {
    ParseResult<BackupBundle> result;
    DocPtr doc = ParseJsonDocument(jsonText, result.error);
    if (!doc) {
        return result;
    }

    yyjson_val* root = yyjson_doc_get_root(doc.get());
    if (!root || !yyjson_is_obj(root)) {
        result.error = "Backup JSON root must be an object";
        return result;
    }

    if (yyjson_val* bv = yyjson_obj_get(root, "backupVersion")) {
        if (!yyjson_is_int(bv)) {
            result.error = "Backup 'backupVersion' must be an integer";
            return result;
        }
        result.value.backupVersion = static_cast<int>(yyjson_get_sint(bv));
        if (result.value.backupVersion != 1) {
            result.error =
                "Unsupported backupVersion: " + std::to_string(result.value.backupVersion);
            return result;
        }
    } else {
        result.error = "Backup bundle missing 'backupVersion'";
        return result;
    }

    ReadString(root, "appName", result.value.appName);
    ReadString(root, "createdAt", result.value.createdAt);

    yyjson_val* settingsVal = yyjson_obj_get(root, "settings");
    if (!ParseSettingsObject(settingsVal, result.value.settings, result.error, result.warnings)) {
        return result;
    }

    yyjson_val* catalogVal = yyjson_obj_get(root, "catalog");
    if (!ParseCatalogObject(catalogVal, result.value.catalog, result.error, result.warnings)) {
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace geekdesk::storage
