# GeekDeskCpp

**极简、原生、极致效率的 Windows 桌面快速启动器（C++20 + Win32 + Direct2D Software）。**

> **设计哲学：做减法，少即是多。**  
> 砍掉动辄 270~330MB 内存的 .NET/WPF 运行时、砍掉臃肿的 Everything 外挂搜索、砍掉过时的二进制迁移与冗余小工具，只保留启动器最核心的 **秒级呼出、拼音直达、文件夹联动与可靠的数据备份恢复**。

---

## 项目现状与实施阶段

- [x] **Phase 1 — 工程底座、核心逻辑、Schema v1 兼容存储、备份恢复与自动化测试**（已完成）
- [x] **Phase 2 — Win32 系统外壳（DWM 材质 / 热键 / 中键钩子 / 托盘 / 启动器）与 Direct2D Software 主面板**（已完成）
- [x] **Phase 3 — 完整交互编辑（分组/条目增删改、拖拽排序、文件夹监听、原生设置与备份恢复界面）**（已完成）
- [x] **Phase 4 — 长稳压测、内存/冷启动审计与便携发行打包**（已完成）

---

## 实测资源与稳定性指标（Windows 11 x64，9 分组 / 78 条目实测）

| 检查项目 | 实测结果 |
| :--- | :--- |
| 单文件可执行体积 | **`GeekDeskCpp.exe` 2,748,416 字节（约 2.62 MB）**；便携压缩包 `1,179,868` 字节（约 1.13 MB） |
| 外部依赖审计（`objdump -p`） | 仅依赖 Windows 系统 DLL；**无** MinGW 运行时 DLL、**无** GPU 硬件用户态驱动、**无** `Everything*.dll` |
| 单分组启动渲染（`--smoke-ui`） | **Private Bytes `8.70 MB`**，Working Set `22.66 MB` |
| 热缓存日常 500 轮切换压测（全 9 组 / 78 项） | **Private Bytes `19.18 MB -> 20.27 MB`**，Working Set `36.62 MB -> 37.62 MB`，进程句柄 `426 -> 426`（零增长） |
| 首次冷提取 500 轮压测（含全部 Shell 图标提取 + 20 次设置弹窗开关） | GDI `97 -> 95`，USER `49 -> 50`，句柄 `469 -> 469`，Private Bytes `40.80 MB -> 40.80 MB` |
| 自动化单元测试（`doctest`） | **16 / 16 测试用例、210 / 210 断言 100% 通过** |

---

## 核心特性（目标架构）

- **原生零依赖单文件**：静态链接 MinGW C++20 运行时，无 `.NET`、无 `WebView2`、无 `Everything64.dll`，不初始化 D3D11/DXGI 进程内驱动上下文（Mode A：DWM + Direct2D Software）。
- **Schema v1 无缝互通**：完全读写 `GeekDeskNext` 格式的 `settings.json` 与 `catalog.json`，静默忽略已废弃字段（如 `enableExternalEverything`）。
- **离线内置拼音检索**：编译期嵌入完整 `U+4E00..U+9FA5` 汉字拼音查表，零 IO 启动、零外挂，支持全拼、首字母（含英文驼峰缩写如 `vsc` -> `Visual Studio Code`）、模糊子序列与高频权重排序。
- **高可靠数据安全**：全量原子写入（`WriteFile` + `FlushFileBuffers` + `MoveFileExW`）、防抖合并落盘、每日滚动快照、启动损坏自动隔离与回滚恢复、`.gdbackup` 单文件导出/校验/一键恢复（自动生成 `pre-restore-*` 安全快照）。

---

## 快速构建与测试

环境要求：Windows 10/11 x64，CMake >= 3.24，Ninja，MinGW-w64 UCRT (`g++` C++20) 或 MSVC。

```powershell
Set-Location W:\GeekDeskCpp

# 1. 配置与编译 Release 版本（-Wall -Wextra -Wpedantic -Werror 零警告）
cmake --preset mingw-release
cmake --build --preset mingw-release

# 2. 运行全部单元测试（doctest）
ctest --preset mingw-release --output-on-failure

# 3. 使用现有 GeekDeskNext 数据目录进行只读兼容性校验
.\build\mingw-release\GeekDeskCpp.exe --verify-data "I:\tools\GeekDeskNext\data" | Out-String

# 4. 运行 Mode A 渲染烟雾测试、N 轮长稳泄漏压测或指定数据目录启动桌面主界面
.\build\mingw-release\GeekDeskCpp.exe --smoke-ui --data-dir "<temp-data-dir>" | Out-String
.\build\mingw-release\GeekDeskCpp.exe --soak-test 500 --data-dir "<temp-data-dir>" | Out-String
.\build\mingw-release\GeekDeskCpp.exe --data-dir "<your-data-dir>"

# 5. 一键构建并生成单文件便携发行包（输出至 dist\）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-release.ps1
```

若使用 MSVC 工具链（需在 VS Developer PowerShell 中执行）：

```powershell
Set-Location W:\GeekDeskCpp
cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release --output-on-failure
```

---

## 文档导航

| 文档 | 内容说明 |
| :--- | :--- |
| [`AGENTS.md`](./AGENTS.md) | 给所有 AI / 开发者看的仓库硬性红线、目录职责与验证规范 |
| [`docs/01-prd-and-scope.md`](./docs/01-prd-and-scope.md) | 产品背景、严格范围边界（做什么 vs 坚决不做什么）与资源预算 |
| [`docs/02-architecture-and-design.md`](./docs/02-architecture-and-design.md) | 4 层架构、线程模型、Mode A 渲染管线与内存控制策略 |
| [`docs/03-implementation-plan.md`](./docs/03-implementation-plan.md) | Phase 1~4 详细开发计划、Win32/D2D 关键踩坑经验与 **可直接复制的 AI 接力 Prompt** |
| [`docs/04-data-and-backup-spec.md`](./docs/04-data-and-backup-spec.md) | JSON Schema v1 字段契约、原子写、自动恢复与 `.gdbackup` 备份规范 |
