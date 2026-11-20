#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "core/catalog_sorter.hpp"
#include "core/folder_sync.hpp"
#include "core/hotkey_parser.hpp"
#include "core/models.hpp"
#include "core/pinyin.hpp"
#include "core/placement_math.hpp"
#include "core/search_engine.hpp"
#include "core/utf8.hpp"
#include "storage/atomic_writer.hpp"
#include "storage/backup_manager.hpp"
#include "storage/json_codec.hpp"
#include "storage/logger.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace geekdesk::core;
using namespace geekdesk::storage;

namespace {

struct TempDirGuard {
    std::filesystem::path path;

    explicit TempDirGuard(std::string_view suffix) {
        static std::atomic<std::uint64_t> counter{0};
        const auto n = counter.fetch_add(1, std::memory_order_relaxed);
        const std::string dirName = "geekdeskcpp_test_" + std::to_string(GetCurrentProcessId()) +
                                    "_" + std::to_string(n) + "_" + std::string(suffix);
        path = std::filesystem::temp_directory_path() / utf8::PathFromUtf8(dirName);
        std::filesystem::create_directories(path);
    }

    ~TempDirGuard() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("utf8 utilities handle CJK paths, BOM stripping, codepoints, and UUID") {
    // "测试目录/元宝应用.lnk" in UTF-8
    const std::string utf8Sample =
        "\xE6\xB5\x8B\xE8\xAF\x95\xE7\x9B\xAE\xE5\xBD\x95/\xE5\x85\x83\xE5\xAE\x9D\xE5\xBA\x94\xE7\x94\xA8.lnk";
    const std::filesystem::path p = utf8::PathFromUtf8(utf8Sample);
    CHECK(utf8::PathToUtf8(p).find(".lnk") != std::string::npos);

    const std::string wideRoundTrip = utf8::WideToUtf8(utf8::Utf8ToWide(utf8Sample));
    CHECK(wideRoundTrip == utf8Sample);

    const std::string withBom = "\xEF\xBB\xBF{\"hello\":1}";
    CHECK(utf8::StripUtf8Bom(withBom) == "{\"hello\":1}");
    CHECK(utf8::StripUtf8Bom("{\"ok\":true}") == "{\"ok\":true}");

    std::size_t pos = 0;
    // "中A" -> U+4E2D, 'A'
    const std::string mixed = "\xE4\xB8\xAD" "A";
    CHECK(utf8::NextUtf8Codepoint(mixed, pos) == 0x4E2Du);
    CHECK(utf8::NextUtf8Codepoint(mixed, pos) == U'A');
    CHECK(pos == mixed.size());

    const std::string id1 = utf8::GenerateUuidV4();
    const std::string id2 = utf8::GenerateUuidV4();
    CHECK(id1.size() == 36);
    CHECK(id1[8] == '-');
    CHECK(id1[13] == '-');
    CHECK(id1 != id2);
}

TEST_CASE("pinyin lookup and key generation handle CJK, English CamelCase, and mixed text") {
    CHECK(pinyin::LookupSyllable(0x4E2D) == "zhong"); // 中
    CHECK(pinyin::LookupSyllable(0x6587) == "wen");   // 文
    CHECK(pinyin::LookupSyllable(U'A').empty());

    // "元宝" -> full: yuanbao, initials: yb
    const auto yb = pinyin::BuildPinyinKeys("\xE5\x85\x83\xE5\xAE\x9D");
    CHECK(yb.full == "yuanbao");
    CHECK(yb.initials == "yb");

    const auto vsc = pinyin::BuildPinyinKeys("Visual Studio Code");
    CHECK(vsc.full == "visualstudiocode");
    CHECK(vsc.initials == "vsc");

    const auto ds = pinyin::BuildPinyinKeys("DeepSeek Harness");
    CHECK(ds.full == "deepseekharness");
    CHECK(ds.initials == "dsh");

    // "元宝AI" -> full: yuanbaoai, initials: yba
    const auto ybai = pinyin::BuildPinyinKeys("\xE5\x85\x83\xE5\xAE\x9D" "AI");
    CHECK(ybai.full == "yuanbaoai");
    CHECK(ybai.initials == "yba");
}

TEST_CASE("hotkey parser parses valid combinations and rejects invalid inputs") {
    const auto hk1 = ParseHotKey("Ctrl+Q");
    REQUIRE(hk1.has_value());
    CHECK(hk1->modifiers == MOD_CONTROL);
    CHECK(hk1->virtualKey == 'Q');
    CHECK(hk1->normalized == "Ctrl+Q");

    const auto hk2 = ParseHotKey("  ctrl + alt + shift + space ");
    REQUIRE(hk2.has_value());
    CHECK(hk2->modifiers == (MOD_CONTROL | MOD_ALT | MOD_SHIFT));
    CHECK(hk2->virtualKey == VK_SPACE);
    CHECK(hk2->normalized == "Ctrl+Alt+Shift+Space");

    const auto hkFn = ParseHotKey("F9");
    REQUIRE(hkFn.has_value());
    CHECK(hkFn->modifiers == 0);
    CHECK(hkFn->virtualKey == VK_F9);
    CHECK(hkFn->normalized == "F9");

    const auto hkWin = ParseHotKey("Win+E");
    REQUIRE(hkWin.has_value());
    CHECK(hkWin->modifiers == MOD_WIN);
    CHECK(hkWin->virtualKey == 'E');

    CHECK_FALSE(ParseHotKey("").has_value());
    CHECK_FALSE(ParseHotKey("Ctrl+").has_value());
    CHECK_FALSE(ParseHotKey("Ctrl+Shift").has_value());
    CHECK_FALSE(ParseHotKey("Q", true).has_value());
    CHECK(ParseHotKey("Q", false).has_value());
    CHECK_FALSE(ParseHotKey("Ctrl+UnknownKey").has_value());
}

TEST_CASE("placement math aligns cursor top-left and clamps across multi-DPI monitors") {
    CHECK(DipToPhysicalPx(820.0, 96) == 820);
    CHECK(DipToPhysicalPx(820.0, 144) == 1230);
    CHECK(DipToPhysicalPx(100.0, 192) == 200);

    const PixelRect primaryWork{0, 0, 1920, 1040};
    // Inside bounds -> cursor is exact top-left
    CHECK(ComputeTopLeftClampedPosition({300, 200}, 820, 520, primaryWork) ==
          PixelPoint{300, 200});
    // Near bottom-right -> clamped so window fits cleanly inside monitor
    CHECK(ComputeTopLeftClampedPosition({1800, 900}, 820, 520, primaryWork) ==
          PixelPoint{1100, 520});
    // Left negative secondary monitor
    const PixelRect leftWork{-1920, 0, 0, 1080};
    CHECK(ComputeTopLeftClampedPosition({-1950, -20}, 820, 520, leftWork) ==
          PixelPoint{-1920, 0});
    CHECK(ComputeTopLeftClampedPosition({-200, 900}, 820, 520, leftWork) ==
          PixelPoint{-820, 560});

    CHECK(ComputeCenteredPosition(800, 500, primaryWork) == PixelPoint{560, 270});
    CHECK(ComputeGridColumns(600, 100, 16) == 5);
}

TEST_CASE("catalog sorter orders naturally and linked folder sync preserves item metadata") {
    std::vector<LaunchItem> items;
    LaunchItem a;
    a.id = "1";
    a.name = "Item 10";
    a.launchCount = 2;
    LaunchItem b;
    b.id = "2";
    b.name = "Item 2";
    b.launchCount = 15;
    LaunchItem c;
    c.id = "3";
    c.name = "Alpha";
    c.launchCount = 15;
    items.push_back(a);
    items.push_back(b);
    items.push_back(c);

    SortItems(items, SortOrder::NameAsc);
    CHECK(items[0].name == "Alpha");
    CHECK(items[1].name == "Item 2");
    CHECK(items[2].name == "Item 10");

    SortItems(items, SortOrder::LaunchCountDesc);
    CHECK(items[0].name == "Alpha");
    CHECK(items[1].name == "Item 2");
    CHECK(items[2].name == "Item 10");

    // Linked folder sync test
    TempDirGuard dir("sync");
    REQUIRE(WriteFileAtomic(dir.path / L"ToolOne.exe", "bin1").ok);
    REQUIRE(WriteFileAtomic(dir.path / L"Notes.txt", "txt").ok);
    REQUIRE(WriteFileAtomic(dir.path / L"desktop.ini", "ignored").ok);
    std::filesystem::create_directories(dir.path / L"SubFolder");

    CategoryGroup group;
    group.id = utf8::GenerateUuidV4();
    group.name = "Linked";
    group.kind = GroupKind::LinkedFolder;
    group.linkedFolderPath = utf8::PathToUtf8(dir.path);

    const FolderSyncResult r1 = SyncLinkedFolderGroup(group);
    CHECK(r1.folderExists);
    CHECK(r1.added == 3);
    CHECK(r1.removed == 0);
    CHECK(group.items.size() == 3);
    // Folders sort before files
    CHECK(group.items[0].kind == ItemKind::Folder);
    CHECK(group.items[0].name == "SubFolder");

    std::string preservedId;
    for (auto& item : group.items) {
        if (item.name == "ToolOne") {
            item.launchCount = 42;
            item.arguments = "--silent";
            item.customIconFile = "custom.png";
            preservedId = item.id;
        }
    }
    REQUIRE_FALSE(preservedId.empty());

    std::filesystem::remove(dir.path / L"Notes.txt");
    REQUIRE(WriteFileAtomic(dir.path / L"AddedApp.lnk", "lnk").ok);

    const FolderSyncResult r2 = SyncLinkedFolderGroup(group);
    CHECK(r2.added == 1);
    CHECK(r2.removed == 1);
    CHECK(r2.preserved == 2);

    bool foundPreserved = false;
    bool foundAdded = false;
    for (const auto& item : group.items) {
        if (item.id == preservedId) {
            foundPreserved = true;
            CHECK(item.launchCount == 42);
            CHECK(item.arguments == "--silent");
            CHECK(item.customIconFile == "custom.png");
        }
        if (item.name == "AddedApp") {
            foundAdded = true;
        }
    }
    CHECK(foundPreserved);
    CHECK(foundAdded);
}

TEST_CASE("search engine matches English, Chinese pinyin initials, full pinyin, and ranks frequency") {
    CatalogDocument doc;
    CategoryGroup grp;
    grp.id = "g1";
    grp.name = "AI";

    LaunchItem i1;
    i1.id = "1";
    i1.name = "\xE8\x85\xBE\xE8\xAE\xAF\xE5\x85\x83\xE5\xAE\x9D"; // 腾讯元宝
    i1.targetPath = "C:\\Apps\\YuanBao.exe";
    i1.launchCount = 5;

    LaunchItem i2;
    i2.id = "2";
    i2.name = "Visual Studio Code";
    i2.targetPath = "C:\\Apps\\Code.exe";
    i2.launchCount = 30;

    LaunchItem i3;
    i3.id = "3";
    i3.name = "Code Blocks";
    i3.targetPath = "C:\\Apps\\CB.exe";
    i3.launchCount = 2;

    grp.items = {i1, i2, i3};
    doc.groups.push_back(grp);

    SearchEngine engine;
    engine.Rebuild(doc);
    CHECK(engine.IndexedCount() == 3);
    CHECK(engine.Search("").empty());

    const auto pyInitial = engine.Search("txyb");
    REQUIRE(pyInitial.size() == 1);
    CHECK(pyInitial[0].itemId == "1");

    const auto pyFull = engine.Search("yuanbao");
    REQUIRE(pyFull.size() == 1);
    CHECK(pyFull[0].itemId == "1");

    const auto vscMatches = engine.Search("vsc");
    REQUIRE(vscMatches.size() >= 1);
    CHECK(vscMatches[0].itemId == "2");

    const auto freq = engine.TopFrequent(2);
    REQUIRE(freq.size() == 2);
    CHECK(freq[0].itemId == "2");
    CHECK(freq[1].itemId == "1");
}

TEST_CASE("json codec round-trips Schema v1, ignores deprecated Everything field, and rejects invalid versions") {
    AppSettings s = AppSettings::CreateDefault();
    s.window.width = 900.0;
    s.window.hidePolicy = HidePolicy::OnDeactivate;
    s.activation.hotKey = "Ctrl+Alt+Space";
    s.behavior.backupRetentionDays = 14;
    s.appearance.theme = ThemeMode::Dark;
    s.appearance.backdrop = BackdropMode::Acrylic;
    s.appearance.wallpaperFileName = "bg.png";
    s.state.lastSelectedGroupId = "group-123";

    const std::string sJson = SerializeSettings(s);
    const auto sParsed = DeserializeSettings(sJson);
    REQUIRE(sParsed.ok);
    CHECK(sParsed.value.window.width == doctest::Approx(900.0));
    CHECK(sParsed.value.window.hidePolicy == HidePolicy::OnDeactivate);
    CHECK(sParsed.value.activation.hotKey == "Ctrl+Alt+Space");
    CHECK(sParsed.value.behavior.backupRetentionDays == 14);
    CHECK(sParsed.value.appearance.theme == ThemeMode::Dark);
    CHECK(sParsed.value.appearance.backdrop == BackdropMode::Acrylic);
    CHECK(sParsed.value.appearance.wallpaperFileName == "bg.png");
    CHECK(sParsed.value.state.lastSelectedGroupId == "group-123");

    // Deprecated enableExternalEverything field + BOM + trailing comma tolerated
    const std::string compatJson =
        "\xEF\xBB\xBF{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"behavior\": {\n"
        "    \"enableExternalEverything\": true,\n"
        "    \"backupRetentionDays\": 21,\n"
        "  },\n"
        "}\n";
    const auto compatParsed = DeserializeSettings(compatJson);
    REQUIRE(compatParsed.ok);
    CHECK(compatParsed.value.behavior.backupRetentionDays == 21);

    const auto futureParsed = DeserializeSettings("{\"schemaVersion\": 99}");
    CHECK_FALSE(futureParsed.ok);

    CatalogDocument c = CatalogDocument::CreateDefault();
    LaunchItem item;
    item.id = utf8::GenerateUuidV4();
    item.name = "\xE5\x85\x83\xE5\xAE\x9D"; // 元宝
    item.kind = ItemKind::File;
    item.targetPath = "C:\\Apps\\YuanBao.exe";
    item.runAsAdmin = true;
    item.launchCount = 9;
    c.groups[0].items.push_back(item);

    const std::string cJson = SerializeCatalog(c);
    const auto cParsed = DeserializeCatalog(cJson);
    REQUIRE(cParsed.ok);
    REQUIRE(cParsed.value.groups.size() == 1);
    REQUIRE(cParsed.value.groups[0].items.size() == 1);
    CHECK(cParsed.value.groups[0].items[0].name == item.name);
    CHECK(cParsed.value.groups[0].items[0].runAsAdmin);
    CHECK(cParsed.value.groups[0].items[0].launchCount == 9);
}

TEST_CASE("atomic writer leaves no temp files and debounced writer coalesces rapid bursts") {
    TempDirGuard dir("writer");
    const std::filesystem::path target = dir.path / L"\x4E2D\x6587.json"; // 中文.json

    const WriteResult w1 = WriteFileAtomic(target, "{\"v\":1}");
    CHECK(w1.ok);
    const WriteResult w2 = WriteFileAtomic(target, "{\"v\":2}");
    CHECK(w2.ok);

    std::string content;
    std::string err;
    REQUIRE(ReadFileUtf8(target, content, err));
    CHECK(content == "{\"v\":2}");

    int fileCount = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir.path)) {
        (void)e;
        ++fileCount;
    }
    CHECK(fileCount == 1);

