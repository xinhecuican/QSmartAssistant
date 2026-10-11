#!/bin/bash
# ---------------------------------------------------------------------------
# setup_rocm_linux.sh —— 在 Linux 上准备 AMD ROCm + PyTorch（仅微调需要）
#
# ⚠️ 先说清楚适用范围，否则容易白忙：
#   **部署机不需要 ROCm。** 运行期只有 rasa（NLU）与 sherpa-onnx（唤醒/VAD/ASR/
#   TTS）在跑，rasa 走的是 TensorFlow/CPU 推理路径，CPU 上 P50 只有 42ms。
#   只有「微调 Qwen3.5-2B 做 NLU」这一步才需要 GPU。
#   如果 nlu.backend = "rasa"（当前默认），这套完全用不上。
#
# 用法：
#   bash scripts/setup_rocm_linux.sh              # 打印命令，不执行
#   bash scripts/setup_rocm_linux.sh --check      # 体检当前环境
#   bash scripts/setup_rocm_linux.sh --install    # 真正执行
#   bash scripts/setup_rocm_linux.sh --gfx gfx1100
#
# GPU 矩阵（gfx target 决定 wheel 后缀 / rocm 的 AMDGPU_TARGETS，写错就是
# "no kernel image is available for execution on the device"）：
#     RDNA 4  gfx1201  RX 9070 XT / RX 9070 / Radeon AI PRO R9700
#     RDNA 4  gfx1200  RX 9060 XT / RX 9060
#     RDNA 3  gfx1100  RX 7900 XTX / 7900 XT
#     RDNA 3  gfx1101  RX 7800 XT
#     RDNA 3  gfx1102  RX 7700 XT
#     RDNA 2  gfx1030  RX 6800 / 6900（老卡，ROCm 已逐步放弃）
#
# ⚠️ 两个必踩的坑（Windows 版 setup_rocm_windows.py 里已经处理，
#    Linux 版同样适用，别只装 torch）：
#   1. **必须同时**装 rocm 与 torch，且带 --no-cache-dir --only-binary=:all:。
#      只装 torch 会报"找不到 rocm"；不加 only-binary 会回退源码构建（几小时）。
#   2. **ROCm OOM 的读法**：报 `free: 0, total: 17095983104` 说明是显存**碎片**
#      不是用满 → 在 `import torch` 之前（模块级）设
#        os.environ.setdefault("PYTORCH_HIP_ALLOC_CONF", "expandable_segments:True")
#      放 import 之后就无效（HIP 上下文已初始化）。
#      另外 16GB 卡上跑 Trainer 务必 prediction_loss_only=True，
#      否则 eval 会把全部样本的 logits 留在显存里（约 6.7GB）直接 OOM。
# ---------------------------------------------------------------------------
set -uo pipefail

GFX="gfx1201"
ROCM_VER="10.0.0"
TORCH_VER="2.13.0"
ACTION="print"

while [ $# -gt 0 ]; do
    case "$1" in
        --gfx)     GFX="$2"; shift 2 ;;
        --rocm)    ROCM_VER="$2"; shift 2 ;;
        --torch)   TORCH_VER="$2"; shift 2 ;;
        --install) ACTION="install"; shift ;;
        --check)   ACTION="check"; shift ;;
        -h|--help) sed -n '3,36p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

INDEX="https://stable.repo.amd.com/rocm/whl-next/"

