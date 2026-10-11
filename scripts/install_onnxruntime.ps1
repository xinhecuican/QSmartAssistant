# install_onnxruntime.ps1
# Download the prebuilt Windows ONNX Runtime package and extract it into lib/onnxruntime
#   lib/onnxruntime/include  headers (onnxruntime_cxx_api.h, ...)
#   lib/onnxruntime/lib      import library onnxruntime.lib (link time)
#   lib/onnxruntime/bin      runtime onnxruntime.dll
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 using the
#       system ANSI code page unless a BOM is present, so non-ASCII text breaks it.
param(
    [string]$Version = "1.17.1",
    [string]$Arch = "x64"
)

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$libDir = Join-Path $root "lib\onnxruntime"
$tmp    = Join-Path $env:TEMP "qsa_onnxruntime_$Version"

$zipName = "onnxruntime-win-$Arch-$Version.zip"
$url     = "https://github.com/microsoft/onnxruntime/releases/download/v$Version/$zipName"

Write-Host "==> Downloading onnxruntime $Version ($Arch)"
Write-Host "    $url"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$zipPath = Join-Path $tmp $zipName
Invoke-WebRequest -Uri $url -OutFile $zipPath
Expand-Archive -Path $zipPath -DestinationPath $tmp -Force

$pkgDir = Join-Path $tmp "onnxruntime-win-$Arch-$Version"
if (-not (Test-Path $pkgDir)) {
    # Fallback: pick the first extracted directory
    $pkgDir = (Get-ChildItem -Path $tmp -Directory | Select-Object -First 1).FullName
}

foreach ($sub in @("include", "lib", "bin")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $libDir $sub) | Out-Null
}

Copy-Item -Recurse -Force (Join-Path $pkgDir "include\*") (Join-Path $libDir "include")
Copy-Item -Force (Join-Path $pkgDir "lib\onnxruntime.lib")  (Join-Path $libDir "lib")
Copy-Item -Force (Join-Path $pkgDir "lib\onnxruntime.dll")  (Join-Path $libDir "bin")

Remove-Item -Recurse -Force $tmp
Write-Host "==> onnxruntime installed into $libDir"
