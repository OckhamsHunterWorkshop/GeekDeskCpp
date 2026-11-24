# GeekDeskCpp

**English** | [简体中文](./README.zh-CN.md)

> 🏛️ Crafted by **[Ockham's Hunter Workshop](https://github.com/OckhamsHunterWorkshop)**  
> *"Fear the creeping bloat. Entia non sunt multiplicanda praeter necessitatem."*

**A minimal, ultra-fast native Windows 10/11 desktop launcher built with C++20, Win32, and Direct2D Software.**

> **Design Philosophy: Less is more.**  
> Stripping away the 110–330 MB `.NET`/WPF runtime, external Everything indexing overhead, and legacy bloat—leaving only instant reflex-like launch, offline Pinyin & acronym search, live folder sync, unified Win10/Win11 layered glass visuals, and rock-solid backup/recovery.

<img width="824" height="620" alt="GeekDeskCpp" src="./GeekDeskCppDemo.png" />

---

## Highlights

- **Zero-Dependency Native Single Binary**: Statically linked MinGW C++20 executable (~**2.88 MB**). No `.NET`, no `WebView2`, no `Everything64.dll`, and **0 MB VRAM** (zero D3D11/DXGI hardware GPU driver loading).
- **Isolated System File Dialogs (`--dialog-helper`)**: Native Windows Open/Save/Folder dialogs execute in an ephemeral child process via pipes and exit immediately after selection—preventing `explorerframe.dll`, `d3d11`/`dxgi`, and third-party cloud shell extensions (OneDrive, Adobe, etc.) from bloating or destabilizing the main launcher process.
- **Unified Win10 / Win11 Layered Compositing (Mode A)**: Built on `WS_EX_LAYERED` + `UpdateLayeredWindow` + Direct2D Software (WARP), supporting real **Acrylic**, wallpaper-blurred **Mica / MicaAlt**, custom wallpapers & gradients, `0–24 px` anti-aliased corner radius, card opacity, and live **UI font family / size** tuning.
- **Desktop Drag-and-Drop Archiving & On-Demand Rename Recovery**:
  - Dropping shortcuts, files, or folders from the User Desktop (including OneDrive Desktop) or Public Desktop automatically pops a lightweight 2-choice menu: **Move to Archive (Clean Desktop)** or **Add Entry Only (Keep on Desktop)**—asking once per batch drop;
  - Physical files are moved safely via native Windows `IFileOperation` into `<ArchiveDir>\<GroupName>\` (supporting Public Desktop UAC elevation and collision renaming). Renaming, moving across groups, or removing entries inside GeekDeskCpp never deletes original files, and right-clicking archived items offers one-click **Restore to Desktop**;
  - **On-Demand NTFS File ID Recovery & Missing-File Guidance**: With **0% background scanning**, renaming or moving a tracked file/folder on the same NTFS volume automatically resolves its new path and updates default filenames on click; files moved to the Recycle Bin or permanently deleted are safely intercepted with a themed guidance dialog (**Relocate / Remove / Cancel**);
  - Sidebar bottom quick-action buttons (**`+ Group` / `Link`**) and timer-free cursor-following group drag reordering (in-place ghosting, insertion bar, and floating preview);
  - **2-line wrapped item labels** with strict horizontal icon alignment across every row;
  - Unified dark/light Direct2D rounded popup menus across groups, items, and tray; **Open File Location** uses native `SHOpenFolderAndSelectItems` to jump straight to the real `.exe` location;
  - Global hotkey (default `Ctrl+Q`) and middle-mouse toggle aligned to the cursor's top-left corner, consistent column counts across mixed-DPI monitors, and toolbar hover tooltips.
- **Bilingual UI & Built-in Version Visibility**: Instant live switching between **English** and **简体中文**; build version and commit hash visible in Settings footer, tray tooltip, diagnostics report, startup logs, `--version`, and EXE file properties.
- **Offline Built-in Pinyin & Acronym Search**: Compile-time `U+4E00..U+9FA5` Pinyin table supporting full Pinyin, initials, English camel-case acronyms (e.g., `vsc` → `Visual Studio Code`), and launch-frequency ranking.
- **Schema v1 Interop & One-Click Diagnostics**: Directly compatible with `GeekDeskNext` (`settings.json` + `catalog.json`), atomic writes, daily snapshots, auto-recovery, `.gdbackup` export/restore, and one-click clipboard diagnostics.

---

## Measured Footprint & Stability (Windows x64 Release)

| Metric | Measured Result |
| :--- | :--- |
| Single-File Executable Size | `GeekDeskCpp.exe` ~**2.88 MB** (`3,023,360` bytes) |
| External Dependencies (`objdump -p`) | Standard Windows system DLLs only; **no** MinGW runtime DLLs, **no** GPU UMD, **no** `Everything*.dll` |
| Cold Startup + First Frame (`--smoke-ui`) | **~92 ms**, Private Bytes **~11–12 MB** (~25 MB Working Set) |
| Catalog Load & Index (9 groups / 78 items) | **~15 ms** |
| 200-Cycle Headless Soak (`--soak-test 200`) | GDI `62 -> 69`, USER `48 -> 49`, Handles `348 -> 348`, Private Bytes `14.63 MB -> 15.75 MB` (zero leaks) |
| Daily Idle Footprint (78 items + icon cache) | **~22–26 MB** Private Bytes (stays clean even after browsing files), **0%** idle CPU, **0 MB** VRAM |

---

## Quick Start

1. Download the latest `GeekDeskCpp-v*-win64-portable.zip` from [Releases](../../releases).
2. **Extract to a standalone folder** (e.g. `D:\Tools\GeekDeskCpp\`; do not run directly inside the ZIP archive so the portable `data\` folder remains writable).
3. Double-click `GeekDeskCpp.exe` to launch (default hotkey `Ctrl+Q`, or middle-click anywhere on screen to toggle).
   - **First-run SmartScreen notice**: Because this indie open-source tool does not carry a paid commercial code-signing certificate yet, Windows 10/11 may show *"Windows protected your PC"*. Click **More info → Run anyway**, and feel free to verify integrity using the `.sha256` checksum attached to each Release.
4. **Migrating from `GeekDeskNext`**: Exit `GeekDeskCpp.exe`, then copy `catalog.json`, `settings.json`, and `assets\` from `GeekDeskNext\data\` into the `data\` folder next to `GeekDeskCpp.exe`.

---

## Build from Source

Requirements: Windows 10/11 x64, CMake >= 3.24, Ninja, MinGW-w64 UCRT (`g++` C++20) or MSVC.

```powershell
# 1. Configure & build Release (-Wall -Wextra -Wpedantic -Werror, zero warnings)
cmake --preset mingw-release
cmake --build --preset mingw-release

# 2. Run unit tests (doctest)
ctest --preset mingw-release --output-on-failure

# 3. Check version, Mode A smoke test, 200-cycle leak check, and read-only data verification
.\build\mingw-release\GeekDeskCpp.exe --version | Out-Host
.\build\mingw-release\GeekDeskCpp.exe --smoke-ui --data-dir "<temp-data-dir>" | Out-Host
.\build\mingw-release\GeekDeskCpp.exe --soak-test 200 --data-dir "<temp-data-dir>" | Out-Host
.\build\mingw-release\GeekDeskCpp.exe --verify-data "<your-data-dir>" | Out-Host

# 4. Build, audit, soak-test, and package portable ZIP into dist\
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-release.ps1
```

---

## Acknowledgements

Design inspiration pays tribute to the open-source project [BookerLiu/GeekDesk](https://github.com/BookerLiu/GeekDesk) (rewritten from scratch in C++20 + Win32 + Direct2D with a strict focus on minimalism and low memory footprint).
