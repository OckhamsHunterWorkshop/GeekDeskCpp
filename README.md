# GeekDeskCpp

**极简、原生、极致效率的 Windows 10/11 桌面快速启动器（C++20 + Win32 + Direct2D Software）。**  
*A minimal, ultra-fast native Windows 10/11 desktop launcher built with C++20, Win32, and Direct2D Software.*

> **设计哲学：做减法，少即是多。**  
> 砍掉动辄 110~330 MB 内存的 .NET/WPF 运行时、砍掉臃肿的 Everything 全盘检索与过时迁移包袱，只保留启动器最核心的 **毫秒级呼出、离线拼音直达、文件夹联动、Win10/Win11 统一分层视效与可靠的数据备份恢复**。

---

## 核心特性 / Highlights

- **原生零依赖单文件**：MinGW C++20 静态链接，单文件约 **2.65 MB**（ZIP 约 **1.14 MB**）；无 `.NET`、无 `WebView2`、无 `Everything64.dll`、**0 MB 显存占用**（不加载 D3D11/DXGI GPU 用户态驱动）。
- **Win10 / Win11 统一分层合成（Mode A）**：采用 `WS_EX_LAYERED` + `UpdateLayeredWindow` + Direct2D Software（WARP），支持真实 **亚克力透明 (Acrylic)**、桌面壁纸模糊 **云母 / 深云母 (Mica / MicaAlt)**、自定义壁纸与渐变、`0~24 px` 抗锯齿圆角、卡片遮罩浓度，以及 **界面字体 / 字号** 实时调节。
- **跨屏一致性与顺手交互**：支持全局热键（默认 `Ctrl+Q`）与鼠标中键光标左上角对齐呼出；跨 2K / 1080p 不同 DPI 屏幕呼出时自动保持 **每行图标列数一致**；右上角工具栏支持悬停文字提示。
- **中英双语界面（zh-CN / en-US）**：支持跟随系统或一键切换简体中文 / English，设置窗、主界面、右键菜单、弹窗与托盘菜单实时同步。
- **离线内置拼音检索**：编译期嵌入 `U+4E00..U+9FA5` 汉字拼音表，支持全拼、首字母（含英文驼峰缩写如 `vsc` → `Visual Studio Code`）与高频权重排序。
- **Schema v1 无缝互通 & 原子数据安全**：直接兼容 `GeekDeskNext` 的 `settings.json` 与 `catalog.json`；全量原子写入（`WriteFile` + `FlushFileBuffers` + `MoveFileExW`）、每日快照、启动损坏自动隔离恢复，以及 `.gdbackup` 单文件导出/回滚恢复。

---

## 实测资源与稳定性指标（Windows 11 x64）

| 检查项目 | 实测结果 |
| :--- | :--- |
| 单文件可执行体积 | `GeekDeskCpp.exe` 约 **2.65 MB**；便携压缩包约 **1.14 MB** |
| 外部依赖审计（`objdump -p`） | 仅依赖 Windows 系统 DLL；**无** MinGW 运行时 DLL、**无** GPU 硬件用户态驱动、**无** `Everything*.dll` |
| 冷启动 + 首帧渲染（`--smoke-ui`） | **~92 ms**，Private Bytes **~11–12 MB**（Working Set ~25 MB） |
| 真实目录加载建索引（9 分组 / 78 条目） | **~15 ms** |
| 200 轮高强度压测（`--soak-test 200`） | GDI `63 -> 69`、USER `48 -> 49`、Handles `351 -> 351`、Private Bytes `14.28 MB -> 14.86 MB`（零泄漏） |
| 日常驻留使用（78 条目 + 图标缓存） | 私有内存约 **22–24 MB**，空闲 CPU **0%**，显存 **0 MB** |

---

## 下载与首次使用说明 / Quick Start

1. 前往 [Releases](../../releases) 下载最新的 `GeekDeskCpp-v*-win64-portable.zip`。
2. **解压到独立文件夹**（例如 `D:\Tools\GeekDeskCpp\`，请勿直接在压缩包内双击运行，以免同级 `data\` 目录无法写入）。
3. 双击 `GeekDeskCpp.exe` 启动（默认热键 `Ctrl+Q`，也可在屏幕任意位置点击鼠标中键呼出）。
   - **首次运行提示**：由于个人开源软件尚未购买商业代码签名证书，Windows 10/11 首次运行可能弹出 SmartScreen **“Windows 已保护你的电脑 / 未知发布者”**，点击 **「更多信息」→「仍要运行」**（*More info → Run anyway*）即可。可核对 Release 中附带的 `.sha256` 校验值确认文件完整。
4. **从 `GeekDeskNext` 迁移**：先退出 `GeekDeskCpp.exe`，将原 `GeekDeskNext\data\` 内的 `catalog.json`、`settings.json` 与 `assets\` 复制到 `GeekDeskCpp.exe` 同级 `data\` 目录即可。

---

## 本地构建与测试 / Build from Source

环境要求：Windows 10/11 x64，CMake >= 3.24，Ninja，MinGW-w64 UCRT (`g++` C++20) 或 MSVC。

```powershell
# 1. 配置与编译 Release 版本（-Wall -Wextra -Wpedantic -Werror 零警告）
cmake --preset mingw-release
cmake --build --preset mingw-release

# 2. 运行全部单元测试（doctest）
ctest --preset mingw-release --output-on-failure

# 3. 运行 Mode A 渲染烟雾测试 / 200 轮泄漏压测 / 只读数据校验
.\build\mingw-release\GeekDeskCpp.exe --smoke-ui --data-dir "<temp-data-dir>" | Out-Host
.\build\mingw-release\GeekDeskCpp.exe --soak-test 200 --data-dir "<temp-data-dir>" | Out-Host
.\build\mingw-release\GeekDeskCpp.exe --verify-data "<your-data-dir>" | Out-Host

# 4. 一键构建、审计、压测并生成便携发行包（输出至 dist\）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-release.ps1
```

---

## 文档导航

| 文档 | 内容说明 |
| :--- | :--- |
| [`AGENTS.md`](./AGENTS.md) | 仓库架构红线、目录职责与验证规范 |
| [`docs/01-prd-and-scope.md`](./docs/01-prd-and-scope.md) | 产品背景、范围边界与资源预算 |
| [`docs/02-architecture-and-design.md`](./docs/02-architecture-and-design.md) | 4 层架构、线程模型、Mode A 渲染管线与内存控制策略 |
| [`docs/03-implementation-plan.md`](./docs/03-implementation-plan.md) | Phase 1~4 实施计划与 Win32/D2D 关键经验 |
| [`docs/04-data-and-backup-spec.md`](./docs/04-data-and-backup-spec.md) | JSON Schema v1 字段契约、原子写、自动恢复与 `.gdbackup` 备份规范 |

---

## 致敬 / Acknowledgements

本项目的设计灵感致敬开源项目 [BookerLiu/GeekDesk](https://github.com/BookerLiu/GeekDesk)（基于 C++20 + Win32 + Direct2D 从零重写，聚焦极简与低内存占用）。

