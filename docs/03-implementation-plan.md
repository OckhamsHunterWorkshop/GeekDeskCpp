# 03 — 分阶段架构实施计划与 AI 接力指南

> 本文档专供后续接入本项目的工程师与 AI 工具（Claude Code / Codex / Cursor 等）按阶段顺序推进实施。每个 Phase 均给出明确的**模块清单、关键接口、历史踩坑避坑指南、验收标准**以及**可直接复制给下一个 AI 的启动 Prompt**。

---

## 总览路线图

| 阶段 | 核心目标 | 状态 | 关键产出 |
| :--- | :--- | :--- | :--- |
| **Phase 1** | 工程脚手架、纯逻辑内核、Schema v1 存储与备份恢复、单元测试、架构文档 | **已完成** | `src/core/*`, `src/storage/*`, `tests/core_tests.cpp`, `docs/*` |
| **Phase 2** | Win32 外壳（热键/中键/托盘/DWM/启动）+ Direct2D Software 主面板（可看/可用/能搜索/能启动） | **已完成** | `src/platform/*`, `src/ui/*` |
| **Phase 3** | 完整交互管理（分组/图标 CRUD、拖拽排序、文件夹监听、原生设置与备份恢复 UI） | **已完成** | `catalog_mutator`, `folder_watcher`, `drag_drop`, `context_menus`, `item_dialogs`, `settings_window` |
| **Phase 4** | 长稳压测（睡眠唤醒/锁屏/多屏热插拔）、内存与启动审计、便携发行打包 | **已完成** | `--soak-test`, `scripts/package-release.ps1`, 便携发行包 |

---

## 从 GeekDeskNext 迁移沉淀的 6 条硬核经验教训（Phase 2~3 必避之坑）

后续 AI 实现 Win32 / D2D 逻辑时，**务必直接采用以下已验证解法，严禁重踩老坑**：

1. **光标跟随呼出对齐“窗口左上角”而非中心**：
   - 使用 Phase 1 已实现并经过单测的 `core::ComputeTopLeftClampedPosition(cursorPx, winWidthPx, winHeightPx, workAreaPx)`。
   - 必须通过 `GetCursorPos` -> `MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST)` -> `GetMonitorInfoW` 获取目标显示器物理像素 `rcWork` 与 `GetDpiForMonitor`，再调用 `SetWindowPos` 直接写入物理像素坐标，避免在主/副屏不同 DPI 下发生窗口漂移或跨屏缩水。
2. **鼠标滚轮支持切换分组**：
   - 在左侧分组栏区域内滚动滚轮，或当右侧图标区纵向滚动已到顶部/底部（或内容无滚动条）继续向上/向下滚动时，直接切换到上一组/下一组，并高亮选中新分组。
3. **桌面图标外框透明白边/黑框自动剥离（Moat Cropping）**：
   - Windows Shell 返回的 256px/48px `.lnk`/`.exe` 图标中，部分应用会在外圈 `1..5px` 画一层圆角卡片淡色外框，与内部图标之间隔着一条 `alpha <= 8` 的透明护城河（Moat）。
   - 在 WIC 图标提取管道中必须加入检测：外缘 2px 存在不透明像素且 `2px..8px` 内存在完整一圈 `alpha <= 8` 透明环时，自动裁剪至透明环内侧真实图形边界并等比缩放居中，彻底消除图标大小不一的视觉落差。
4. **DWM 材质与“背景不透明度 / 卡片遮罩浓度”必须真实可见**：
   - Acrylic/Mica/MicaAlt 均在 `DwmExtendFrameIntoClientArea(hwnd, {-1,-1,-1,-1})` 下由 DWM 负责后台模糊，D2D 前景必须以**随 `backgroundOpacity` 变化的半透明 tint 颜色清屏**。
   - 卡片与内容区背景使用**高对比不透明基色（深色如 `#202024`、浅色如 `#FFFFFF`）乘以 `cardOpacity`**，禁止对本就只有 6% 不透明度的控件颜色再乘 `cardOpacity`。
5. **休眠恢复 / 锁屏解锁自动重建系统钩子**：
   - Win32 主窗口必须监听 `WM_POWERBROADCAST`（`PBT_APMRESUMESUSPEND` / `PBT_APMRESUMEAUTOMATIC`）与 `WTSRegisterSessionNotification`（`WM_WTSSESSION_CHANGE` -> `WTS_SESSION_UNLOCK`）。
   - 收到唤醒或解锁通知后，延迟 300ms 卸载并重装 `WH_MOUSE_LL` 低级鼠标钩子，重新校验全局热键与托盘图标，避免睡眠后中键失灵。
