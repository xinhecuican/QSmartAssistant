#!/bin/bash
# ---------------------------------------------------------------------------
# install_yaml_cpp.sh —— 安装 yaml-cpp（静态库）到 lib/yaml-cpp
#
# 为什么必须有这个脚本
# --------------------
# `lib/` 整个目录都在 .gitignore 里，所以**新 clone 的仓库没有 lib/yaml-cpp**；
# 而顶层 CMakeLists.txt 第 141 行是 `find_package(yaml_cpp REQUIRED)` —— 缺它
# 连 configure 都过不去：
#     Could NOT find yaml_cpp (missing: yaml_cpp_LIBRARY)
# 配置自 2026-10 起是 YAML，而 Qt 没有 YAML 模块，只能用 yaml-cpp。
#
# 产出布局（与 cmake/Findyaml_cpp.cmake 的约定一致）
# ------------------------------------------------
#     lib/yaml-cpp/include/yaml-cpp/*.h
#     lib/yaml-cpp/lib/libyaml-cpp.a
#
# ⚠ 两个必须满足的约束，改这个脚本时别破坏：
#   1. **静态库**。与 Windows 侧的 yaml-cpp.lib 保持一致；Findyaml_cpp.cmake 会
#      附带 INTERFACE_COMPILE_DEFINITIONS=YAML_CPP_STATIC_DEFINE，动态库会让
#      yaml-cpp/dll.h 在 Windows 上按 __declspec(dllimport) 展开（LNK2019）。
#   2. **带 PIC**。yaml-cpp 被 lpcommon（SHARED）链接，不开 PIC 链接 .so 时会报
#      "relocation R_X86_64_PC32 against symbol ... can not be used when making a
#       shared object"。0.8.0 的 CMakeLists 对静态构建已经默认开 PIC，
#      这里仍显式传一次，避免换版本后静默退化成不可用。
#
# 用法
# ----
#     bash scripts/install_yaml_cpp.sh               # 默认 0.8.0
#     bash scripts/install_yaml_cpp.sh --version 0.9.0
#     bash scripts/install_yaml_cpp.sh --force       # 已装也重装
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"
LIB_PATH="${ROOT}/lib/yaml-cpp"

# 0.8.0 的 git tag 就叫 "0.8.0"（不是 yaml-cpp-0.8.0），别照着 0.7.0 的命名去猜。
VERSION="0.8.0"
FORCE=0
JOBS="$(nproc 2>/dev/null || echo 4)"

while [ $# -gt 0 ]; do
    case "$1" in
        --version)   VERSION="$2"; shift 2 ;;
        --force)     FORCE=1; shift ;;
        --jobs|-j)   JOBS="$2"; shift 2 ;;
        -h|--help)   sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

STAMP="${LIB_PATH}/lib/libyaml-cpp.a"
if [ "${FORCE}" -eq 0 ] && [ -f "${STAMP}" ] \
   && [ -f "${LIB_PATH}/include/yaml-cpp/yaml.h" ]; then
    echo "==> yaml-cpp already present (${LIB_PATH}), skipping (use --force)"
    exit 0
fi

for tool in cmake git; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "ERROR: ${tool} not found. Install build deps:" >&2
        echo "  sudo apt install build-essential cmake git" >&2
        exit 1
    }
done

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/yaml-cpp-XXXXXX")"
trap 'rm -rf "${TMP_DIR}"' EXIT

echo "==> Cloning yaml-cpp ${VERSION}"
git clone --depth 1 --branch "${VERSION}" \
    https://github.com/jbeder/yaml-cpp.git "${TMP_DIR}/src"

# contrib 不在本项目的使用范围内，关掉可以少编一堆用不到的东西
# （AnchoredDict / GraphBuilder 都没有被引用）。
#
# CMAKE_POLICY_VERSION_MINIMUM=3.5：yaml-cpp 0.8.0 写的是
# `cmake_minimum_required(VERSION 3.4)`，而 **CMake 4.x 起不再兼容 < 3.5**，
# 不加这句会直接报 "Compatibility with CMake < 3.5 has been removed"。
# 老版本 CMake 不认识这个变量，传了也只是忽略，所以无条件带上。
echo "==> Building yaml-cpp (static, PIC) -j${JOBS}"
cmake -S "${TMP_DIR}/src" -B "${TMP_DIR}/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_SHARED_LIBS=OFF \
    -DYAML_BUILD_SHARED_LIBS=OFF \
    -DYAML_CPP_BUILD_TESTS=OFF \
    -DYAML_CPP_BUILD_TOOLS=OFF \
    -DYAML_CPP_BUILD_CONTRIB=OFF \
    -DYAML_CPP_INSTALL=ON \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="${LIB_PATH}" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    > "${TMP_DIR}/configure.log" 2>&1 || {
        cat "${TMP_DIR}/configure.log" >&2
        echo "ERROR: yaml-cpp configure failed" >&2
        exit 1
    }

cmake --build "${TMP_DIR}/build" -j "${JOBS}"
cmake --install "${TMP_DIR}/build"

# 自检：Findyaml_cpp.cmake 找的就是这两个名字。少一个都会让 CI/本地在
# configure 阶段失败，且错误信息（"missing: yaml_cpp_LIBRARY"）完全指不出原因。
MISSING=0
[ -f "${LIB_PATH}/include/yaml-cpp/yaml.h" ] || { echo "ERROR: yaml-cpp/yaml.h missing" >&2; MISSING=1; }
[ -f "${STAMP}" ] || {
    echo "ERROR: ${STAMP} not produced. Actual layout:" >&2
    find "${LIB_PATH}" -maxdepth 2 2>/dev/null | sed 's/^/    /' >&2
    MISSING=1
}
[ "${MISSING}" -eq 0 ] || exit 1

echo ""
echo "==> yaml-cpp ${VERSION} installed into ${LIB_PATH}"
echo "    include : ${LIB_PATH}/include"
echo "    library : ${STAMP}"
