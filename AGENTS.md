# AGENTS.md — AI 协作开发规范与架构约束

本仓库为 **GeekDeskCpp**：使用原生 **C++20 + Win32 + DWM + Direct2D Software (Mode A)** 构建的极简桌面启动器，追求 **做减法 · 少即是多 · 极致轻量与稳定**。

任何接手后续 Phase 的 AI Agent（Claude Code / Codex / Cursor / Antigravity 等）在修改代码前，**必须先阅读并严格遵守本文档与 `docs/` 下全部设计文档**。

---

## 1. 不可逾越的架构红线（Hard Constraints）

1. **严禁引入已砍掉的臃肿功能**：
   - **禁止** 集成 `Everything`（包括 `Everything.exe`、`Everything64.dll`、IPC 查询或外部全盘搜索）。搜索仅限本地已配置的目录项（`catalog.json` 中各分组条目）。
   - **禁止** 实现旧版 GeekDesk 2.x 的 `Data`（`BinaryFormatter` / NRBF）二进制迁移代码。
   - **禁止** 加入待办（ToDo）、分组密码锁、秒表窗口（SecondsWindow）、取色器、网络检查或自动更新模块。
2. **渲染模式锁定为 Mode A（零进程内 GPU 驱动初始化）**：
   - 使用 **Win32 HWND + DWM 系统材质（`DwmExtendFrameIntoClientArea` + `DWMWA_SYSTEMBACKDROP_TYPE`）+ Direct2D Software（`D2D1_RENDER_TARGET_TYPE_SOFTWARE` / `ID2D1DCRenderTarget`）+ DirectWrite + WIC**。
   - **禁止** 创建 `ID3D11Device`、`IDXGISwapChain`、`IDCompositionDevice` 或引入 WebView2 / WinUI / Qt / WPF。
3. **数据 Schema v1 双向兼容红线**：
   - 必须完全兼容 `GeekDeskNext` 的 Schema v1 目录结构（`settings.json`、`catalog.json`、`assets/`、`cache/icons/`、`backups/`、`logs/`）。
   - 读取未知历史字段（例如 `enableExternalEverything`）时必须**静默忽略并正常加载**，不得报错。
   - 所有写入操作**必须**通过 `storage::WriteFileAtomic`（临时文件 + `FlushFileBuffers` + `MoveFileExW`）或 `storage::DebouncedFileWriter` 进行。
4. **Windows UTF-8 路径规范**：
   - 内部所有 `std::string` 一律按 **UTF-8** 处理。
   - **严禁**直接用窄字符 `std::string` 构造 `std::filesystem::path(str)`（Windows MinGW/MSVC 默认按 ACP/GBK 解释会损坏中文路径）。
   - 必须统一调用 `geekdesk::core::utf8::PathFromUtf8()` 与 `PathToUtf8()`。
5. **依赖与静态链接约束**：
   - 首选 MinGW-w64 UCRT (`g++` C++20) + Ninja + CMake Presets 构建。
   - 最终 `GeekDeskCpp.exe` 必须保持**单文件独立运行**，静态链接 `libgcc` / `libstdc++` / `libwinpthread`（MinGW 下使用 `-static -static-libgcc -static-libstdc++`），禁止依赖外部运行时 DLL。
   - 新增任何 `third_party/` 依赖必须为 MIT/BSD/Apache 等宽松开源协议、无构建脚本污染，并通过 `SYSTEM` include 引入，不得破坏主工程零警告构建。

---

## 2. 编译警告与代码质量门禁

- 主工程（`geekdesk_core`、`GeekDeskCpp`、`geekdesk_tests`）启用：
  - GCC/MinGW：`-std=c++20 -Wall -Wextra -Wpedantic -Werror`
  - MSVC：`/std:c++20 /W4 /WX /utf-8 /EHsc`
- **提交标准**：任意阶段改动必须达到 **0 Warning / 0 Error**、`ctest` **100% 通过**。
- 分层单向依赖规则：
  - `src/core/`：纯领域模型、算法、拼音、排序、查找、坐标数学（**不依赖 `HWND` / `ID2D1*` / 文件持久化**）。
  - `src/storage/`：JSON 编解码（`yyjson`）、原子写入、防抖合并写入、备份/恢复与日志（依赖 `core`，不依赖 UI）。
  - `src/platform/`（Phase 2+）：Win32 窗口外壳、热键、鼠标钩子、托盘、Shell 启动、DWM 材质、全屏检测。
  - `src/ui/`（Phase 2+）：Direct2D/DirectWrite 渲染、WIC 图标缓存与透明边框裁剪、设置/管理界面（通过事件回调驱动 `core`/`storage`，禁止把持久化写散落在消息处理里）。

---

## 3. 仓库结构与文档索引

```text
W:\GeekDeskCpp\
├── AGENTS.md                        # 本规范（AI 必读）
├── README.md                        # 项目说明、构建指南与进度看板
├── CMakeLists.txt                   # CMake 根配置
├── CMakePresets.json                # mingw-debug / mingw-release / msvc-release
├── .clang-format / .gitignore
├── third_party/
│   ├── yyjson/                      # C99 高性能 JSON 解析/序列化 (MIT)
│   └── doctest/                     # 单头文件 C++ 单元测试框架 (MIT)
├── resources/
│   ├── AppIcon.ico                  # 应用图标
│   ├── app.manifest                 # PerMonitorV2 DPI + UTF-8 ActiveCodePage
│   ├── app.rc                       # 纯 ASCII Windows 资源脚本
│   └── resource.h
├── src/
│   ├── core/                        # 纯逻辑内核（模型/UTF-8/拼音/热键/坐标/排序/同步/搜索）
│   ├── storage/                     # JSON Codec/原子写/防抖/备份恢复/日志
│   ├── platform/                    # [Phase 2+] Win32 系统集成层
│   ├── ui/                          # [Phase 2+] Direct2D Software 渲染与界面
│   └── main.cpp                     # 入口（Phase 1 支持 --verify-data 与数据自举）
├── tests/
│   └── core_tests.cpp               # 核心与存储单元测试
└── docs/
    ├── 01-prd-and-scope.md          # 产品需求、性能预算与删减清单
    ├── 02-architecture-and-design.md# 分层架构、线程模型与 Mode A 渲染设计
    ├── 03-implementation-plan.md    # 分阶段实施蓝图 + 历史经验教训 + 可复制 AI Prompt
    └── 04-data-and-backup-spec.md   # Schema v1 数据契约、备份恢复与灾难恢复规范
```

---

## 4. 每次改动后的标准验证命令

请在项目根目录 `W:\GeekDeskCpp` 执行（注意：`cmake --preset` 依赖当前工作目录）：

```powershell
Set-Location W:\GeekDeskCpp
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release --output-on-failure
```

验证独立可执行文件无 MinGW / GPU / Everything 运行时 DLL 依赖，且只读校验真实配置通过：

```powershell
objdump -p .\build\mingw-release\GeekDeskCpp.exe | Select-String "DLL Name"
.\build\mingw-release\GeekDeskCpp.exe --verify-data "I:\tools\GeekDeskNext\data" | Out-String
```

---

## 5. 文档读取顺序建议

接手新 Phase 时，按以下顺序阅读：
1. `AGENTS.md`（确认红线与分层）
2. `docs/03-implementation-plan.md`（定位当前 Phase 任务清单、接口契约与已知踩坑点）
3. `docs/02-architecture-and-design.md`（核对线程模型与 D2D Mode A 渲染细节）
4. `docs/04-data-and-backup-spec.md`（涉及设置/目录/备份改动时必读）