    const std::filesystem::path debouncedTarget = dir.path / L"debounced.json";
    {
        DebouncedFileWriter writer;
        for (int i = 1; i <= 25; ++i) {
            writer.Schedule(debouncedTarget, "{\"seq\":" + std::to_string(i) + "}",
                            std::chrono::milliseconds(40));
        }
        writer.Flush();
        CHECK(writer.CompletedWritesCount() < 10);
    }

    REQUIRE(ReadFileUtf8(debouncedTarget, content, err));
    CHECK(content == "{\"seq\":25}");
}

TEST_CASE("backup manager handles daily snapshots, corruption recovery, export/inspect/restore with rollback, and pruning") {
    TempDirGuard dir("backup");
    const std::filesystem::path dataRoot = dir.path / L"data";

    AppSettings s = AppSettings::CreateDefault();
    s.window.width = 888.0;
    CatalogDocument c = CatalogDocument::CreateDefault();
    LaunchItem item;
    item.id = utf8::GenerateUuidV4();
    item.name = "OriginalApp";
    item.targetPath = "C:\\Original.exe";
    item.launchCount = 12;
    c.groups[0].items.push_back(item);

    std::string layoutErr;
    REQUIRE(BackupManager::EnsureDataLayout(dataRoot, layoutErr));
    REQUIRE(WriteFileAtomic(dataRoot / L"settings.json", SerializeSettings(s)).ok);
    REQUIRE(WriteFileAtomic(dataRoot / L"catalog.json", SerializeCatalog(c)).ok);

    BackupManager::CreateDailyBackupsIfNeeded(dataRoot, 7);
    REQUIRE(BackupManager::FindLatestDailyBackup(dataRoot / L"backups", "catalog").has_value());

    // Export manual bundle
    const std::filesystem::path manualBundle = dir.path / L"snapshot.gdbackup";
    const auto exp = BackupManager::ExportBackupBundle(dataRoot, manualBundle);
    REQUIRE(exp.ok);

    const auto inspected = BackupManager::InspectBackupBundle(manualBundle);
    REQUIRE(inspected.ok);
    CHECK(inspected.groupCount == 1);
    CHECK(inspected.itemCount == 1);

    // Corrupt primary catalog.json and verify automatic recovery + quarantine
    REQUIRE(WriteFileAtomic(dataRoot / L"catalog.json", "{ broken json !!!").ok);
    const auto recovered = BackupManager::LoadCatalogWithRecovery(dataRoot);
    REQUIRE(recovered.ok);
    CHECK(recovered.recoveredFromBackup);
    CHECK_FALSE(recovered.quarantinedCorruptPath.empty());
    CHECK(std::filesystem::exists(recovered.quarantinedCorruptPath));
    REQUIRE(recovered.value.groups[0].items.size() == 1);
    CHECK(recovered.value.groups[0].items[0].name == "OriginalApp");

    // Mutate catalog, then restore snapshot -> verify rollback pre-restore-* snapshot created
    c.groups[0].items[0].name = "ModifiedApp";
    REQUIRE(WriteFileAtomic(dataRoot / L"catalog.json", SerializeCatalog(c)).ok);

    // Invalid restore must fail cleanly without touching primary catalog
    const std::filesystem::path badBundle = dir.path / L"bad.gdbackup";
    REQUIRE(WriteFileAtomic(badBundle, "{\"backupVersion\":999}").ok);
    CHECK_FALSE(BackupManager::RestoreBackupBundle(dataRoot, badBundle).ok);

    const auto restored = BackupManager::RestoreBackupBundle(dataRoot, manualBundle);
    REQUIRE(restored.ok);
    CHECK_FALSE(restored.rollbackSnapshotPath.empty());
    CHECK(std::filesystem::exists(restored.rollbackSnapshotPath));

    const auto afterRestore = BackupManager::LoadCatalogWithRecovery(dataRoot);
    REQUIRE(afterRestore.ok);
    CHECK(afterRestore.value.groups[0].items[0].name == "OriginalApp");

    // Verify pruning removes old daily snapshots but preserves pre-restore-*.gdbackup
    const std::filesystem::path oldDaily = dataRoot / L"backups" / L"catalog-2020-01-01.json";
    REQUIRE(WriteFileAtomic(oldDaily, "{}").ok);
    const auto oldTime = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 30);
    std::filesystem::last_write_time(oldDaily, oldTime);

    const std::size_t pruned = BackupManager::PruneOldDailyBackups(dataRoot / L"backups", 7);
    CHECK(pruned >= 1);
    CHECK_FALSE(std::filesystem::exists(oldDaily));
    CHECK(std::filesystem::exists(restored.rollbackSnapshotPath));
}

TEST_CASE("file logger supports concurrent UTF-8 writes and flush") {
    TempDirGuard dir("logs");
    const std::filesystem::path logsDir = dir.path / L"logs";
    std::filesystem::path logFile;

    {
        FileLogger logger(logsDir, 14);
        logFile = logger.CurrentLogFilePath();

        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&logger, t]() {
                for (int i = 0; i < 25; ++i) {
                    logger.Info("thread-" + std::to_string(t) + " msg-" + std::to_string(i) +
                                " \xE4\xB8\xAD\xE6\x96\x87");
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        logger.Flush();
    }

    std::string bytes;
    std::string err;
    REQUIRE(ReadFileUtf8(logFile, bytes, err));
    int lines = 0;
    for (char ch : bytes) {
        if (ch == '\n') {
            ++lines;
        }
    }
    CHECK(lines == 100);
}
