#!/bin/bash
# ---------------------------------------------------------------------------
# start-rasa.sh —— 由 RasaNLU 调用（C++ 侧 rasanlu.cpp 的 runRasash）。
#
# 用法（不要手工改调用约定，rasanlu.cpp 依赖它）：
#   ./start-rasa.sh <venv 的 activate 脚本> [模型 tar.gz 绝对路径]
#
# 参数说明：
#   $1  activate 脚本路径。rasanlu.cpp 里的 resolveRasaVenv() 负责找到它，
#       找不到时传空串 —— 此时本脚本退回自动探测（见下方 venv_probe）。
#   $2  可选。指定模型；不给则由 rasa 自己用工作目录下的默认模型。
#
# 相比旧版的修复（每一处都是会静默出错的）：
#   1. `source $1` 未加引号 —— venv 路径含空格时直接语法错误。这里全程 "$1"。
#   2. `cd ~/.config/...` 硬编码 HOME —— 用 $QSA_DATA_DIR（可由 C++ 或
#      systemd 传入）或回退到 ~/.config/QSmartAssistant/Data，支持非 root 运行。
#   3. 旧版把 `echo $! > rasa.pid`，但那取的是 nohup 的 pid；一旦后台化失败
#      会写进一个无效 pid。这里显式 `rasa run ... & echo $!` 并校验进程存活。
#   4. 旧版没有 source 失败的兜底：activate 没找到时 rasa 命令不存在，
#      nohup 报错后脚本照样 exit 0，C++ 侧以为启动成功。这里逐级校验 + 非零退出。
#   5. 端口占用（旧 rasa 还在跑）会让新进程起不来。这里先清理再等端口释放。
#   6. 日志写到 rasa.log —— 与 Windows 分支（rasanlu.cpp 里
#      setStandardOutputFile(.../rasa/rasa.log)）同名，排障时两边一致。
# ---------------------------------------------------------------------------
set -uo pipefail

# ---- 数据目录：优先外部传入，其次 HOME 推导 --------------------------------
if [ -n "${QSA_DATA_DIR:-}" ]; then
    DATA_DIR="${QSA_DATA_DIR}"
else
    DATA_DIR="${HOME}/.config/QSmartAssistant/Data"
fi
RASA_DIR="${DATA_DIR}/rasa"

if [ ! -d "${RASA_DIR}" ]; then
    echo "start-rasa.sh: rasa data dir not found: ${RASA_DIR}" >&2
    exit 1
fi

# ---- 定位 venv 的 activate 脚本 ---------------------------------------------
# 依次尝试：显式传入 → QSA_RASA_VENV → 安装脚本默认位置 → 源码树 lib/rasa。
# 与 C++ 侧 resolveRasaVenv() 的候选顺序保持一致。
venv_probe() {
    local c
    for c in "$@"; do
        [ -n "${c}" ] || continue
        if [ -f "${c}" ]; then echo "${c}"; return 0; fi
        # venv 根目录 → bin/activate
        if [ -f "${c}/bin/activate" ]; then echo "${c}/bin/activate"; return 0; fi
    done
    return 1
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

ACTIVATE="$(venv_probe \
    "${1:-}" \
    "${QSA_RASA_VENV:-}" \
    "${HOME}/.config/QSmartAssistant/rasa-venv/bin/activate" \
    "$(dirname "${SCRIPT_DIR}")/lib/rasa/bin/activate" \
    "lib/rasa/bin/activate")" || true

if [ -z "${ACTIVATE}" ]; then
    echo "start-rasa.sh: could not locate the rasa venv activate script." >&2
    echo "  Install it with:  bash scripts/install_rasa.sh" >&2
    echo "  Or point to it explicitly:" >&2
    echo "    config.json -> rasa.python_venv   (venv root dir or activate path)" >&2
    echo "    env          QSA_RASA_VENV" >&2
    exit 2
fi

# shellcheck disable=SC1090
source "${ACTIVATE}"
if ! command -v rasa >/dev/null 2>&1; then
    echo "start-rasa.sh: 'rasa' not on PATH after sourcing ${ACTIVATE}" >&2
    exit 3
fi

# ---- 关掉可能残留的旧实例 ---------------------------------------------------
if [ -f "${SCRIPT_DIR}/stop-rasa.sh" ]; then
    bash "${SCRIPT_DIR}/stop-rasa.sh" >/dev/null 2>&1 || true
fi

# 等端口释放：kill 之后监听套接字不会立刻消失，抢跑会 bind 失败。
PORT="${QSA_RASA_PORT:-5005}"
for _ in $(seq 1 20); do
    if command -v fuser >/dev/null 2>&1; then
        fuser -s "${PORT}/tcp" || break
    elif command -v ss >/dev/null 2>&1; then
        ss -ltn 2>/dev/null | grep -q ":${PORT} " || break
    else
        break
    fi
    sleep 0.5
done

cd "${RASA_DIR}" || exit 4

if [ $# -ge 2 ] && [ -n "${2:-}" ]; then
    nohup rasa run --enable-api -m "$2" > rasa.log 2>&1 &
else
    nohup rasa run --enable-api > rasa.log 2>&1 &
fi
RASA_PID=$!

# 进程刚 fork 出来就退出是常见情形（模型缺失、端口占用）。落 pid 前先验存活，
# 否则会写进一个已经死掉的 pid，stop-rasa.sh 之后就只能对着空操作报错。
sleep 1
if ! kill -0 "${RASA_PID}" 2>/dev/null; then
    echo "start-rasa.sh: rasa exited immediately. Last 20 log lines:" >&2
    tail -n 20 "${RASA_DIR}/rasa.log" >&2 2>/dev/null
    exit 5
fi

echo "${RASA_PID}" > "${RASA_DIR}/rasa.pid"
echo "start-rasa.sh: rasa started, pid=${RASA_PID}, log=${RASA_DIR}/rasa.log"
exit 0