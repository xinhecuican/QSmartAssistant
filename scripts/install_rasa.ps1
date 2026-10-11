# install_rasa.ps1
# Create a Windows virtualenv and install Rasa + the extra NLU dependencies used
# by Conversation/NLU/rasanlu.cpp, then download the Chinese BERT weights
# (hfl/rbtl3) that Data/rasa/config.yml references.
#
# IMPORTANT: Rasa 3.6.19 supports Python 3.8 - 3.10 only. Newer interpreters
#            will fail to install. Pass -PythonExe if "py -3.10" is not usable.
#
# Examples:
#   .\scripts\install_rasa.ps1
#   .\scripts\install_rasa.ps1 -PythonExe "C:\Python310\python.exe"
#   .\scripts\install_rasa.ps1 -HfBase "https://huggingface.co"   # official mirror
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 using the
#       system ANSI code page unless a BOM is present, so non-ASCII text breaks it.
param(
    [string]$PythonExe = "",
    [string]$VenvDir = "",
    [string]$HfBase = "https://hf-mirror.com",
    [string]$RasaVersion = "3.6.19",
    [switch]$SkipModel,
    [switch]$Reinstall
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if (-not $VenvDir) { $VenvDir = Join-Path $root "lib\rasa" }

# ---------------------------------------------------------------------------
# 1. Locate a Python 3.8 - 3.10 interpreter
# ---------------------------------------------------------------------------
function Get-PythonCommand {
    if ($PythonExe) {
        if (-not (Test-Path $PythonExe)) { throw "Python not found: $PythonExe" }
        return @{ Exe = $PythonExe; Args = @() }
    }
    # Prefer the py launcher, newest supported version first
    if (Get-Command py -ErrorAction SilentlyContinue) {
        foreach ($v in @("-3.10", "-3.9", "-3.8")) {
            & py $v -c "import sys" 2>$null
            if ($LASTEXITCODE -eq 0) { return @{ Exe = "py"; Args = @($v) } }
        }
    }
    # Fall back to a python on PATH and check its version
    if (Get-Command python -ErrorAction SilentlyContinue) {
        $ver = (& python -c "import sys;print('%d.%d'%sys.version_info[:2])").Trim()
        $ok = @("3.8", "3.9", "3.10") -contains $ver
        if (-not $ok) {
            throw "python on PATH is $ver, but Rasa $RasaVersion needs Python 3.8-3.10. Pass -PythonExe."
        }
        return @{ Exe = "python"; Args = @() }
    }
    throw "No usable Python found. Install Python 3.10 and/or pass -PythonExe."
}

$py = Get-PythonCommand
$pyVer = (& $py.Exe @($py.Args) -c "import sys;print('%d.%d.%d'%sys.version_info[:3])").Trim()
Write-Host "==> Using Python $pyVer ($($py.Exe) $($py.Args -join ' '))"
if (-not (@("3.8", "3.9", "3.10") -contains ($pyVer -split '\.')[0..1] -join '.')) {
    throw "Rasa $RasaVersion requires Python 3.8-3.10, got $pyVer"
}

# ---------------------------------------------------------------------------
# 2. Create the virtualenv
# ---------------------------------------------------------------------------
if ($Reinstall -and (Test-Path $VenvDir)) {
    Write-Host "==> Removing $VenvDir"
    Remove-Item -Recurse -Force $VenvDir
}

if (-not (Test-Path (Join-Path $VenvDir "Scripts\python.exe"))) {
    Write-Host "==> Creating virtualenv at $VenvDir"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $VenvDir) | Out-Null
    & $py.Exe @($py.Args) -m venv $VenvDir
    if ($LASTEXITCODE -ne 0) { throw "python -m venv failed" }
} else {
    Write-Host "==> Reusing existing virtualenv: $VenvDir"
}

$venvPy = Join-Path $VenvDir "Scripts\python.exe"

# ---------------------------------------------------------------------------
# 3. Install Rasa + NLU extras (mirrors scripts/install_rasa.sh)
# ---------------------------------------------------------------------------
Write-Host "==> Upgrading pip / setuptools"
& $venvPy -m pip install --upgrade pip setuptools
# 注：旧版这里钉了 setuptools<70.4.0。那个约束在源码构建路径上有必要，
# 但实测 setuptools 78.1.0 下 rasa 3.6.19 import 正常，且过旧的 setuptools
# 无法构建部分新 wheel，所以不再降级。

$packages = @(
    "rasa==$RasaVersion",
    "jieba",
    "torch",
    # ⚠ 这三个必须钉死，否则装出来的是"能装不能跑"的 venv：
    #   rasa/nlu/utils/hugging_face/registry.py 里有
    #       from transformers import TFPreTrainedModel
    #   而 transformers 5.x 删除了全部 TF 支持 -> ImportError，
    #   服务起不来、NLU 全哑，且报错在 import 期，很容易被误判成"模型坏了"。
    #   pip 默认装的正是 5.x。已实测可用的组合：
    #   transformers 4.36.2 / tokenizers 0.15.2 / huggingface-hub 0.20.3。
    "transformers==4.36.2",
    "tokenizers<0.19",
    "huggingface-hub<0.21",
    "emoji==1.7",
    "recognizers-text-suite"
)
Write-Host "==> Installing: $($packages -join ', ')"
Write-Host "    (torch is large, this can take a long time)"
& $venvPy -m pip install @packages
if ($LASTEXITCODE -ne 0) { throw "pip install failed" }

# ---------------------------------------------------------------------------
# 4. Download the Chinese BERT weights referenced by Data/rasa/config.yml
#    (component lm_featurizer -> model_weights: "data/chinese_rbtl3")
# ---------------------------------------------------------------------------
$bertDir = Join-Path $root "Data\rasa\data\chinese_rbtl3"
if (-not $SkipModel) {
    New-Item -ItemType Directory -Force -Path $bertDir | Out-Null
    $files = @("pytorch_model.bin", "vocab.txt", "config.json")
    foreach ($f in $files) {
        $dst = Join-Path $bertDir $f
        if (Test-Path $dst) {
            Write-Host "==> $f already present, skipping"
            continue
        }
        $url = "$HfBase/hfl/rbtl3/resolve/main/$f"
        Write-Host "==> Downloading $f"
        try {
            Invoke-WebRequest -Uri $url -OutFile $dst -UseBasicParsing
        } catch {
            Write-Warning "Failed to download $f from $url : $_"
            Write-Warning "Download it manually and put it into $bertDir"
        }
    }
} else {
    Write-Host "==> -SkipModel: skipping BERT weights download"
}

Write-Host ""
Write-Host "Done."
Write-Host "  venv    : $VenvDir"
Write-Host "  rasa exe: $(Join-Path $VenvDir 'Scripts\rasa.exe')"
Write-Host "  bert    : $bertDir"
Write-Host ""
Write-Host "Add this to config.json (Data\config.json -> rasa):"
Write-Host "  `"python_venv`": `"$($VenvDir -replace '\\','\\')`""
Write-Host "  `"url`": `"http://127.0.0.1:5005/`""
Write-Host "  `"model`": `"rasa/models/default.tar.gz`""
Write-Host ""
Write-Host "The application starts 'rasa run --enable-api' automatically on Windows;"
Write-Host "its log goes to Data\rasa\rasa.log and its pid to Data\rasa\rasa.pid."