6. **拖放防重入与事件冒泡隔离**：
   - 处理资源管理器文件拖入添加条目（`DragAcceptFiles` 或 OLE `IDropTarget`）或内部拖拽排序时，同一轮拖放周期只由单一目标命中区域处理，禁止既添加到目标分组又冒泡到窗口全局导致重复导入。

---

## Phase 1：已完成基线清单（Do Not Rewrite）

Phase 1 已完整交付以下模块并通过 `doctest` 单元测试与真实目录只读校验：
- `src/core/utf8.{hpp,cpp}`：UTF-8/UTF-16 转换、安全路径转换、BOM 剥离、UUID 生成。
- `src/core/models.{hpp,cpp}`：全部 Schema v1 枚举与模型结构（100% 对齐 `GeekDeskNext`）。
- `src/core/pinyin_table.hpp` & `src/core/pinyin.{hpp,cpp}`：内置 20,902 个 CJK 码位静态拼音查表与全拼/驼峰首字母键生成。
- `src/core/hotkey_parser.{hpp,cpp}`：热键字符串解析与规范化格式化。
- `src/core/placement_math.{hpp,cpp}`：物理像素缩放、左上角光标跟随钳位、网格列数计算。
- `src/core/catalog_sorter.{hpp,cpp}` & `folder_sync.{hpp,cpp}`：Windows Explorer 自然排序（数字按数值、支持中文 Locale）与关联文件夹同步（保留已有元数据）。
- `src/core/search_engine.{hpp,cpp}`：局部目录分层打分搜索 + 高频条目列表。
- `src/storage/json_codec.{hpp,cpp}`：基于 `yyjson` 的 Schema v1 解析（支持注释/尾逗号/BOM，忽略 `enableExternalEverything`）与 `.gdbackup` 打包编解码。
- `src/storage/atomic_writer.{hpp,cpp}`：Win32 原子写入与单线程 `DebouncedFileWriter`。
- `src/storage/backup_manager.{hpp,cpp}`：布局初始化、每日快照与清理、启动损坏隔离恢复、手工备份导出/校验/恢复（自动创建 `pre-restore-*` 回滚点）。
- `src/storage/logger.{hpp,cpp}`：线程安全按日滚动 UTF-8 日志器。

---

## Phase 2：Win32 外壳与 Direct2D Software 主界面（已完成基线）

### 2.1 待新增模块

```text
src/platform/
├── dpi_helper.hpp / .cpp          # MonitorFromPoint + GetDpiForMonitor / WM_DPICHANGED
├── backdrop.hpp / .cpp            # DwmExtendFrameIntoClientArea + DWMWA_SYSTEMBACKDROP_TYPE + 暗色标题栏 + 圆角
├── hotkey_manager.hpp / .cpp      # RegisterHotKey / UnregisterHotKey（消费 core::ParseHotKey）
├── mouse_hook.hpp / .cpp          # WH_MOUSE_LL 低级鼠标中键监听 + WM_POWERBROADCAST/WTS 恢复钩子
├── fullscreen_detector.hpp / .cpp # SHQueryUserNotificationState 检测全屏免打扰
├── tray_icon.hpp / .cpp           # Shell_NotifyIconW + 托盘右键菜单
├── autostart.hpp / .cpp           # HKCU\Software\Microsoft\Windows\CurrentVersion\Run 开机自启
├── shortcut_resolver.hpp / .cpp   # IShellLinkW 解析 .lnk 真实目标/参数/工作目录/图标来源
└── shell_launcher.hpp / .cpp      # ShellExecuteExW（File/Folder/URL/SystemItem + runAsAdmin）

src/ui/
├── theme_palette.hpp / .cpp       # 解析 AppearanceSettings + 系统深浅色（ShouldAppsUseDarkMode / Registry）
├── icon_cache.hpp / .cpp          # 后台线程提取 Shell/自定义图标 + WIC Moat 裁剪 + PNG 磁盘缓存 + ID2D1Bitmap
├── d2d_renderer.hpp / .cpp        # ID2D1Factory(SINGLE_THREADED) + ID2D1DCRenderTarget(SOFTWARE) + DirectWrite 文本布局
└── main_window.hpp / .cpp         # 无边框主窗口类、鼠标/滚轮/键盘交互、搜索框状态、WM_PAINT、呼出与隐藏策略
```

