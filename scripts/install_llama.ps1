# install_llama.ps1
# Download the prebuilt llama.cpp binaries and headers into lib/llama
#   lib/llama/include  llama.h, ggml*.h, gguf.h (declarations only)
#   lib/llama/bin      llama.dll + ggml*.dll (runtime)
#
# Why runtime binding instead of linking:
#   the official package ships DLLs and EXEs only -- no headers, no .lib
#   import libraries. Utils/LLM/LlamaApi.cpp therefore resolves every symbol
#   with GetProcAddress at run time, so llama.cpp is an OPTIONAL dependency:
#   projects without it still configure and build, they just lose the
#   "load model directly" backend. See the QSA_HAVE_LLAMA guards.
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 using the
#       system ANSI code page unless a BOM is present, so non-ASCII text breaks it.
param(
    # llama.cpp build tag. Asset names are llama-<tag>-bin-win-<backend>-x64.zip
    [string]$Tag = "b9245",
    # Backend: cpu / cuda-12.4 / vulkan / hip-radeon. Pick per your hardware.
    #   cpu is the safe default and is what the NLU path needs (2B model, short output).
    [string]$Backend = "cpu",
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$libDir = Join-Path $root "lib\llama"
$tmp = Join-Path $env:TEMP "qsa_llama_$Tag"

$archiveName = "llama-$Tag-bin-win-$Backend-$Arch.zip"
$url = "https://github.com/ggml-org/llama.cpp/releases/download/$Tag/$archiveName"

Write-Host "==> Downloading llama.cpp $Tag ($Backend/$Arch)"
Write-Host="    $url"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$archivePath = Join-Path $tmp $archiveName
Invoke-WebRequest -Uri $url -OutFile $archivePath

Write-Host "==> Extracting"
Expand-Archive -Path $archivePath -DestinationPath $tmp -Force

# ---- runtime DLLs ----
New-Item -ItemType Directory -Force -Path (Join-Path $libDir "bin") | Out-Null
$copyFromBin = $false
foreach ($d in Get-ChildItem -Path $tmp -Directory -Recurse -Depth 1) {
    if (Test-Path (Join-Path $d.FullName "llama.dll")) { $copyFromBin = $true; $pkgDir = $d.FullName; break }
}
if (-not $copyFromBin) {
    # some layouts put the DLLs at the archive root
    $pkgDir = $tmp
}

# Copy llama.dll plus every ggml-*.dll it depends on. Skipping these is the
# classic "it worked on my machine" failure: llama.dll loads fine, then dies
# on the first call with STATUS_DLL_NOT_FOUND for ggml-cpu-<isa>.dll.
Get-ChildItem -Path (Join-Path $pkgDir "*.dll") -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "llama.dll" -or $_.Name -like "ggml*.dll" -or $_.Name -like "libomp*.dll" } |
    ForEach-Object { Copy-Item -Force $_.FullName (Join-Path $libDir "bin") }

# ---- headers ----
# These are NOT in the binary package, so pull them from the matching source tag.
# Only declarations, but the struct layouts must match the DLL exactly --
# copying them by hand would eventually corrupt memory.
New-Item -ItemType Directory -Force -Path (Join-Path $libDir "include") | Out-Null

$headers = @(
    @{ path = "include/llama.h" },
    @{ path = "ggml/include/ggml.h" },
    @{ path = "ggml/include/ggml-cpu.h" },
    @{ path = "ggml/include/ggml-alloc.h" },
    @{ path = "ggml/include/ggml-cpp.h" },
    @{ path = "ggml/include/ggml-backend.h" },
    @{ path = "ggml/include/ggml-opt.h" },
    @{ path = "ggml/include/gguf.h" },
    @{ path = "ggml/src/ggml-backend-impl.h" }
)

foreach ($h in $headers) {
    $name = Split-Path $h.path -Leaf
    $dst = Join-Path $libDir "include\$name"
    $src = "https://raw.githubusercontent.com/ggml-org/llama.cpp/$Tag/$($h.path)"
    try {
        Invoke-WebRequest -Uri $src -OutFile $dst -ErrorAction Stop
        Write-Host "    $name"
    } catch {
        Write-Warning "cannot fetch $name from $($h.path): $($_.Exception.Message)"
    }
}

if (-not (Test-Path (Join-Path $libDir "include\llama.h"))) {
    throw "llama.h was not fetched; the local backend cannot be enabled"
}

Remove-Item -Recurse -Force $tmp
Write-Host ""
Write-Host "==> llama.cpp installed into $libDir"
Write-Host "    next: python scripts\fetch_llm_model.py --quant Q4_K_M"
Write-Host ""
Write-Host "    Then add a backend to config.json:"
Write-Host '      "llm": {"backends": {"local-qwen": {'
Write-Host '        "type": "llamacpp",'
Write-Host '        "model": "Qwen3.5-2B-Q4_K_M.gguf",'
Write-Host '        "modelDir": "llm",'
Write-Host '        "nCtx": 2048,'
Write-Host '        "nThreads": 4,'
Write-Host '        "gpuLayers": 0'
Write-Host '      }}}'
Write-Host '      "llm_nlu": {"llm": "local-qwen"}'