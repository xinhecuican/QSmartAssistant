# install_yaml_cpp.ps1
# Build yaml-cpp (static) and install it into lib/yaml-cpp
#     lib/yaml-cpp/include/yaml-cpp/*.h
#     lib/yaml-cpp/lib/yaml-cpp.lib
#
# Why this script exists: `lib/` is git-ignored, so a fresh clone has no
# lib/yaml-cpp, while the top-level CMakeLists.txt does
# `find_package(yaml_cpp REQUIRED)` -> configure fails with
#     Could NOT find yaml_cpp (missing: yaml_cpp_LIBRARY)
#
# Keep it STATIC (same as the checked-in-for-local-use yaml-cpp.lib):
# cmake/Findyaml_cpp.cmake adds YAML_CPP_STATIC_DEFINE for the consumer, and a
# shared build would make yaml-cpp/dll.h expand to __declspec(dllimport).
#
# Examples:
#   powershell -ExecutionPolicy Bypass -File scripts\install_yaml_cpp.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\install_yaml_cpp.ps1 -Force
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 using the
#       system ANSI code page unless a BOM is present, so non-ASCII text breaks it.
param(
    [string]$Version = "0.8.0",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$libDir = Join-Path $root "lib\yaml-cpp"
$stamp  = Join-Path $libDir "lib\yaml-cpp.lib"
$header = Join-Path $libDir "include\yaml-cpp\yaml.h"

if ((-not $Force) -and (Test-Path $stamp) -and (Test-Path $header)) {
    Write-Host "==> yaml-cpp already present ($libDir), skipping (use -Force to rebuild)"
    exit 0
}

foreach ($tool in @("cmake", "git")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool not found in PATH. Install CMake and Git for Windows first."
    }
}

$tmp = Join-Path $env:TEMP "qsa_yaml_cpp_$Version"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# The 0.8.0 release tag is literally "0.8.0", not "yaml-cpp-0.8.0".
Write-Host "==> Cloning yaml-cpp $Version"
& git clone --depth 1 --branch $Version https://github.com/jbeder/yaml-cpp.git (Join-Path $tmp "src")
if ($LASTEXITCODE -ne 0) { throw "git clone failed" }

# Static + PIC, tests/tools/contrib off (nothing in this project uses them).
# CMAKE_POLICY_VERSION_MINIMUM=3.5: yaml-cpp 0.8.0 declares
# `cmake_minimum_required(VERSION 3.4)` and CMake 4.x refuses anything < 3.5
# ("Compatibility with CMake < 3.5 has been removed"). Older CMake ignores the
# variable, so it is safe to always pass it.
Write-Host "==> Configuring yaml-cpp (static)"
$cmakeArgs = @(
    "-S", (Join-Path $tmp "src"),
    "-B", (Join-Path $tmp "build"),
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DYAML_BUILD_SHARED_LIBS=OFF",
    "-DYAML_CPP_BUILD_TESTS=OFF",
    "-DYAML_CPP_BUILD_TOOLS=OFF",
    "-DYAML_CPP_BUILD_CONTRIB=OFF",
    "-DYAML_CPP_INSTALL=ON",
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
    "-DCMAKE_INSTALL_PREFIX=$libDir",
    "-DCMAKE_INSTALL_LIBDIR=lib"
)
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "==> Building yaml-cpp"
& cmake --build (Join-Path $tmp "build") --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

& cmake --install (Join-Path $tmp "build") --config Release
if ($LASTEXITCODE -ne 0) { throw "cmake install failed" }

Remove-Item -Recurse -Force $tmp

# Self check: these two files are exactly what cmake/Findyaml_cpp.cmake looks for.
if (-not (Test-Path $header)) {
    throw "yaml-cpp/yaml.h not installed. Actual layout: $(Get-ChildItem -Recurse $libDir | Select-Object -First 40 | Out-String)"
}
if (-not (Test-Path $stamp)) {
    throw "yaml-cpp.lib not produced. Actual layout: $(Get-ChildItem -Recurse $libDir | Select-Object -First 40 | Out-String)"
}

Write-Host "==> yaml-cpp $Version installed into $libDir"
Write-Host "    include : $libDir\include"
Write-Host "    library : $stamp"