### 2.2 Phase 2 核心交互契约
1. **无边框 + 窗口圆角/拖动**：处理 `WM_NCHITTEST` 使标题栏空白区支持 `HTCAPTION` 拖动、边缘支持 `HTRIGHT`/`HTBOTTOM`/`HTBOTTOMRIGHT` 缩放并写回 `settings.window.{width, height}`（经过 `DebouncedFileWriter` 合并写盘）。
2. **自定义搜索框（Pure Direct2D + Win32 IME/按键消息，而非丑陋的原生 Win32 `Edit` 控件白底！）**：
   - 或在搜索栏区域精确内嵌隐藏焦点机制 + `WM_CHAR`/`WM_IME_COMPOSITION`/`ImmGetCompositionStringW` 原生输入法候选窗定位（`ImmSetCompositionWindow`），保证拼音输入中文搜应用体验丝滑且完全融入磨砂玻璃主题。
3. **键盘快速导航**：`Ctrl+F` 或 `TypeToSearch` 直接输入激活搜索，`Up/Down/Left/Right` 移动选中卡片，`Enter` 直接启动（若当前无精确选中则启动首条命中项）并更新 `launchCount++` 与 `lastLaunchedAt` 后保存；`Esc` 先清空搜索框，再隐藏窗口。
4. **隐藏策略落地**：支持 `OnDeactivate`（`WM_ACTIVATE` 变为 `WA_INACTIVE` 且非模态弹窗/右键菜单时隐藏）、`AfterLaunch`、`ManualOnly`。
5. **验收标准**：
   - 可呼出/隐藏主面板（`Ctrl+Q` 与鼠标中键均有效，且随鼠标左上角对齐弹出）。
   - 加载真实 `I:\tools\GeekDeskNext\data` 后正常展示全部 9 个分组、78 个条目与高清无黑框图标。
   - 搜索中文全拼/首字母/英文瞬间出结果，按回车或点击成功启动应用（包括需提权的程序与快捷方式）。
   - 进程空闲工作集内存实测目标 **< 20 MB**，全程无 D3D11/DXGI 驱动加载。

---

## Phase 3：完整编辑能力、文件夹监听与原生设置/备份界面（已完成基线）

### 3.1 已交付模块
- `src/core/catalog_mutator.hpp / .cpp`：分组/条目 CRUD、去重校验、`LinkedFolder` 保护、同组重排与跨分组移动。
- `src/platform/folder_watcher.hpp / .cpp`：基于 `ReadDirectoryChangesW` 重叠 I/O 监听全部 `GroupKind::LinkedFolder` 目录变化，400ms 防抖同步并保留已有条目元数据。
- `src/platform/file_dialogs.hpp / .cpp`：Windows COM `IFileOpenDialog` / `IFileSaveDialog` 文件、文件夹与保存弹窗，以及 `explorer.exe /select` 定位。
- `src/ui/context_menus.hpp / .cpp` & `src/ui/item_dialogs.hpp / .cpp`：Win32 `TrackPopupMenuEx` 分组与条目右键菜单、轻量级深浅色文本输入与条目属性编辑弹窗。
- `src/ui/drag_drop.hpp / .cpp`：`IDropTarget` 支持外部从桌面/资源管理器拖入文件/文件夹/`.lnk`/`.url`（含重入保护与视觉高亮），配合 `MainWindow` 鼠标捕获完成内部图标与分组拖拽重排。
- `src/ui/settings_window.hpp / .cpp`：3 标签页原生 Win32 设置与备份恢复中心：
  - 外观（材质 `Acrylic/Mica/MicaAlt/Wallpaper/Gradient`、背景不透明度、卡片遮罩浓度、主题、圆角、图标大小与壁纸选择）
  - 呼出与行为（热键校验录入、中键开关、光标左上角跟随、全屏屏蔽、隐藏策略、单击/双击、排序与开机自启）
  - **数据安全与备份恢复中心**：展示统计信息、配置每日保留天数、**一键导出 `.gdbackup`**、**选择 `.gdbackup` 预检条目数 -> 一键恢复（自动生成 `pre-restore-*` 回滚包）**、`.gdbackup` 快照列表直接回滚及目录快捷打开。

---

## Phase 4：稳定压测、内存审计与发布打包（已完成基线）

### 4.1 已交付与验证项
1. **资源泄漏审计与修复**：清理 `SettingsWindow` 销毁阶段 Font/Brush GDI 句柄、`D2DRenderer` 独立文本裁剪 sign，以及抑制压测期间无效落盘（`suppressDiskPersist_`）；新增 `--soak-test [N]` 入口自动执行 `Show -> Paint -> 切组 -> 搜索 -> 材质/透明度轮换 -> Hooks 刷新 -> 设置弹窗开关 -> Hide` 闭环压测。
2. **500 轮实测结果（9 分组 / 78 条目）**：
   - **Cold（含首次全部 Shell 图标提取 + 500 轮）**：`GDI 97 -> 95`，`USER 49 -> 50`，`Handles 469 -> 469`，`Private Bytes 40.80 MB -> 40.80 MB`；
   - **Warm（磁盘 PNG 缓存 + 500 轮）**：`GDI 59 -> 63`，`USER 49 -> 50`，`Handles 426 -> 426`，`Working Set 36.62 MB -> 37.62 MB`，`Private Bytes 19.18 MB -> 20.27 MB`；
   - 启动单分组 `--smoke-ui`：`Private Bytes 8.70 MB`，`Working Set 22.66 MB`，全程 **GPU UMD Loaded = false**、**Everything Loaded = false**。
