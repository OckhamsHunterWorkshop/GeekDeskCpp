# 04 — 数据契约、持久化与备份恢复规范

## 1. 便携式目录布局

所有运行时数据默认存于可执行文件同级的 `data/` 目录（单目录即完整绿色便携状态）：

```text
<AppDir>\data\
├── settings.json                     # Schema v1 应用设置
├── catalog.json                      # Schema v1 分组与启动条目目录
├── assets\                           # 用户自定义图标、壁纸等静态资源
├── cache\
│   └── icons\                        # WIC 提取并裁边后缓存的 PNG 图标
├── backups\
│   ├── settings-YYYY-MM-DD.json      # 每日自动滚动备份
│   ├── catalog-YYYY-MM-DD.json       # 每日自动滚动备份
│   └── pre-restore-YYYYMMDD-HHMMSS-mmm.gdbackup  # 恢复前强制生成的安全回滚包
└── logs\
    └── geekdesk-YYYY-MM-DD.log       # 按天滚动 UTF-8 运行日志
```

---

## 2. Schema v1 兼容契约

### 2.1 `settings.json`
与 `GeekDeskNext` Schema v1 完全一致：
- 顶层：`schemaVersion` (`1`)、`window`、`activation`、`behavior`、`appearance`、`state`
- 枚举均以 PascalCase 字符串序列化（解析器亦容错支持整型枚举）：
  - `hidePolicy`: `"OnDeactivate" | "AfterLaunch" | "ManualOnly"`
  - `searchActivation`: `"HotKeyOnly" | "TypeToSearch"`
  - `groupSort` / `itemSort`: `"Custom" | "LaunchCountAsc" | "LaunchCountDesc" | "NameAsc" | "NameDesc"`
  - `theme`: `"System" | "Light" | "Dark"`
  - `backdrop`: `"Acrylic" | "Mica" | "MicaAlt" | "Wallpaper" | "Gradient"`
- **历史废弃字段处理**：若 `behavior` 中存在 `GeekDeskNext` 留下的 `"enableExternalEverything"`，读取阶段静默忽略、不报错；写回时仅输出 `GeekDeskCpp` 精简后的有效字段。

### 2.2 `catalog.json`
- 顶层：`schemaVersion` (`1`)、`groups` 数组
- `CategoryGroup`：`id` (UUID), `name`, `kind` (`"Manual" | "LinkedFolder"`), `linkedFolderPath`, `iconGlyph`, `iconColor`, `legacyGeometryPath`, `items`
- `LaunchItem`：`id` (UUID), `name`, `kind` (`"File" | "Folder" | "Url" | "SystemItem"`), `targetPath`, `lnkPath`, `relativePath`, `arguments`, `workingDirectory`, `runAsAdmin`, `launchCount`, `lastLaunchedAt`, `customIconFile`, `tooltip`

---

## 3. `.gdbackup` 单文件备份包规范

手动备份与回滚快照均使用 UTF-8 JSON 格式（默认扩展名 `.gdbackup`）：

```json
{
  "backupVersion": 1,
  "appName": "GeekDeskCpp",
  "createdAt": "2026-11-20T09:00:00Z",
  "settings": { "...Schema v1 settings.json..." },
  "catalog": { "...Schema v1 catalog.json..." }
}
```

### 恢复安全协议（必须严格按序执行）
1. **预检（`InspectBackupBundle`）**：读取备份文件并完整校验 `backupVersion == 1`、`settings.schemaVersion == 1`、`catalog.schemaVersion == 1`；若校验失败，立即中止并返回错误，**不改动 `data/` 内任何文件**。
2. **回滚点生成（`pre-restore-*`）**：若当前 `dataRoot` 已存在 `settings.json` 或 `catalog.json`，先将其导出至 `data/backups/pre-restore-YYYYMMDD-HHMMSS-mmm.gdbackup`。
3. **原子切换**：使用 `WriteFileAtomic` 依次原子替换 `settings.json` 与 `catalog.json`。

---

## 4. 原子写入与自愈机制

1. **原子写（`WriteFileAtomic`）**：
   - 先写同目录临时文件 `<target>.tmp.<pid>.<seq>` -> 循环写全字节 -> `FlushFileBuffers` 落盘 -> `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`（遇短暂杀软/索引锁自动重试 5 次）。
2. **启动自愈（`LoadSettingsWithRecovery` / `LoadCatalogWithRecovery`）**：
   - 若主文件 JSON 解析失败或版本非法：先将损坏文件重命名为 `<stem>.corrupt.<timestamp>.bak` 留证，再从 `backups/` 中按日期倒序搜索最新可通过校验的每日快照并自动写回主文件恢复启动。
3. **备份清理边界**：
   - `PruneOldDailyBackups` 仅清理匹配 `<stem>-YYYY-MM-DD.json` 且超过 `backupRetentionDays` 的每日自动备份，**永不删除用户手导出的 `.gdbackup` 与 `pre-restore-*` 安全回滚点**。
