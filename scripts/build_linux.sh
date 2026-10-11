#!/bin/bash
# ---------------------------------------------------------------------------
# build_linux.sh —— Linux 一键构建（对应 scripts/build_windows.ps1）
#
# 目标平台：
#   - x86_64  Ubuntu 22.04（主目标）
#   - aarch64 Raspberry Pi OS / Ubuntu 22.04 arm64（树莓派 4b/5）
#
# 用法：
#   bash scripts/build_linux.sh                          # 自动探测 Qt6
#   bash scripts/build_linux.sh --qt-dir /opt/Qt/6.8.4/gcc_64
#   bash scripts/build_linux.sh --config RelWithDebInfo --jobs 8
#   bash scripts/build_linux.sh --preset linux-gcc
#   bash scripts/build_linux.sh --with-rasa              # 额外装 rasa venv（兜底 NLU）
#   bash scripts/build_linux.sh --skip-deps              # 跳过依赖安装
#   bash scripts/build_linux.sh --no-plugin-deploy       # 不跑 linuxdeployqt
#
# 与 Windows 版的差异（都是平台固有的，不是功能不对等）：
#   - 依赖按 uname -m 选：onnxruntime/sherpa-onnx 官方只有 x64 与 aarch64 预编译包。
#     树莓派上 sherpa-onnx 若无对应 wheel，会退回本脚本的 --build-sherpa 分支（慢）。
#   - Qt 运行时部署用 linuxdeployqt（AppImage）；若系统已有 Qt6 开发包并希望
#     直接跑，可加 --no-plugin-deploy 只做软链。
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"

QT_DIR="${QT_ROOT:-}"
CONFIG="RelWithDebInfo"
BUILD_DIR=""
GENERATOR=""
PRESET=""
JOBS="$(nproc 2>/dev/null || echo 4)"
SKIP_DEPS=0
WITH_RASA=0
DEPLOY_QT=1
BUILD_SHERPA=0

usage() {
    sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --qt-dir)     QT_DIR="$2"; shift 2 ;;
        --qt-dir=*)   QT_DIR="${1#*=}"; shift ;;
        --config)     CONFIG="$2"; shift 2 ;;
        --build-dir)  BUILD_DIR="$2"; shift 2 ;;
        --generator)  GENERATOR="$2"; shift 2 ;;
        --preset)     PRESET="$2"; shift 2 ;;
        --jobs|-j)    JOBS="$2"; shift 2 ;;
        --skip-deps)  SKIP_DEPS=1; shift ;;
        --with-rasa)  WITH_RASA=1; shift ;;
        --build-sherpa) BUILD_SHERPA=1; shift ;;
        --no-plugin-deploy) DEPLOY_QT=0; shift ;;
        -h|--help)    usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# 0. 环境探测
# ---------------------------------------------------------------------------
ARCH="$(uname -m)"
case "${ARCH}" in
    x86_64)         ORT_ARCH="x64";     SHERPA_ARCH="x64" ;;
    aarch64|arm64)  ORT_ARCH="aarch64"; SHERPA_ARCH="aarch64" ;;
    *) echo "unsupported architecture: ${ARCH}" >&2
       echo "  supported: x86_64, aarch64" >&2
       exit 1 ;;
esac
echo "==> Platform: ${ARCH} (onnxruntime/sherpa arch: ${ORT_ARCH})"

for tool in cmake g++ make; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "ERROR: ${tool} not found. Install build deps:" >&2
        echo "  sudo apt install build-essential cmake" >&2
        exit 1
    }
done

# ---------------------------------------------------------------------------
# 1. 定位 Qt6
#    注意：Windows 强制 Qt>=6.8.4（MSVC 编译器要求），Linux 这条限制来自
#    Qt 自身的平台插件，没那么硬 —— 只要代码用到的 API 在即可。所以这里
#    只要求 Qt6，不设 6.8.4 下限，但仍会打印实际版本。
# ---------------------------------------------------------------------------
if [ -z "${QT_DIR}" ]; then
    # 常见安装位置：aqtinstall 装的 /opt/Qt、官方包的 ~/Qt、发行版包
    # （发行版包的 cmake 配置文件在 /usr/lib/<triplet>/cmake/Qt6，此时 QT_DIR
    #  应指向 /usr，不然 CMAKE_PREFIX_PATH 找不到 Qt6Config.cmake）
    for cand in \
        /opt/Qt/*/gcc_64 \
        /opt/Qt/*/gcc_arm64 \
        "${HOME}"/Qt/*/gcc_64 \
        "${HOME}"/Qt/*/gcc_arm64 ; do
        if [ -f "${cand}/lib/cmake/Qt6/Qt6Config.cmake" ]; then
            QT_DIR="${cand}"
            break
        fi
    done
    if [ -z "${QT_DIR}" ]; then
        TRILET="$("${CXX:-g++}" -print-multiarch 2>/dev/null || true)"
        for cand in "/usr/lib/${TRILET}/cmake/Qt6" /usr/lib/cmake/Qt6 /usr/local/lib/cmake/Qt6; do
            if [ -f "${cand}/Qt6Config.cmake" ]; then
                QT_DIR="$(dirname "$(dirname "$(dirname "${cand}")")")"
                break
            fi
        done
    fi
