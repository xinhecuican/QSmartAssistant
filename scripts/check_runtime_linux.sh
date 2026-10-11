#!/bin/bash
# ---------------------------------------------------------------------------
# check_runtime_linux.sh —— 部署后自检：可执行文件与插件的动态依赖是否齐全
#
# Linux 版对应 Windows 的 windeployqt / deploy_qt.py 场景。windeployqt 会把
# Qt 运行时一起打包，而 Linux 上更常见的是「依赖系统 Qt」——这时一旦某个
# .so 没装，程序会在**启动瞬间**报
#     ./QSmartAssistant: error while loading shared libraries: libxxx.so: cannot open
# 这份脚本把这类"缺依赖"一次性列出来，而不是等运行时报错再逐个猜。
#
# 用法：
#   bash scripts/check_runtime_linux.sh                       # 自动找可执行文件
#   bash scripts/check_runtime_linux.sh /path/to/QSmartAssistant
#   bash scripts/check_runtime_linux.sh --fix                # 尝试从 lib/ 补拷缺失的 .so
#
# 退出码：0 全部齐全 / 1 有缺失
# ---------------------------------------------------------------------------
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"
EXE=""
FIX=0

while [ $# -gt 0 ]; do
    case "$1" in
        --fix) FIX=1; shift ;;
        -h|--help) sed -n '3,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) EXE="$1"; shift ;;
    esac
done

command -v ldd >/dev/null 2>&1 || {
    echo "ERROR: ldd not found (install libc-bin / glibc)." >&2
    exit 1
}

if [ -z "${EXE}" ]; then
    EXE="$(find "${ROOT}/build" -type f -name QSmartAssistant -perm -u+x 2>/dev/null | head -n 1 || true)"
fi
[ -n "${EXE}" ] && [ -f "${EXE}" ] || {
    echo "ERROR: QSmartAssistant binary not found. Pass a path explicitly:" >&2
    echo "  bash scripts/check_runtime_linux.sh /path/to/QSmartAssistant" >&2
    exit 1
}
echo "==> Checking: ${EXE}"

# 插件是运行时 dlopen 的，ldd 主程序看不到它们 —— 必须单独查。
PLUGIN_DIR="${HOME}/.config/QSmartAssistant/plugins/lib"
[ -d "${PLUGIN_DIR}" ] || PLUGIN_DIR="$(find "${ROOT}/build" -maxdepth 3 -type d -name lib -path "*plugins*" 2>/dev/null | head -n 1 || true)"

MISSING=0

check_one() {
    local f="$1" label="$2"
    local out
    # ldd 对非 ELF / 权限不足的文件会报错，先挡掉，避免把工具报错当成依赖缺失。
    if ! head -c 4 "$f" | grep -q $'\x7fELF' 2>/dev/null; then
        return 0
    fi
    out="$(ldd "$f" 2>/dev/null)"
    local miss
    # 形如 "libfoo.so.1 => not found"
    miss="$(echo "${out}" | grep 'not found' | awk '{print $1}' | sort -u)"
    if [ -n "${miss}" ]; then
        MISSING=$((MISSING + 1))
        echo "  [MISSING DEPS] ${label}"
        echo "${miss}" | sed 's/^/      /'
        if [ "${FIX}" -eq 1 ]; then
            fix_one "$f" "$miss"
        fi
        return 1
    fi
    echo "  [ok] ${label}"
    return 0
}

# --fix：从仓库 lib/ 与系统路径里找同名 .so 拷到目标目录。
# 只在系统路径里确实存在时才拷 —— 否则会拷进一个同样坏的文件。
fix_one() {
    local target_dir="$1"; shift
    local miss="$*"
    mkdir -p "${target_dir}"
    for lib in ${miss}; do
        local found=""
        # 先找本仓库 lib/（build_linux.sh 装的那批）
        if [ -f "${ROOT}/lib/${lib}" ]; then
            cp -f "${ROOT}/lib/${lib}" "${target_dir}/" && found="lib/"
        else
            local sys
            sys="$(find /usr/lib /usr/local/lib /lib -name "${lib}" -type f 2>/dev/null | head -n 1 || true)"
            # 只拷系统里真的存在的；没有就报告"需要装包"而不是静默跳过。
            if [ -n "${sys}" ]; then
                cp -f "${sys}" "${target_dir}/" && found="${sys}"
            fi
        fi
        if [ -n "${found}" ]; then
            echo "      copied ${lib} (from ${found})"
        else
            echo "      ${lib}: not found on this system either."
            echo "      Install the providing package, e.g.:"
            echo "        apt-file search ${lib}   # shows which package owns it"
            echo "        sudo apt install <package>"
        fi
    done
}

echo "==> Main binary"
check_one "${EXE}" "$(basename "${EXE}")"

if [ -n "${PLUGIN_DIR}" ] && [ -d "${PLUGIN_DIR}" ]; then
    echo "==> Plugins in ${PLUGIN_DIR}"
    # 插件名 Linux 为 *.so，且带 lib 前缀
    found_any=0
    for so in "${PLUGIN_DIR}"/*.so; do
        [ -f "${so}" ] || continue
        found_any=1
        check_one "${so}" "$(basename "${so}")"
    done
    [ "${found_any}" -eq 0 ] && echo "  (no .so found — did the build produce plugins?)"
else
    echo "==> Plugins: directory not found at ${PLUGIN_DIR}"
    echo "    Plugins are dlopen'd at run time; build them with:"
    echo "      cmake --build <builddir>   (BUILD_PLUGINS=ON by default)"
fi

echo ""
if [ "${MISSING}" -gt 0 ]; then
    echo "==> ${MISSING} file(s) have unresolved dependencies."
    echo "    On Linux the usual fix is to install the distro packages, not to"
    echo "    copy .so files around:  sudo apt install <package>"
    exit 1
fi
echo "==> All dynamic dependencies resolved."
exit 0