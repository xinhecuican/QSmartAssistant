# build_windows.ps1
# One-click Windows build script (MSVC + official Qt6, Qt >= 6.8.4)
#
# Examples:
#   # Visual Studio 2022 generator
#   .\scripts\build_windows.ps1 -QtDir "C:\Qt\6.8.4\msvc2022_64"
#
#   # Ninja generator (run inside "x64 Native Tools Command Prompt for VS 2022")
#   .\scripts\build_windows.ps1 -QtDir "C:\Qt\6.8.4\msvc2022_64" -Generator Ninja
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 using the
#       system ANSI code page unless a BOM is present, so non-ASCII text breaks it.
param(
    [string]$QtDir = $env:QT_ROOT,
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [string]$BuildDir = "",
    [ValidateSet("VisualStudio", "Ninja")]
    [string]$Generator = "VisualStudio",
    [switch]$SkipDeps
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if (-not $BuildDir) {
    if ($Generator -eq "Ninja") { $BuildDir = Join-Path $root "build-ninja" }
    else { $BuildDir = Join-Path $root "build" }
}

# ---------------------------------------------------------------------------
# 1. Check Qt
# ---------------------------------------------------------------------------
if (-not $QtDir -or -not (Test-Path $QtDir)) {
    throw "Qt not found. Pass -QtDir pointing to a Qt msvc directory, e.g. C:\Qt\6.8.4\msvc2022_64, or set the QT_ROOT environment variable."
}
$windeployqt = Join-Path $QtDir "bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt.exe not found under $QtDir. Make sure it is the msvc2022_64 directory of Qt."
}

# Require Qt >= 6.8.4
$qmake = Join-Path $QtDir "bin\qmake.exe"
if (Test-Path $qmake) {
    $qtVer = (& $qmake -query QT_VERSION).Trim()
    if ([version]$qtVer -lt [version]"6.8.4") {
        throw "This project requires Qt 6.8.4 or newer for the Windows build, but found $qtVer"
    }
    Write-Host "==> Detected Qt $qtVer"
}

$env:PATH = (Join-Path $QtDir "bin") + ";" + $env:PATH

# ---------------------------------------------------------------------------
# 2. Fetch third-party dependencies (yaml-cpp / onnxruntime / sherpa-onnx)
#    lib/ is git-ignored, so a fresh clone has none of them. yaml-cpp is a
#    hard configure-time requirement (find_package(yaml_cpp REQUIRED)).
# ---------------------------------------------------------------------------
# Require BOTH the import library and the headers (mirrors install_yaml_cpp.ps1,
# which skips only when both are present). A half-present lib/yaml-cpp used to
# make this check lie, and the build then died at configure time with a very
# confusing "Could NOT find yaml_cpp (missing: yaml_cpp_LIBRARY)".
$needYaml   = -not ((Test-Path (Join-Path $root "lib\yaml-cpp\lib\yaml-cpp.lib")) -and
                    (Test-Path (Join-Path $root "lib\yaml-cpp\include\yaml-cpp\yaml.h")))
$needOrt    = -not (Test-Path (Join-Path $root "lib\onnxruntime\include"))
$needSherpa = -not (Test-Path (Join-Path $root "lib\sherpa_onnx\include"))

if (-not $SkipDeps) {
    if ($needYaml) {
        Write-Host "==> Installing yaml-cpp"
        & (Join-Path $PSScriptRoot "install_yaml_cpp.ps1")
    }
    if ($needOrt) {
        Write-Host "==> Installing onnxruntime"
        & (Join-Path $PSScriptRoot "install_onnxruntime.ps1")
    }
    if ($needSherpa) {
        Write-Host "==> Installing sherpa-onnx"
        & (Join-Path $PSScriptRoot "install_sherpa_onnx.ps1")
    }
}

# ---------------------------------------------------------------------------
# 3. Prepare runtime config (config.yaml / plugin_order.txt)
#    These are copied by CMake into ~/.config/QSmartAssistant/Data
#    A legacy config.json is still read and auto-migrated to yaml.
# ---------------------------------------------------------------------------
$dataDir = Join-Path $root "Data"
$cfg = Join-Path $dataDir "config.yaml"
$legacyCfg = Join-Path $dataDir "config.json"
if ((-not (Test-Path $cfg)) -and (-not (Test-Path $legacyCfg))) {
    Copy-Item (Join-Path $dataDir "default_config.yaml") $cfg
    Write-Host "==> Generated Data\config.yaml (adjust paths and keys as needed)"
}
$order = Join-Path $dataDir "plugin_order.txt"
if (-not (Test-Path $order)) {
    # NeteaseMusic plugin is not built on Windows by default, drop it from the list
    (Get-Content (Join-Path $dataDir "plugin_order_default.txt")) |
        Where-Object { $_.Trim() -ne "NeteaseMusic" } |
        Set-Content $order -Encoding ASCII
    Write-Host "==> Generated Data\plugin_order.txt"
}

# ---------------------------------------------------------------------------
# 4. Configure + build
# ---------------------------------------------------------------------------
$genArgs = @("-S", $root, "-B", $BuildDir, "-DCMAKE_PREFIX_PATH=$QtDir")
if ($Generator -eq "VisualStudio") {
    $genArgs += @("-G", "Visual Studio 17 2022", "-A", "x64")
} else {
    $genArgs += @("-G", "Ninja", "-DCMAKE_BUILD_TYPE=$Config")
}

Write-Host "==> CMake configure"
& cmake @genArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "==> Build"
if ($Generator -eq "VisualStudio") {
    & cmake --build $BuildDir --config $Config --parallel
} else {
    & cmake --build $BuildDir --parallel
}
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# ---------------------------------------------------------------------------
# 5. Locate the executable and deploy Qt runtime
# ---------------------------------------------------------------------------
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "QSmartAssistant.exe" -File |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "QSmartAssistant.exe not found" }
$outDir = $exe.DirectoryName

Write-Host "==> Deploying Qt runtime into $outDir"
$deployArgs = @("--no-translations", "--no-compiler-runtime", $exe.FullName)
if ($Config -eq "Debug") { $deployArgs = @("--debug") + $deployArgs } else { $deployArgs = @("--release") + $deployArgs }
& $windeployqt @deployArgs

Write-Host ""
Write-Host "Build finished."
Write-Host "  Executable : $($exe.FullName)"
Write-Host "  Plugins dir: $env:USERPROFILE\.config\QSmartAssistant\plugins\lib"
Write-Host "  Data dir   : $env:USERPROFILE\.config\QSmartAssistant\Data"
Write-Host ""
Write-Host "Next step: download the runtime models (ASR / wakeup / TTS):"
Write-Host "  python scripts\fetch_models.py"
Write-Host "See doc/windows.md for details."
