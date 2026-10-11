#!/bin/bash
# 安装 sherpa-onnx（C API 共享库）+ 示例 ASR/TTS 模型
#
# 用法：
#   bash scripts/install_sherpa_onnx.sh              # 按 uname -m 自动选
#   bash scripts/install_sherpa_onnx.sh --skip-model # 只装库，不下模型
#   bash scripts/install_sherpa_onnx.sh --jobs 8
#
# 架构说明：sherpa-onnx 与 onnxruntime 官方都只提供 x86_64 / aarch64 预编译包。
# 树莓派 4b（64 位）走 aarch64 路径。32 位系统不在支持范围内。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"
lib_path="${ROOT}/lib/sherpa_onnx"
onnx_path="${ROOT}/lib/onnxruntime"
bert_path="${ROOT}/Data/rasa/data/chinese_rbtl3"

JOBS="$(nproc 2>/dev/null || echo 4)"
SKIP_MODEL=0
SHERPA_COMMIT="94a040e396d5b612de1b54f173434e3489c357b4"

while [ $# -gt 0 ]; do
    case "$1" in
        --jobs|-j)    JOBS="$2"; shift 2 ;;
        --skip-model) SKIP_MODEL=1; shift ;;
        -h|--help)    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

ARCH="$(uname -m)"
case "${ARCH}" in
    x86_64)        ;;
    aarch64|arm64) ;;
    *) echo "unsupported architecture: ${ARCH} (need x86_64 or aarch64)" >&2; exit 1 ;;
esac

# onnxruntime 的产物布局随架构不同：x64 包解压后是 include/ lib/，
# aarch64 包是 onnxruntime-linux-aarch64-<ver>/ 里的同名目录。install_onnxruntime.sh
# 已经把两者都规整成 ${onnx_path}/{include,lib}，所以这里只要存在性检查。
if [ ! -f "${onnx_path}/lib/libonnxruntime.so" ]; then
    echo "${onnx_path}/lib/libonnxruntime.so not found, installing it first"
    bash "${SCRIPT_DIR}/install_onnxruntime.sh"
fi
[ -f "${onnx_path}/lib/libonnxruntime.so" ] || {
    echo "ERROR: onnxruntime still missing after install_onnxruntime.sh" >&2
    exit 1
}

BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/sherpa-build-XXXXXX")"
# 无论成功失败都清掉临时目录。旧版把 clone 留在 scripts/ 下，
# 第二次跑会因为目录已存在而失败。
cleanup() { rm -rf "${BUILD_DIR}"; }
trap cleanup EXIT

echo "==> Cloning sherpa-onnx @ ${SHERPA_COMMIT:0:12}"
git clone --depth 1 https://github.com/k2-fsa/sherpa-onnx.git "${BUILD_DIR}/sherpa-onnx"
cd "${BUILD_DIR}/sherpa-onnx"
# depth 1 拿不到指定 commit，改用 fetch 补上
git fetch --depth 1 origin "${SHERPA_COMMIT}"
git checkout -q "${SHERPA_COMMIT}"

mkdir -p build-shared
cd build-shared

echo "==> Building sherpa-onnx C API (-j${JOBS})"
SHERPA_ONNXRUNTIME_INCLUDE_DIR="${onnx_path}/include" \
SHERPA_ONNXRUNTIME_LIB_DIR="${onnx_path}/lib" \
cmake \
  -DSHERPA_ONNX_ENABLE_C_API=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install" \
  ..

make -j "${JOBS}"
make install

mkdir -p "${lib_path}"
[ -f "${BUILD_DIR}/install/lib/libonnxruntime.so" ] && chmod +x "${BUILD_DIR}/install/lib/libonnxruntime.so" || true
[ -f "${BUILD_DIR}/install/lib/libsherpa-onnx-c-api.so" ] && chmod +x "${BUILD_DIR}/install/lib/libsherpa-onnx-c-api.so" || true
# 旧版用 mv，会在重复安装时因目标已存在而失败；用 cp -r 保证幂等。
rm -rf "${lib_path}/include" "${lib_path}/lib"
cp -r "${BUILD_DIR}/install/include" "${lib_path}/include"
cp -r "${BUILD_DIR}/install/lib" "${lib_path}/lib"

if [ "${SKIP_MODEL}" -eq 1 ]; then
    echo "==> --skip-model: not downloading ASR/TTS models"
else
    cd "${ROOT}/Data" || exit 1
    fetch_model() {
        local url="$1" fname="$2"
        if [ -s "${fname}" ] || [ -d "${fname%.tar.bz2}" ]; then
            echo "==> ${fname} already present, skipping"
            return 0
        fi
        echo "==> Downloading ${fname}"
        wget -q --show-progress "${url}" -O "${fname}" || {
            echo "WARNING: failed to download ${fname}" >&2
            rm -f "${fname}"
            return 1
        }
        tar xvf "${fname}"
        rm -f "${fname}"
    }
    # ⚠ 这里下载的模型必须与 Data/default_config.yaml 引用的路径**逐字一致**，
    #   否则"模型装好了但程序说找不到"。另外刻意选 small 版：非 small 的
    #   sherpa-onnx-paraformer-zh-2024-03-09 整包近 1GB，而 small 版只有 ~80MB，
    #   识别效果对本项目足够（scripts/fetch_models.py 与 scripts/setup.py 用的也是它）。
    fetch_model "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-paraformer-zh-small-2024-03-09.tar.bz2" \
                "sherpa-onnx-paraformer-zh-small-2024-03-09.tar.bz2" || true
    # TTS 同理：配置里 sherpa_tts.model 指的是 vits-zh-aishell3（不是 matcha），
    # 早期这里下的是 matcha 包，装完等于白下。
    fetch_model "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-zh-aishell3.tar.bz2" \
                "vits-zh-aishell3.tar.bz2" || true
fi

echo ""
echo "==> sherpa-onnx installed into ${lib_path}"
echo "    next: python3 scripts/fetch_models.py"