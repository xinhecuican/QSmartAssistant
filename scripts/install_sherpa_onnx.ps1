# install_sherpa_onnx.ps1
# Download the prebuilt Windows sherpa-onnx shared library and extract it into lib/sherpa_onnx
#   lib/sherpa_onnx/include  C API headers (sherpa-onnx/c-api/c-api.h)
#   lib/sherpa_onnx/lib      import libraries *.lib (link time)
#   lib/sherpa_onnx/bin      runtime *.dll (sherpa-onnx-c-api.dll, onnxruntime.dll, ...)
#
# NOTE: the sherpa-onnx C API structs evolve between releases. If the build reports
#       unknown struct members, switch to a version that matches the source code by
#       passing -Version <ver>.
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 using the
#       system ANSI code page unless a BOM is present, so non-ASCII text breaks it.
param(
    [string]$Version = "1.13.8",
    [string]$Arch = "x64",
    [ValidateSet("MD", "MT")]
    [string]$Runtime = "MD",
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$libDir = Join-Path $root "lib\sherpa_onnx"
$tmp    = Join-Path $env:TEMP "qsa_sherpa_$Version"

$archiveName = "sherpa-onnx-v$Version-win-$Arch-shared-$Runtime-$BuildType.tar.bz2"
$url = "https://github.com/k2-fsa/sherpa-onnx/releases/download/v$Version/$archiveName"

Write-Host "==> Downloading sherpa-onnx $Version ($Arch)"
Write-Host "    $url"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$archivePath = Join-Path $tmp $archiveName
Invoke-WebRequest -Uri $url -OutFile $archivePath

# Windows 10 1803+ ships tar (bsdtar) which supports tar.bz2
Push-Location $tmp
try {
    & tar -xjf $archivePath
} finally {
    Pop-Location
}

# Locate the extracted package directory (the one containing an "include" folder)
$pkgDir = $null
foreach ($d in Get-ChildItem -Path $tmp -Directory) {
    if (Test-Path (Join-Path $d.FullName "include")) { $pkgDir = $d.FullName; break }
}
if (-not $pkgDir) { throw "Cannot locate the extracted sherpa-onnx directory" }

foreach ($sub in @("include", "lib", "bin")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $libDir $sub) | Out-Null
}

Copy-Item -Recurse -Force (Join-Path $pkgDir "include\*") (Join-Path $libDir "include")
if (Test-Path (Join-Path $pkgDir "lib")) {
    Copy-Item -Force (Join-Path $pkgDir "lib\*.lib") (Join-Path $libDir "lib") -ErrorAction SilentlyContinue
    # some releases put the runtime DLLs directly under lib
    Copy-Item -Force (Join-Path $pkgDir "lib\*.dll") (Join-Path $libDir "bin") -ErrorAction SilentlyContinue
}
if (Test-Path (Join-Path $pkgDir "bin")) {
    Copy-Item -Force (Join-Path $pkgDir "bin\*.dll") (Join-Path $libDir "bin") -ErrorAction SilentlyContinue
}

Remove-Item -Recurse -Force $tmp
Write-Host "==> sherpa-onnx installed into $libDir"
