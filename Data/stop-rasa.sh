#!/bin/bash
# ---------------------------------------------------------------------------
# stop-rasa.sh —— 由 RasaNLU 调用（C++ 侧 rasanlu.cpp 的 runRasash），
# 也可手工执行：bash Data/stop-rasa.sh
#
# 相比旧版的修复：
#   1. 硬编码 ~/.config/... → 支持 QSA_DATA_DIR（与 start-rasa.sh 同一约定）。
#   2. `cat rasa.pid | xargs kill`：pid 文件不存在时 cat 报错、xargs 拿空参数
#      去跑 kill（等价于 `kill` 列出全部信号），在别的进程上误杀。这里逐项校验。
#   3. kill 默认 SIGTERM。rasa 加载 TF/torch 后可能忽略 SIGTERM，
#      这里先 TERM 再按需 KILL，并等端口真正释放。
#   4. 旧版残留 log.txt / 无 rasa.pid 时静默失败；现在统一用 rasa.log。
#   5. 结束清理 rasa.pid，避免下次 start-rasa.sh 读到陈旧 pid。
# ---------------------------------------------------------------------------
set -uo pipefail

if [ -n "${QSA_DATA_DIR:-}" ]; then
    DATA_DIR="${QSA_DATA_DIR}"
else
    DATA_DIR="${HOME}/.config/QSmartAssistant/Data"
fi
RASA_DIR="${DATA_DIR}/rasa"
PID_FILE="${RASA_DIR}/rasa.pid"
PORT="${QSA_RASA_PORT:-5005}"

if [ ! -f "${PID_FILE}" ]; then
    # 没有 pid 文件是正常状态（从没启动过 / 已停过），不算错误。
    echo "stop-rasa.sh: no ${PID_FILE}, nothing to stop"
    exit 0
fi

PID="$(tr -d '[:space:]' < "${PID_FILE}")"
if [ -z "${PID}" ] || ! [ "${PID}" -eq "${PID}" ] 2>/dev/null; then
    echo "stop-rasa.sh: bogus pid content in ${PID_FILE}, removing it"
    rm -f "${PID_FILE}"
    exit 0
fi

if ! kill -0 "${PID}" 2>/dev/null; then
    echo "stop-rasa.sh: pid ${PID} is not running, removing stale pid file"
    rm -f "${PID_FILE}"
    exit 0
fi

echo "stop-rasa.sh: sending SIGTERM to ${PID}"
kill "${PID}" 2>/dev/null || true

# 等最多 10s；不响应再 SIGKILL
for _ in $(seq 1 20); do
    kill -0 "${PID}" 2>/dev/null || break
    sleep 0.5
done
if kill -0 "${PID}" 2>/dev/null; then
    echo "stop-rasa.sh: ${PID} ignored SIGTERM, sending SIGKILL"
    kill -9 "${PID}" 2>/dev/null || true
    sleep 0.5
fi

rm -f "${PID_FILE}"

# 端口还占着说明还有别的实例（可能被 systemd 拉起过），提示但不阻塞。
if command -v fuser >/dev/null 2>&1; then
    if fuser -s "${PORT}/tcp" 2>/dev/null; then
        echo "stop-rasa.sh: warning - port ${PORT} still in use by another process"
    fi
elif command -v ss >/dev/null 2>&1; then
    if ss -ltn 2>/dev/null | grep -q ":${PORT} "; then
        echo "stop-rasa.sh: warning - port ${PORT} still in use by another process"
    fi
fi

echo "stop-rasa.sh: stopped ${PID}"
exit 0