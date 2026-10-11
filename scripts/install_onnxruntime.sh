#!/bin/bash
# 安装 onnxruntime 预编译包到 lib/onnxruntime（按 uname -m 选 x64 / aarch64）
#
# 用法：
#   bash scripts/install_onnxruntime.sh            # 自动选架构
#   bash scripts/install_onnxruntime.sh --version 1.17.1
#
# 说明：官方只提供 linux-x64 与 linux-aarch64 预编译包。其它架构
# （armv7l / i686）会退回源码构建，那条路需要 cmake + 完整编译链。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"
lib_path="${ROOT}/lib/onnxruntime"
version=1.17.1

while [ $# -gt 0 ]; do
    case "$1" in
        --version) version="$2"; shift 2 ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

mkdir -p "${lib_path}"

get_arch="$(uname -m)"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ort-XXXXXX")"
# 旧版把 tgz 下载到 CWD（通常是 scripts/），残留会污染仓库。
trap 'rm -rf "${TMP_DIR}"' EXIT

fetch_prebuilt() {
    local os_arch="$1"        # x64 / aarch64
    local pkg="onnxruntime-linux-${os_arch}-${version}"
    local url="https://github.com/microsoft/onnxruntime/releases/download/v${version}/${pkg}.tgz"
    echo "==> Downloading ${pkg}.tgz"
    wget -q --show-progress "${url}" -O "${TMP_DIR}/${pkg}.tgz"
    tar xzf "${TMP_DIR}/${pkg}.tgz" -C "${TMP_DIR}"
    # 两个架构的包内部结构一致：<pkg>/include, <pkg>/lib
    rm -rf "${lib_path}/include" "${lib_path}/lib"
    cp -r "${TMP_DIR}/${pkg}/include" "${lib_path}/include"
    cp -r "${TMP_DIR}/${pkg}/lib" "${lib_path}/lib"
}

case "${get_arch}" in
    x86_64)
        fetch_prebuilt x64
        ;;
    aarch64|arm64)
        fetch_prebuilt aarch64
        ;;
    *)
        echo "No prebuilt onnxruntime for '${get_arch}', building from source" >&2
        echo "(this takes 20-40 min; only x86_64 / aarch64 are supported officially)" >&2
        git clone --depth 1 --recursive https://github.com/Microsoft/onnxruntime.git "${TMP_DIR}/onnxruntime"
        cd "${TMP_DIR}/onnxruntime"
        ./build.sh --config RelWithDebInfo --build_shared_lib --parallel \
                   --compile_no_warning_as_error --skip_submodule_sync
        # 旧版这里 mv 到 ~/projects/lowpower-robot/lib/... —— 那是一条早已
        # 废弃的绝对路径，重复安装会失败。改为直接落到本仓库的 lib/。
        rm -rf "${lib_path}/include" "${lib_path}/lib"
        mkdir -p "${lib_path}/include"
        cp -r build/Linux/RelWithDebInfo/_deps/onnxruntime-src/onnxruntime/core/providers/shared/lib \
              "${lib_path}/include/" 2>/dev/null || true
        cp -r build/Linux/RelWithDebInfo/lib/. "${lib_path}/lib/"
        ;;
esac

echo ""
echo "==> onnxruntime installed into ${lib_path}"
ls -1 "${lib_path}/lib" | sed 's/^/    /'
