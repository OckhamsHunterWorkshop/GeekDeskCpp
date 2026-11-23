#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace geekdesk::core {

enum class HidePolicy {
    OnDeactivate = 1,
    AfterLaunch = 2,
    ManualOnly = 3,
};

enum class SearchActivationMode {
    HotKeyOnly = 0,
    TypeToSearch = 1,
};

enum class SortOrder {
    Custom = 1,
    LaunchCountAsc = 2,
    LaunchCountDesc = 3,
    NameAsc = 4,
    NameDesc = 5,
};

enum class ThemeMode {
    System = 0,
    Light = 1,
    Dark = 2,
};

enum class BackdropMode {
    Acrylic = 0,
    Mica = 1,
    MicaAlt = 2,
    Wallpaper = 3,
    Gradient = 4,
};

enum class GroupKind {
    Manual = 0,
    LinkedFolder = 1,
};

enum class ItemKind {
    File = 0,
    Folder = 1,
    Url = 2,
    SystemItem = 3,
};

enum class DockEdge {
    None = 0,
    Top = 1,
    Left = 2,
    Right = 3,
};

enum class UiLanguage {
    Auto = 0,
    ZhCn = 1,
    EnUs = 2,
};

[[nodiscard]] std::string_view ToString(HidePolicy value);
[[nodiscard]] std::optional<HidePolicy> ParseHidePolicy(std::string_view text);

[[nodiscard]] std::string_view ToString(SearchActivationMode value);
[[nodiscard]] std::optional<SearchActivationMode> ParseSearchActivationMode(std::string_view text);

[[nodiscard]] std::string_view ToString(SortOrder value);
[[nodiscard]] std::optional<SortOrder> ParseSortOrder(std::string_view text);

[[nodiscard]] std::string_view ToString(ThemeMode value);
[[nodiscard]] std::optional<ThemeMode> ParseThemeMode(std::string_view text);

[[nodiscard]] std::string_view ToString(BackdropMode value);
[[nodiscard]] std::optional<BackdropMode> ParseBackdropMode(std::string_view text);

[[nodiscard]] std::string_view ToString(GroupKind value);
[[nodiscard]] std::optional<GroupKind> ParseGroupKind(std::string_view text);

[[nodiscard]] std::string_view ToString(ItemKind value);
[[nodiscard]] std::optional<ItemKind> ParseItemKind(std::string_view text);

[[nodiscard]] std::string_view ToString(DockEdge value);
[[nodiscard]] std::optional<DockEdge> ParseDockEdge(std::string_view text);

[[nodiscard]] std::string_view ToString(UiLanguage value);
[[nodiscard]] std::optional<UiLanguage> ParseUiLanguage(std::string_view text);

struct WindowSettings {
    double width = 820.0;
    double height = 520.0;
    double sidebarWidth = 160.0;
    bool alwaysTopmost = false;
    bool followMouse = true;
    bool startVisible = true;
    HidePolicy hidePolicy = HidePolicy::AfterLaunch;
};

struct ActivationSettings {
    bool enableHotKey = true;
    std::string hotKey = "Ctrl+Q";
    bool enableMiddleMouse = true;
    bool ignoreWhenFullScreen = true;
    bool edgeDockEnabled = false;
};

struct BehaviorSettings {
    bool doubleClickToLaunch = false;
    bool hoverSwitchGroup = false;
    SearchActivationMode searchActivation = SearchActivationMode::HotKeyOnly;
    bool autoStart = false;
    SortOrder groupSort = SortOrder::Custom;
    SortOrder itemSort = SortOrder::Custom;
    int backupRetentionDays = 7;
    std::optional<std::string> desktopArchiveDir;
};

struct AppearanceSettings {
    ThemeMode theme = ThemeMode::Dark;
    BackdropMode backdrop = BackdropMode::Acrylic;
    std::optional<std::string> wallpaperFileName;
    bool wallpaperBlurEnabled = true;
    std::string gradientColor1 = "#2563EB";
    std::string gradientColor2 = "#0F172A";
    double backgroundOpacity = 0.80;
    double cardOpacity = 0.50;
    int cornerRadius = 4;
    int iconSize = 56;
    bool showItemTitle = true;
    int fontSize = 12;
    std::string fontFamily = "Microsoft YaHei UI";
    UiLanguage language = UiLanguage::Auto;
    std::string textColor = "#FFFFFF";
};

struct UiState {
    std::optional<std::string> lastSelectedGroupId;
    bool showTrayIcon = true;
};

struct AppSettings {
    int schemaVersion = 1;
    WindowSettings window;
    ActivationSettings activation;
    BehaviorSettings behavior;
    AppearanceSettings appearance;
    UiState state;

    [[nodiscard]] static AppSettings CreateDefault();
};

struct LaunchItem {
    std::string id;
    std::string name;
    ItemKind kind = ItemKind::File;
    std::string targetPath;
    std::optional<std::string> lnkPath;
    std::optional<std::string> relativePath;
    std::string arguments;
    std::optional<std::string> workingDirectory;
    bool runAsAdmin = false;
    int launchCount = 0;
    std::optional<std::string> lastLaunchedAt;
    std::optional<std::string> customIconFile;
    std::optional<std::string> tooltip;

    [[nodiscard]] bool IsExecutableOrScript() const noexcept;
};

struct CategoryGroup {
    std::string id;
    std::string name;
    GroupKind kind = GroupKind::Manual;
    std::optional<std::string> linkedFolderPath;
    std::optional<std::string> iconGlyph;
    std::optional<std::string> iconColor;
    std::optional<std::string> legacyGeometryPath;
    std::vector<LaunchItem> items;
};

struct CatalogDocument {
    int schemaVersion = 1;
    std::vector<CategoryGroup> groups;

    [[nodiscard]] static CatalogDocument CreateDefault();
};

} // namespace geekdesk::core
