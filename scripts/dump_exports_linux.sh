#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# dump_exports_linux.sh —— 列出 ELF 动态库的导出符号（Linux 版 dump_pe_exports.py）
#
# 用途：llama.cpp 官方只发二进制包（无头文件、无 .so 符号链接），
# 要直接 dlopen libllama.so 就得先确认它导出了哪些 C API 符号。
#
# 为什么不用 Python 重写 ELF 解析：nm/readelf 是系统自带的 binutils，
# 解析 ELF 是它们的本职，比在脚本里手写 section header 可靠得多。
#
# 用法：
#   bash scripts/dump_exports_linux.sh lib/llama/bin/libllama.so
#   bash scripts/dump_exports_linux.sh lib/llama/bin/libllama.so llama_model   # 过滤
#   bash scripts/dump_exports_linux.sh lib/llama/bin/libggml.so ggml_backend  # 找后端符号
#
# 与 Windows 版的差异：
#   PE  用 dump_pe_exports.py（自己解析 PE 导出目录，因为 Windows 没 nm）
#   ELF 用 nm -D（binutils 自带）
#   C++ 侧要 resolve 的关键符号在两边同名，只是文件后缀不同。
# ---------------------------------------------------------------------------
set -uo pipefail

usage() {
    sed -n '3,16p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    usage "${1:-0}"
fi

LIB="$1"
FILTER="${2:-}"

if [ ! -f "${LIB}" ]; then
    echo "not a file: ${LIB}" >&2
    echo "install it first:  bash scripts/install_llama.sh" >&2
    exit 1
fi

# ---- 选可用的工具（nm 优先，readelf 兜底）----
DUMP=""
if command -v nm >/dev/null 2>&1; then
    # -D 只看动态符号表；--defined-only 排除未定义(外部引用)符号。
    # Linux 上 nm 的语法是 `nm -D --defined-only <file>`，不是 `nm --defined-only -D`。
    DUMP="nm -D --defined-only"
elif command -v readelf >/dev/null 2>&1; then
    DUMP="readelf --dyn-syms"
elif command -v objdump >/dev/null 2>&1; then
    DUMP="objdump -T"
else
    cat >&2 <<EOF
ERROR: none of nm / readelf / objdump found.
  sudo apt install binutils
EOF
    exit 1
fi

# 确认这确实是 ELF（避免对着 .dll 跑 nm，报出一堆看不懂的错误）
if ! head -c 4 "${LIB}" | grep -q $'\x7fELF'; then
    echo "ERROR: ${LIB} is not an ELF file (missing the \\x7fELF magic)." >&2
    echo "On Linux you want lib/llama/bin/libllama.so, not a Windows DLL." >&2
    exit 1
fi

OUT="$(mktemp)"
trap 'rm -f "${OUT}"' EXIT

# shellcheck disable=SC2086
${DUMP} "${LIB}" 2>/dev/null \
    | awk '{ $1=""; $2=""; sub(/^ +/, ""); if ($0 != "") print }' \
    | sed 's/^[ \t]*//; s/[ \t]*$//' \
    | sort -u > "${OUT}"

if [ -n "${FILTER}" ]; then
    grep -- "${FILTER}" "${OUT}" || true
else
    cat "${OUT}"
fi
COUNT="$(grep -c -- "${FILTER}" "${OUT}" 2>/dev/null || echo 0)"
echo "---- total: $(wc -l < "${OUT}" | tr -d ' ') (matched ${COUNT}) ----"

# ---- 应用真正依赖的符号，给出明确结论而不是让人自己比对长列表 ----
if [ -z "${FILTER}" ]; then
    echo ""
    echo "Sanity check — symbols LlamaApi.cpp resolves:"
    for sym in llama_backend_init llama_model_load_from_file \
               llama_model_free llama_free llama_decode llama_sampler_chain_init; do
        if grep -qx -- "${sym}" "${OUT}"; then
            echo "  OK      ${sym}"
        else
            echo "  MISSING ${sym}"
        fi
    done
    echo ""
    echo "And the backend loader (lives in libggml.so, not libllama.so):"
    for lib in "$(dirname "${LIB}")"/libggml*.so*; do
        [ -f "${lib}" ] || continue
        for sym in ggml_backend_load_all_from_path ggml_backend_load_all; do
            if nm -D --defined-only "${lib}" 2>/dev/null | awk '{print $NF}' | grep -qx -- "${sym}"; then
                echo "  OK      ${sym} in $(basename "${lib}")"
            else
                echo "  MISSING ${sym} in $(basename "${lib}")"
            fi
        done
        break
    done
fi