fi
if [ -z "${QT_DIR}" ] || [ ! -f "${QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    cat >&2 <<EOF
ERROR: Qt6 not found. Pass --qt-dir pointing at the Qt 6 gcc dir, e.g.
       bash scripts/build_linux.sh --qt-dir /opt/Qt/6.8.4/gcc_64
     or export QT_ROOT=/opt/Qt/6.8.4/gcc_64
  Ubuntu 22.04: sudo apt install qt6-base-dev qt6-multimedia-dev
                 qt6-declarative-dev libqt6network6
EOF
    exit 1
fi
QMAKE="${QT_DIR}/bin/qmake"
[ -x "${QMAKE}" ] && QT_VER="$("${QMAKE}" -query QT_VERSION)" || QT_VER="(unknown)"
echo "==> Qt: ${QT_DIR} (version ${QT_VER})"

# ---------------------------------------------------------------------------
# 2. 第三方依赖
# ---------------------------------------------------------------------------
need_ort()    { [ ! -f "${ROOT}/lib/onnxruntime/lib/libonnxruntime.so" ]; }
need_sherpa() { [ ! -d "${ROOT}/lib/sherpa_onnx/include" ]; }
# yaml-cpp 是**configure 阶段**的硬依赖（find_package(yaml_cpp REQUIRED)）：
# 缺了它连 cmake 都跑不完，报 "Could NOT find yaml_cpp"。lib/ 不入库，
# 所以新 clone 的仓库必须装一次。
need_yaml()   { [ ! -f "${ROOT}/lib/yaml-cpp/lib/libyaml-cpp.a" ] \
                && [ ! -f "${ROOT}/lib/yaml-cpp/lib/yaml-cpp.lib" ]; }

if [ "${SKIP_DEPS}" -eq 0 ]; then
    if need_yaml; then
        echo "==> Installing yaml-cpp (config.yaml 的 YAML 读写依赖)"
        bash "${SCRIPT_DIR}/install_yaml_cpp.sh"
    else
        echo "==> yaml-cpp present, skip"
    fi
    if need_ort; then
        echo "==> Installing onnxruntime (${ORT_ARCH})"
        bash "${SCRIPT_DIR}/install_onnxruntime.sh"
    else
        echo "==> onnxruntime present, skip"
    fi
    if need_sherpa; then
        if [ "${BUILD_SHERPA}" -eq 1 ]; then
            echo "!! sherpa-onnx will be built from source (30-60 min on a Pi)"
        else
            echo "==> Installing sherpa-onnx (${SHERPA_ARCH})"
        fi
        bash "${SCRIPT_DIR}/install_sherpa_onnx.sh"
    else
        echo "==> sherpa-onnx present, skip"
    fi
    if [ "${WITH_RASA}" -eq 1 ]; then
        echo "==> Installing rasa venv"
        bash "${SCRIPT_DIR}/install_rasa.sh"
    fi
else
    echo "==> --skip-deps: not touching lib/"
fi

# ---------------------------------------------------------------------------
# 3. 运行期配置
#    CMake 把 Data/ 拷贝到 ~/.config/QSmartAssistant/Data。这里先生成
#    config.yaml（不覆盖已有的），避免首次运行时到处找不到配置。
#    老版本 config.json 仍可被程序读取并自动迁移成 yaml。
# ---------------------------------------------------------------------------
DATA_DIR="${ROOT}/Data"
if [ ! -f "${DATA_DIR}/config.yaml" ] && [ ! -f "${DATA_DIR}/config.json" ]; then
    cp "${DATA_DIR}/default_config.yaml" "${DATA_DIR}/config.yaml"
    echo "==> Generated Data/config.yaml from default_config.yaml"
else
    echo "==> Data/config.yaml (or legacy config.json) already exists, keeping it"
fi
if [ ! -f "${DATA_DIR}/plugin_order.txt" ]; then
    cp "${DATA_DIR}/plugin_order_default.txt" "${DATA_DIR}/plugin_order.txt"
    echo "==> Generated Data/plugin_order.txt"
fi

# ---------------------------------------------------------------------------
# 4. configure + build
# ---------------------------------------------------------------------------
if [ -z "${BUILD_DIR}" ]; then BUILD_DIR="${ROOT}/build"; fi

if [ -n "${PRESET}" ]; then
    CMAKE_ARGS=(--preset "${PRESET}")
else
    CMAKE_ARGS=(-S "${ROOT}" -B "${BUILD_DIR}" -DCMAKE_PREFIX_PATH="${QT_DIR}")
    if [ -n "${GENERATOR}" ]; then
        CMAKE_ARGS+=(-G "${GENERATOR}")
    fi
    if [ "${CONFIG}" != "Release" ]; then
        # Ninja/Make 是单配置生成器；Unix Makefiles 会把 $CACHE_BUILD_TYPE 传进来，
        # 这里显式给。若用 Ninja Multi-Config 则不该传。
        CMAKE_ARGS+=(-DCMAKE_BUILD_TYPE="${CONFIG}")
    fi
    # 模块组合：默认安装 = livekit-wakeword 唤醒 + silero VAD + sherpa ASR/TTS
    # + ONNX NLU（进程内，无需 rasa）。这一套全跨平台、纯 onnxruntime，
    # 不依赖厂商私有 .so；duilite 需要时自己加 -DWAKEUP_DUILITE=ON。
    # 这里仍显式写出，避免依赖 CMake 默认值（旧的默认值是 sherpa-kws + rasa）。
    CMAKE_ARGS+=(
        -DWAKEUP_LIVEKIT=ON -DWAKEUP_SHERPA=OFF -DWAKEUP_DUILITE=OFF
        -DVAD_SILERO=ON     -DVAD_DUILITE=OFF
        -DASR_SHERPA=ON     -DTTS_SHERPA=ON
        -DNLU_ONNX=ON       -DNLU_RASA=OFF      -DNLU_BAIDU=OFF
        -DUSE_OPENSSL=OFF
        # 网易云插件走 node 版 API（与 Windows 默认一致）。
        # Linux 上该选项的默认值是 OFF，那会走 FetchContent —— 在 **configure
        # 阶段** git clone 第三方仓库（含 submodule）并把它编进来，既是隐式的
        # 网络依赖，又会在源码树里写 lib/QCloudMusicApi。node 模式构建期零依赖，
        # 且 doc/plugin.md 里本来就写着"推荐使用 js 的 api"。
        -DNETEASE_USE_JS=ON
    )
fi

echo "==> CMake configure: ${CMAKE_ARGS[*]}"
cmake "${CMAKE_ARGS[@]}"

echo "==> Build (-j${JOBS})"
BUILD_ARGS=(--build "${BUILD_DIR}" -j "${JOBS}")
if [ -n "${PRESET}" ]; then
    # 有 preset 时构建目录由 preset 的 binaryDir 决定，不要再传 --build。
    BUILD_ARGS=(--preset "${PRESET}")
fi
cmake "${BUILD_ARGS[@]}"

# ---------------------------------------------------------------------------
# 5. 找可执行文件 + 部署 Qt 运行时
# ---------------------------------------------------------------------------
EXE="$(find "${BUILD_DIR}" -type f -name QSmartAssistant -perm -u+x 2>/dev/null | head -n 1 || true)"
if [ -z "${EXE}" ]; then
    echo "ERROR: QSmartAssistant binary not found under ${BUILD_DIR}" >&2
    exit 1
fi
OUT_DIR="$(dirname "${EXE}")"
echo "==> Binary: ${EXE}"

if [ "${DEPLOY_QT}" -eq 1 ]; then
    if command -v linuxdeployqt >/dev/null 2>&1; then
        echo "==> Deploying Qt runtime with linuxdeployqt (AppImage tool)"
        # -unsupported-allow-new-glibc: 树莓派 OS 是 glibc 2.36，linuxdeployqt 自带的
        # 检测器常把它当"太新"而拒绝，这里显式放行（否则每次都要手工加）。
        linuxdeployqt "${EXE}" -appimage -unsupported-allow-new-glibc -no-translations
    else
        echo "!! linuxdeployqt not found. Install it with:" >&2
        echo "     wget -O /tmp/linuxdeployqt.AppImage \\" >&2
        echo "       https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage" >&2
        echo "     chmod +x /tmp/linuxdeployqt.AppImage && sudo mv /tmp/linuxdeployqt.AppImage /usr/local/bin/" >&2
        echo "   Or run with --no-plugin-deploy to use system Qt6 directly." >&2
    fi
fi

# ---------------------------------------------------------------------------
# 6. 汇总
# ---------------------------------------------------------------------------
PLUGIN_LIB="${HOME}/.config/QSmartAssistant/plugins/lib"
cat <<EOF

Build finished.
  Executable : ${EXE}
  Plugins dir: ${PLUGIN_LIB}
  Data dir   : ${HOME}/.config/QSmartAssistant/Data

Remaining runtime setup:
  # ASR / wakeup / TTS models (the sherpa-onnx installer fetches some already)
  python3 scripts/fetch_models.py

  # NLU: default backend is the in-process ONNX model (no Python needed).
  # It ships with the repo under Data/onnx_nlu/. Only install the rasa venv
  # if you deliberately switch config.json -> "nlu": {"backend": "rasa"}.
  # bash scripts/install_rasa.sh

  # Optional: local GGUF LLM backend
  bash scripts/install_llama.sh --backend cpu
  python3 scripts/fetch_llm_model.py --quant Q4_K_M

Docs: doc/linux.md  (Windows: doc/windows.md)
EOF