# ---------------------------------------------------------------------------
# --check
# ---------------------------------------------------------------------------
do_check() {
    echo "==> OS        : $(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME" || uname -a)"
    echo "==> Arch      : $(uname -m)"
    echo "==> Kernel    : $(uname -r)"

    if command -v rocm-smi >/dev/null 2>&1; then
        echo "==> rocm-smi  :"
        rocm-smi --showproductname 2>/dev/null | sed 's/^/    /' || true
    else
        echo "==> rocm-smi  : NOT FOUND (install rocm-smi or the amdgpu driver)"
    fi

    if command -v rocminfo >/dev/null 2>&1; then
        echo "==> rocminfo gfx targets:"
        rocminfo 2>/dev/null | grep -oE 'gfx[0-9a-z]+' | sort -u | sed 's/^/    /'
    fi

    local py="${QSA_FINETUNE_PYTHON:-python3}"
    if command -v "${py}" >/dev/null 2>&1 || [ -x "${py}" ]; then
        echo "==> python    : $("${py}" -V 2>&1)"
        # 注意：torch 必须先于 transformers 装好 ROCm 版再验证，
        # 否则 import 到的是 PyPI 的 CPU 版，看不出问题。
        "${py}" - <<'PY' 2>&1 | sed 's/^/    /'
try:
    import torch
    print("torch", torch.__version__, "| hip:", getattr(torch.version, "hip", "n/a"))
    print("cuda_available:", torch.cuda.is_available())
    if torch.cuda.is_available():
        print("device:", torch.cuda.get_device_name(0))
        print("capability:", torch.cuda.get_device_capability(0))
except ImportError:
    print("torch not installed")
PY
    else
        echo "==> python    : ${py} not found"
    fi
    echo ""
    echo "ROCm 训练要 Python 3.12（部分依赖只为 3.12 提供构建）。"
}

# ---------------------------------------------------------------------------
# 打印 / 执行
# ---------------------------------------------------------------------------
run() {
    echo "+ $*"
    if [ "${ACTION}" = "install" ]; then
        "$@"
    fi
}

do_install_cmds() {
    echo "==> Steps (target gfx=${GFX}, ROCm ${ROCM_VER}, torch ${TORCH_VER})"
    echo ""
    echo "# 1) 系统层 AMDGPU 仓库（发行版不同，Ubuntu/Debian 用这一套）"
    run sudo apt-get update
    run sudo apt-get install -y python3-pip libstdc++-dev build-essential git gnupg2 curl
    run curl -sSL -o /tmp/rocm.gpg https://repo.radeon.com/rocm/rocm.gpg
    run sudo install -m 0644 /tmp/rocm.gpg /etc/apt/keyrings/rocm.gpg
    run sh -c 'echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/rocm.gpg] https://repo.radeon.com/rocm/apt/$(. /etc/os-release; echo $VERSION_CODENAME) main" > /etc/apt/sources.list.d/rocm.list'
    run sudo apt-get update
    run sudo apt-get install -y rocm-dev rocm-libs
    run sudo usermod -aG video render "$USER"   # 之后需要重新登录才生效

    echo ""
    echo "# 2) Python 3.12 venv（不要用发行版自带的 3.10）"
    run sudo apt-get install -y python3.12 python3.12-venv
    run python3.12 -m venv .venv
    run source .venv/bin/activate

    echo ""
    echo "# 3) rocm 与 torch 必须同时装，并强制二进制（见文件头「两个必踩的坑」）"
    run python -m pip install --upgrade pip
    run python -m pip install "rocm[libraries,device-${GFX}]==${ROCM_VER}"
    run python -m pip install "torch[device-${GFX}]==${TORCH_VER}+rocm${ROCM_VER}" \
        --index-url "${INDEX}" --no-cache-dir --only-binary=:all:

    echo ""
    echo "# 4) 其余微调依赖"
    run python -m pip install -r tools/nlu_llm_finetune/requirements.txt

    echo ""
    echo "# 5) 验证：device 应显示你的 GPU，capability 应非 0"
    run python -c "import torch; print(torch.__version__, torch.cuda.is_available(), torch.cuda.get_device_name(0) if torch.cuda.is_available() else '')"
    echo ""
    echo "若 'no kernel image is available'：gfx 选错了，重跑 --gfx <target>（rocminfo | grep gfx 看实际值）。"
}

case "${ACTION}" in
    check)   do_check ;;
    install) do_check; echo ""; do_install_cmds ;;
    *)       do_install_cmds ;;
esac