3. **损坏自愈回归演练与单测**：单元测试扩展至 **16 / 16（210 断言全绿）**，覆盖多级损坏每日备份降级自愈与 `FolderWatcher` 连续启停压测；端到端人工损坏 `catalog.json` 启动自愈演练成功隔离 `.corrupt.*.bak` 并恢复全部 9 个分组及日志审计记录。
4. **便携发行脚本**：已交付 `scripts/package-release.ps1`，全自动执行洁净编译、`ctest`、`objdump -p` 导入表红线审计、200 轮压测并输出仅含 `GeekDeskCpp.exe`（2,748,416 字节）与 `README.txt` 的便携压缩包 `GeekDeskCpp-v0.1.0-win64-portable.zip`（1,179,868 字节）及 `.sha256` 校验文件。

---

## 给下一个 AI 的可直接复制 Prompt（按 Phase 使用）

### 复制 Prompt — 启动 Phase 2
```text
请先完整阅读 W:\GeekDeskCpp\AGENTS.md、W:\GeekDeskCpp\README.md，以及 W:\GeekDeskCpp\docs\ 下 01~04 号全部文档，充分理解本项目的“做减法/Mode A Direct2D Software/Schema v1 兼容”原则与历史踩坑点。

你的任务是完整实现 GeekDeskCpp 的 **Phase 2（Win32 系统外壳 + Direct2D Software 主窗口 + WIC 图标缓存）**：
1. 严格保持 src/core/ 与 src/storage/ 的现有测试全绿，禁止引入 D3D11/DXGI/WebView2/Everything/旧二进制迁移。
2. 实现 src/platform/ 全部系统外壳模块（DPI、DWM Backdrop、RegisterHotKey、WH_MOUSE_LL 中键呼出含睡眠/锁屏恢复、全屏检测、托盘图标、IShellLinkW 解析、ShellExecuteExW 启动）与 src/ui/（ThemePalette、WIC Moat 裁剪图标缓存、Direct2D Software + DirectWrite 渲染、主窗口交互、搜索框与中文 IME 支持、滚轮切分组、光标左上角跟随定位）。
3. 更新 W:\GeekDeskCpp\CMakeLists.txt（保持 -Wall -Wextra -Wpedantic -Werror 零警告）并在 tests/ 中补充新增纯逻辑模块的单元测试。
4. 完成后构建 mingw-release，运行 ctest 全部通过，检查 objdump DLL 依赖无多余运行时，并用实际数据启动/烟雾验证后，在项目内输出清晰的修改清单与内存占用观察结果。
```

### 复制 Prompt — 启动 Phase 3
```text
请阅读 W:\GeekDeskCpp\AGENTS.md 及 W:\GeekDeskCpp\docs\03-implementation-plan.md，在已完成的 Phase 1~2 基础上实施 **Phase 3（完整编辑能力、LinkedFolder 监听与原生设置/备份恢复界面）**：
1. 实现 ReadDirectoryChangesW 关联文件夹监听（防抖同步并保留已有条目属性）。
2. 实现分组与条目的完整增删改、右键菜单、文件拖入（IDropTarget，防重入重复添加）及自定义排序拖拽。
3. 实现直观精简的设置与备份恢复界面，支持实时调节材质/透明度预览、热键录制、开机自启，以及一键导出 .gdbackup、预检并恢复备份（自动生成 pre-restore-* 回滚快照）。
4. 保持 zero-warning 编译与全部 ctest 通过。
```

### 复制 Prompt — 启动 Phase 4
```text
请阅读 W:\GeekDeskCpp\AGENTS.md 及 W:\GeekDeskCpp\docs\03-implementation-plan.md，执行 GeekDeskCpp 的 **Phase 4（长稳压测、内存/句柄泄漏审计、多屏高分屏验证与便携发行打包）**：
1. 检查并压测睡眠唤醒、锁屏解锁、反复呼出隐藏 500 次后的 Handles/GDI/Working Set 是否稳定且无泄漏。
2. 审核全部边界异常与损坏恢复日志路径，补齐回归测试。
3. 整理 scripts/package-release.ps1 生成便携式单文件发行包，并更新 README.md 状态为正式可用。
```
