#Requires -Version 5.1
param(
    [string]$Version = "0.1.0",
    [switch]$SkipCleanBuild
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

Write-Host "=== [1/5] Configure & Build GeekDeskCpp (mingw-release) ==="
cmake --preset mingw-release | Out-Host
if ($SkipCleanBuild) {
    cmake --build --preset mingw-release | Out-Host
} else {
    cmake --build --preset mingw-release --clean-first | Out-Host
}

Write-Host "=== [2/5] Run Automated Unit Tests (ctest) ==="
ctest --preset mingw-release --output-on-failure | Out-Host

$exePath = Join-Path $RepoRoot "build\mingw-release\GeekDeskCpp.exe"
if (-not (Test-Path $exePath)) {
    throw "Executable not found: $exePath"
}

Write-Host "=== [3/5] Audit Binary Import Table ==="
$dllLines = objdump -p $exePath | Select-String "DLL Name" | ForEach-Object { $_.Line.Trim() }
$dllLines | Out-Host
foreach ($forbidden in @('libstdc++', 'libgcc', 'libwinpthread', 'd3d11', 'dxgi', 'everything')) {
    if ($dllLines -match [regex]::Escape($forbidden)) {
        throw "Forbidden DLL dependency found in import table: $forbidden"
    }
}

Write-Host "=== [4/5] Run 200-Cycle Headless UI Soak & Leak Check ==="
$soakData = Join-Path $env:TEMP ("gdcpp_release_soak_" + $PID)
New-Item -ItemType Directory -Force -Path $soakData | Out-Null
try {
    & $exePath --soak-test 200 --data-dir $soakData | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Soak test failed with exit code $LASTEXITCODE"
    }
} finally {
    Remove-Item -Recurse -Force $soakData -ErrorAction SilentlyContinue
}

Write-Host "=== [5/5] Stage Portable Package & SHA-256 ==="
$pkgName = "GeekDeskCpp-v$Version-win64-portable"
$distRoot = Join-Path $RepoRoot "dist"
$stageDir = Join-Path $distRoot $pkgName
$zipPath = Join-Path $distRoot "$pkgName.zip"
$hashPath = Join-Path $distRoot "$pkgName.zip.sha256"

Remove-Item -Recurse -Force $stageDir -ErrorAction SilentlyContinue
Remove-Item -Force $zipPath, $hashPath -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

Copy-Item -Path $exePath -Destination (Join-Path $stageDir "GeekDeskCpp.exe") -Force

$readmeText = @"
GeekDeskCpp v$Version (Windows 10/11 x64 便携版)
================================================

简介
----
GeekDeskCpp 是基于原生 C++20 + Win32 + DWM + Direct2D Software（Mode A）构建的极简桌面启动器：
- 单文件绿色运行，零 .NET / WebView2 / GPU UMD / Everything 外部依赖
- 兼容 GeekDeskNext Schema v1 数据目录（settings.json + catalog.json）
- 内置离线汉字全拼/首字母搜索，支持多屏混合 DPI 光标左上角对齐呼出
- 原子写入 + 每日快照 + 启动损坏自动恢复 + .gdbackup 单文件备份/恢复与 pre-restore-* 回滚保护

快速使用
--------
1. 直接双击 GeekDeskCpp.exe 即可在当前目录自动创建 data\ 并启动（默认热键 Ctrl+Q，鼠标中键可呼出/隐藏）。
2. 如需从 GeekDeskNext 迁移：退出原程序后，将原 data\ 文件夹完整复制到 GeekDeskCpp.exe 同级的 data\ 目录即可无缝接管。
3. 命令行参数：
   - --data-dir <目录>        指定自定义数据目录
   - --verify-data <目录>     只读校验 settings.json / catalog.json 兼容性
   - --smoke-ui               执行一次快速渲染与内存检查
   - --soak-test [N]          执行 N 轮压测并核验 GDI/USER/句柄稳定性
"@
[System.IO.File]::WriteAllText((Join-Path $stageDir "README.txt"), $readmeText, (New-Object System.Text.UTF8Encoding($false)))

Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -CompressionLevel Optimal
$sha = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText($hashPath, "$sha  $pkgName.zip`r`n", (New-Object System.Text.UTF8Encoding($false)))

Write-Host "Package ready:"
Write-Host "  EXE : $stageDir\GeekDeskCpp.exe ($((Get-Item $exePath).Length) bytes)"
Write-Host "  ZIP : $zipPath ($((Get-Item $zipPath).Length) bytes)"
Write-Host "  SHA : $sha"
