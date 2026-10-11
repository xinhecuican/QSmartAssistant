#!/bin/bash
# ---------------------------------------------------------------------------
# install_llama.sh —— 下载 llama.cpp 预编译包到 lib/llama（对应 install_llama.ps1）
#
#   lib/llama/include   llama.h, ggml*.h, gguf.h（只有声明）
#   lib/llama/bin       libllama.so + libggml*.so（运行期）
#
# 为什么是运行期绑定而不是链接：
#   官方只发二进制包，没有头文件之外的导入库。Utils/LLM/LlamaApi.cpp 用
#   QLibrary 在运行期 resolve 每个符号，所以 llama.cpp 是**可选依赖**：
#   不装照样能 configure + build，只是没有"本地直接加载模型"的后端。
#   C++ 侧按 dir + "/llama" 查找（无扩展名），QLibrary 在 Linux 上会自动
#   补成 libllama.so，Windows 上补成 llama.dll —— 两端同一份代码。
#
# 用法：
#   bash scripts/install_llama.sh                          # CPU, 自动探测架构
#   bash scripts/install_llama.sh --backend vulkan          # AMD/Intel/NVIDIA 都可用
#   bash scripts/install_llama.sh --backend cuda-12.4      # 仅 NVIDIA x64
#   bash scripts/install_llama.sh --arch arm64 --backend cpu
#   bash scripts/install_llama.sh --tag b9859
#
# ⚠️ 包名规则（**与 Windows 不同，别照抄 ps1 的模板**）：
#     Linux: llama-<tag>-bin-ubuntu-<backend>-<arch>.tar.gz
#            llama-<tag>-bin-ubuntu-<arch>.tar.gz           <- CPU 没有 -cpu 段！
#     Windows: llama-<tag>-bin-win-<backend>-x64.zip
#   CPU 版在 Linux 上不带 backend 段 —— 照抄 "-cpu-" 会 404。
#   解压出来的目录名是 llama-<tag>（不含 backend/arch），也不是包名。
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"
lib_dir="${ROOT}/lib/llama"

TAG="b9859"
BACKEND="cpu"
ARCH=""
HF_BASE="${HF_ENDPOINT:-https://huggingface.co}"

usage() {
    sed -n '3,30p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --tag)     TAG="$2"; shift 2 ;;
        --backend) BACKEND="$2"; shift 2 ;;
        --arch)    ARCH="$2"; shift 2 ;;
        --hf-base) HF_BASE="$2"; shift 2 ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

# ---- 架构 ----
if [ -z "${ARCH}" ]; then
    case "$(uname -m)" in
        x86_64)        ARCH="x64" ;;
        aarch64|arm64) ARCH="arm64" ;;
        *) echo "unsupported architecture: $(uname -m)" >&2
           echo "  llama.cpp publishes linux builds for x64 and arm64 only" >&2
           exit 1 ;;
    esac
fi

# ---- 拼包名（CPU 无 backend 段，这是最容易写错的地方）----
if [ "${BACKEND}" = "cpu" ]; then
    archive="llama-${TAG}-bin-ubuntu-${ARCH}.tar.gz"
else
    archive="llama-${TAG}-bin-ubuntu-${BACKEND}-${ARCH}.tar.gz"
fi
url="https://github.com/ggml-org/llama.cpp/releases/download/${TAG}/${archive}"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/qsa-llama-XXXXXX")"
trap 'rm -rf "${TMP_DIR}"' EXIT

echo "==> Downloading llama.cpp ${TAG} (${BACKEND}/${ARCH})"
echo "    ${url}"
if ! wget -q --show-progress "${url}" -O "${TMP_DIR}/${archive}"; then
    echo "" >&2
    echo "ERROR: download failed: ${url}" >&2
    echo "  This tag may not publish a ${BACKEND}/${ARCH} linux build." >&2
    echo "  List the available assets:" >&2
    echo "    curl -s https://api.github.com/repos/ggml-org/llama.cpp/releases/tags/${TAG} \\" >&2
    echo "      | grep -o '\"name\": \"[^\"]*ubuntu[^\"]*\"'" >&2
    echo "  Note: the linux CPU asset has NO '-cpu' segment." >&2
    exit 1
fi

# GitHub 下载失败时会给一个几字节的 HTML/JSON 而不是 404 页面。显式挡掉。
SIZE="$(wc -c < "${TMP_DIR}/${archive}")"
if [ "${SIZE}" -lt 100000 ]; then
    echo "ERROR: ${archive} is only ${SIZE} bytes - this is an error page, not the package." >&2
    head -c 200 "${TMP_DIR}/${archive}" >&2
    exit 1
fi

echo "==> Extracting"
tar xzf "${TMP_DIR}/${archive}" -C "${TMP_DIR}"

