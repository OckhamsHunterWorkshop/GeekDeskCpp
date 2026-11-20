#include "core/models.hpp"

#include "core/utf8.hpp"

namespace geekdesk::core {

namespace {

template <typename Enum>
std::optional<Enum> MatchEnum(std::string_view text,
                              std::initializer_list<std::pair<std::string_view, Enum>> table) {
    const std::string lower = utf8::ToLowerAscii(utf8::TrimAscii(text));
    for (const auto& [name, value] : table) {
        if (utf8::ToLowerAscii(name) == lower) {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace

std::string_view ToString(HidePolicy value) {
    switch (value) {
    case HidePolicy::OnDeactivate:
        return "OnDeactivate";
    case HidePolicy::AfterLaunch:
        return "AfterLaunch";
    case HidePolicy::ManualOnly:
        return "ManualOnly";
    }
    return "AfterLaunch";
}

std::optional<HidePolicy> ParseHidePolicy(std::string_view text) {
    return MatchEnum<HidePolicy>(text, {
                                           {"OnDeactivate", HidePolicy::OnDeactivate},
                                           {"AfterLaunch", HidePolicy::AfterLaunch},
                                           {"ManualOnly", HidePolicy::ManualOnly},
                                       });
}

std::string_view ToString(SearchActivationMode value) {
    switch (value) {
    case SearchActivationMode::HotKeyOnly:
        return "HotKeyOnly";
    case SearchActivationMode::TypeToSearch:
        return "TypeToSearch";
    }
    return "HotKeyOnly";
}

std::optional<SearchActivationMode> ParseSearchActivationMode(std::string_view text) {
    return MatchEnum<SearchActivationMode>(
        text, {
                  {"HotKeyOnly", SearchActivationMode::HotKeyOnly},
                  {"TypeToSearch", SearchActivationMode::TypeToSearch},
              });
}

std::string_view ToString(SortOrder value) {
    switch (value) {
    case SortOrder::Custom:
        return "Custom";
    case SortOrder::LaunchCountAsc:
        return "LaunchCountAsc";
    case SortOrder::LaunchCountDesc:
        return "LaunchCountDesc";
    case SortOrder::NameAsc:
        return "NameAsc";
    case SortOrder::NameDesc:
        return "NameDesc";
    }
    return "Custom";
}

std::optional<SortOrder> ParseSortOrder(std::string_view text) {
    return MatchEnum<SortOrder>(text, {
                                          {"Custom", SortOrder::Custom},
                                          {"LaunchCountAsc", SortOrder::LaunchCountAsc},
                                          {"LaunchCountDesc", SortOrder::LaunchCountDesc},
                                          {"NameAsc", SortOrder::NameAsc},
                                          {"NameDesc", SortOrder::NameDesc},
                                      });
}

std::string_view ToString(ThemeMode value) {
    switch (value) {
    case ThemeMode::System:
        return "System";
    case ThemeMode::Light:
        return "Light";
    case ThemeMode::Dark:
        return "Dark";
    }
    return "System";
}

std::optional<ThemeMode> ParseThemeMode(std::string_view text) {
    return MatchEnum<ThemeMode>(text, {
                                          {"System", ThemeMode::System},
                                          {"Light", ThemeMode::Light},
                                          {"Dark", ThemeMode::Dark},
                                      });
}

std::string_view ToString(BackdropMode value) {
    switch (value) {
    case BackdropMode::Acrylic:
        return "Acrylic";
    case BackdropMode::Mica:
        return "Mica";
    case BackdropMode::MicaAlt:
        return "MicaAlt";
    case BackdropMode::Wallpaper:
        return "Wallpaper";
    case BackdropMode::Gradient:
        return "Gradient";
    }
    return "Mica";
}

std::optional<BackdropMode> ParseBackdropMode(std::string_view text) {
    return MatchEnum<BackdropMode>(text, {
                                             {"Acrylic", BackdropMode::Acrylic},
                                             {"Mica", BackdropMode::Mica},
                                             {"MicaAlt", BackdropMode::MicaAlt},
                                             {"Wallpaper", BackdropMode::Wallpaper},
                                             {"Gradient", BackdropMode::Gradient},
                                         });
}

std::string_view ToString(GroupKind value) {
    switch (value) {
    case GroupKind::Manual:
        return "Manual";
    case GroupKind::LinkedFolder:
        return "LinkedFolder";
    }
    return "Manual";
}

std::optional<GroupKind> ParseGroupKind(std::string_view text) {
    return MatchEnum<GroupKind>(text, {
                                          {"Manual", GroupKind::Manual},
                                          {"LinkedFolder", GroupKind::LinkedFolder},
                                      });
}

std::string_view ToString(ItemKind value) {
    switch (value) {
    case ItemKind::File:
        return "File";
    case ItemKind::Folder:
        return "Folder";
    case ItemKind::Url:
        return "Url";
    case ItemKind::SystemItem:
        return "SystemItem";
    }
    return "File";
}

std::optional<ItemKind> ParseItemKind(std::string_view text) {
    return MatchEnum<ItemKind>(text, {
                                         {"File", ItemKind::File},
                                         {"Folder", ItemKind::Folder},
                                         {"Url", ItemKind::Url},
                                         {"SystemItem", ItemKind::SystemItem},
                                     });
}

std::string_view ToString(DockEdge value) {
    switch (value) {
    case DockEdge::None:
        return "None";
    case DockEdge::Top:
        return "Top";
    case DockEdge::Left:
        return "Left";
    case DockEdge::Right:
        return "Right";
    }
    return "None";
}

std::optional<DockEdge> ParseDockEdge(std::string_view text) {
    return MatchEnum<DockEdge>(text, {
                                         {"None", DockEdge::None},
                                         {"Top", DockEdge::Top},
                                         {"Left", DockEdge::Left},
                                         {"Right", DockEdge::Right},
                                     });
}

AppSettings AppSettings::CreateDefault() {
    return AppSettings{};
}

bool LaunchItem::IsExecutableOrScript() const noexcept {
    if (kind != ItemKind::File) {
        return false;
    }
    const auto dotPos = targetPath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }
    const std::string ext = utf8::ToLowerAscii(std::string_view(targetPath).substr(dotPos));
    return ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".msi" || ext == ".ps1";
}

CatalogDocument CatalogDocument::CreateDefault() {
    CatalogDocument doc;
    CategoryGroup group;
    group.id = utf8::GenerateUuidV4();
    // UTF-8 for U+5E38 U+7528
    group.name = "\xE5\xB8\xB8\xE7\x94\xA8";
    group.kind = GroupKind::Manual;
    doc.groups.push_back(std::move(group));
    return doc;
}

} // namespace geekdesk::core
