# install_netease_js.ps1
# Install the node-based NeteaseCloudMusicApi server used by the NeteaseMusic
# plugin when built with NETEASE_USE_JS (the default on Windows).
#
# The server source is fetched as a tarball from codeload.github.com, so no git
# installation is required. Needs Node.js 18+ and tar.exe (bundled with
# Windows 10 1803+).
#
# After it finishes, the easiest is to leave homeDir EMPTY in config.yaml so the
# plugin auto-detects the install dir (portable across Linux/Windows):
#     "netease_cloud": { "homeDir": "", "port": "34793" }
# Otherwise put the printed directory into homeDir (absolute path, OS-specific).
#
# Examples:
#   .\scripts\install_netease_js.ps1
#   .\scripts\install_netease_js.ps1 -Reinstall
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 using the
#       system ANSI code page unless a BOM is present, so non-ASCII text breaks it.
param(
    [string]$TargetDir = "",
    [string]$TarUrl = "https://codeload.github.com/neteasecloudmusicapienhanced/api-enhanced/tar.gz/refs/heads/main",
    [string]$TopDirName = "api-enhanced-main",
    [switch]$Reinstall
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if (-not $TargetDir) { $TargetDir = Join-Path $root "lib\NeteaseCloudMusicApi" }

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    throw "node not found in PATH. Install Node.js 18+ from https://nodejs.org"
}
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
    throw "npm not found in PATH. Reinstall Node.js and make sure npm is included."
}
if (-not (Get-Command tar -ErrorAction SilentlyContinue)) {
    throw "tar.exe not found in PATH. It ships with Windows 10 1803+; otherwise install Git for Windows or bsdtar."
}

Write-Host "==> node version: $(& node --version)"

if ($Reinstall -and (Test-Path $TargetDir)) {
    Write-Host "==> Removing $TargetDir"
    Remove-Item -Recurse -Force $TargetDir
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("qsa_netease_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null

try {
    $tarFile = Join-Path $tmpDir "api.tar.gz"
    Write-Host "==> Downloading api-enhanced"
    $oldProgress = $ProgressPreference
    $ProgressPreference = "SilentlyContinue"
    try {
        Invoke-WebRequest -Uri $TarUrl -OutFile $tarFile -UseBasicParsing
    } finally {
        $ProgressPreference = $oldProgress
    }
    Write-Host ("    -> {0:N1} MB" -f ((Get-Item $tarFile).Length / 1MB))

    Write-Host "==> Extracting"
    & tar -xzf $tarFile -C $tmpDir
    if ($LASTEXITCODE -ne 0) { throw "tar extraction failed" }

    $src = Join-Path $tmpDir $TopDirName
    if (-not (Test-Path (Join-Path $src "app.js"))) {
        throw "app.js not found under $src - unexpected repository layout"
    }

    if (Test-Path $TargetDir) { Remove-Item -Recurse -Force $TargetDir }
    Move-Item $src $TargetDir
} finally {
    Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
}

Push-Location $TargetDir
try {
    Write-Host "==> npm install --omit=dev"
    # --ignore-scripts: the upstream "prepare" hook runs husky, which fails
    # without a .git directory (we installed from a tarball).
    & npm install --omit=dev --ignore-scripts --no-audit --no-fund
    if ($LASTEXITCODE -ne 0) { throw "npm install failed" }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "Done. Node API server installed at:"
Write-Host "  $TargetDir"
Write-Host ""
Write-Host "Add this to config.json (Data\config.json -> netease_cloud):"
Write-Host "  `"homeDir`": `"$($TargetDir -replace '\\','\\')`""
Write-Host "  `"port`": `"34793`""
Write-Host ""
Write-Host "Optional: set `"nodeExe`" if node is not on PATH, e.g."
Write-Host "  `"nodeExe`": `"C:\\Program Files\\nodejs\\node.exe`""
Write-Host "(see doc/windows.md, section about NeteaseMusic)"