# 解压目录名不固定：可能是 llama-<tag>、llama-<tag>-bin-ubuntu-<arch>，
# 也可能把 .so 直接摊在根目录。用"含 libllama.so 的那一层"来定位。
pkg_dir="$(dirname "$(find "${TMP_DIR}" -maxdepth 3 -name 'libllama.so*' -print -quit)")"
[ -n "${pkg_dir}" ] || { echo "ERROR: libllama.so not found in ${archive}" >&2; exit 1; }

# ---- 运行期 .so ----
mkdir -p "${lib_dir}/bin"
# 必须连带 libggml*.so 与 libgomp*.so：libllama.so 会 dlopen/链接它们，
# 少拷一个就是"llama.so 加载成功、第一次调用才报 cannot open shared object file"。
find "${pkg_dir}" -maxdepth 1 -name 'libllama.so*' -exec cp -f {} "${lib_dir}/bin/" \;
find "${pkg_dir}" -maxdepth 1 -name 'libggml*.so*'  -exec cp -f {} "${lib_dir}/bin/" \;
find "${pkg_dir}" -maxdepth 1 -name 'libgomp*.so*'  -exec cp -f {} "${lib_dir}/bin/" \;
chmod +x "${lib_dir}"/bin/*.so* 2>/dev/null || true

copied="$(find "${lib_dir}/bin" -maxdepth 1 -name '*.so*' | wc -l)"
echo "==> Copied ${copied} shared objects into ${lib_dir}/bin"
if [ "${copied}" -lt 2 ]; then
    echo "WARNING: only ${copied} .so copied. A working llama.cpp needs both" >&2
    echo "         libllama.so and libggml*.so (the latter holds" >&2
    echo "         ggml_backend_load_all_from_path). Check the package layout." >&2
fi

# ---- 头文件（不在二进制包里，从同 tag 的源码 tag 拉）----
# 结构布局必须与 .so 完全一致，手抄迟早会踩内存。
echo "==> Fetching headers from tag ${TAG}"
mkdir -p "${lib_dir}/include"
headers=(
    "include/llama.h"
    "ggml/include/ggml.h"
    "ggml/include/ggml-cpu.h"
    "ggml/include/ggml-alloc.h"
    "ggml/include/ggml-cpp.h"
    "ggml/include/ggml-backend.h"
    "ggml/include/ggml-opt.h"
    "ggml/include/gguf.h"
    "ggml/src/ggml-backend-impl.h"
)
for h in "${headers[@]}"; do
    name="$(basename "${h}")"
    dst="${lib_dir}/include/${name}"
    src="https://raw.githubusercontent.com/ggml-org/llama.cpp/${TAG}/${h}"
    if curl -fsSL "${src}" -o "${dst}"; then
        echo "    ${name}"
    else
        echo "    WARN cannot fetch ${name} from ${h}" >&2
        rm -f "${dst}"
    fi
done

[ -f "${lib_dir}/include/llama.h" ] || {
    echo "ERROR: llama.h was not fetched; the llamacpp backend cannot be enabled" >&2
    exit 1
}

# ---- GPU 依赖提示（装了 vulkan/cuda 包却缺运行库，是最常见的"装完不能用"）----
if [ "${BACKEND}" = "vulkan" ]; then
    echo ""
    echo "NOTE: Vulkan backend needs the Vulkan loader + a working ICD:"
    echo "  sudo apt install libvulkan1 vulkan-tools mesa-vulkan-drivers"
    echo "  vulkaninfo --summary      # verify a GPU is detected"
    echo "  If this reports nothing, install the vendor driver first"
    echo "  (NVIDIA: nvidia-driver-XXX / AMD: amdgpu-install)."
fi
if [[ "${BACKEND}" == cuda-* ]]; then
    echo ""
    echo "NOTE: CUDA backend needs the NVIDIA driver + CUDA runtime."
    echo "  nvidia-smi                # verify the driver is loaded"
    echo "  The wheel only bundles libcudart, not the driver."
fi

cat <<EOF

Done.
  llama libs : ${lib_dir}/bin
  headers    : ${lib_dir}/include

Next:
  python3 scripts/fetch_llm_model.py --hf-base ${HF_BASE} --quant Q4_K_M

Then add a backend to config.json:
  "llm": { "backends": { "local-qwen": {
    "type": "llamacpp",
    "model": "Qwen3.5-2B-Q4_K_M.gguf",
    "modelDir": "llm",
    "nCtx": 2048,
    "nThreads": $(nproc 2>/dev/null || echo 4),
    "gpuLayers": 0
  }}}
  "llm_nlu": { "llm": "local-qwen" }

Leave "llamaDir" empty to auto-detect lib/llama/bin (LlamaApi::searchPaths).
EOF