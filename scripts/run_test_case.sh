#!/bin/bash
# ---------------------------------------------------------------------------
# run_test_case.sh —— 跑一个 QTest 用例，失败时把关键行以 GitHub annotation 抛出
#
# 为什么需要它
# ------------
# GitHub Actions 的 step 名 / 结论 / annotation 都能用 REST API **匿名**读到，
# 而 **日志不能**（下载 log 需要 admin 权限）。CI 出红时如果只能靠翻日志定位，
# 没有仓库权限的人（以及自动化流程）就完全瞎了。这个脚本把"日志里最要紧的
# 那几行"提升成 annotation —— 等于把失败的断言搬到了可读的地方。
# 另外每个用例占一个独立 step，所以"是哪个用例挂了"看 step 名就知道。
#
# 用法
# ----
#     bash scripts/run_test_case.sh <用例名> <构建目录>
#
#   <用例名>    QTest 的测试函数名，例如 hassRegex / mcpClient
#   <构建目录>  CI 里的单测构建目录（Debug）；二进制在 <构建目录>/Debug/bin/ 下
#               （输出目录规则见顶层 CMakeLists.txt：单配置生成器用
#                ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bin）
#
# 环境变量（由 workflow 的 Test step 提供）：
#   QSA_SOURCE_DIR   仓库根。mcpClient 用它定位 scripts/mock_mcp_server.py
#   QSA_PYTHON       拉起 mock server 用的解释器，默认 python3
#   QT_QPA_PLATFORM  =offscreen（无显示环境）
#
# 退出码 = QTest 的退出码（= 失败断言数），0 表示通过，照原样透传给 CI。
# ---------------------------------------------------------------------------
set -uo pipefail

CASE="${1:?用法: run_test_case.sh <用例名> <构建目录>}"
BUILD_DIR="${2:?用法: run_test_case.sh <用例名> <构建目录>}"
BIN_DIR="${BUILD_DIR}/Debug/bin"

if [ ! -d "${BIN_DIR}" ]; then
    echo "::error title=测试构建目录不存在::${BIN_DIR} 不存在（先跑 Configure & build tests）"
    ls -R "${BUILD_DIR}" 2>/dev/null | head -50 || true
    exit 1
fi

if [ ! -x "${BIN_DIR}/QSmartAssistant" ]; then
    echo "::error title=测试程序不存在::${BIN_DIR}/QSmartAssistant 不存在或不可执行"
    ls -l "${BIN_DIR}" 2>/dev/null | head -50 || true
    exit 1
fi

LOG="$(mktemp)"
trap 'rm -f "${LOG}"' EXIT

cd "${BIN_DIR}" || exit 1
# 注意：不要用管道，QTest 的退出码本身就是"失败断言数"，管道会被 tee 吃掉。
./QSmartAssistant "${CASE}" >"${LOG}" 2>&1
rc=$?

cat "${LOG}"

if [ "${rc}" -ne 0 ]; then
    # 1) 断言类失败：挑出真正有信息量的行
    detail="$(grep -aE 'FAIL!|QFATAL|QCOMPARE|QVERIFY|Actual|Expected|Received|Aborted|Segmentation fault|error while loading|cannot open shared' "${LOG}" | head -20 || true)"
    # 2) 崩溃/早退时上面可能一条都匹配不到，而**日志尾部**（含 qInfo/qDebug 的进度
    #    标记，例如 "[mcp: mock] ready, protocol 2024-11-05"）才是唯一线索 —— 一并
    #    带上，否则一个 SIGSEGV 只能看到"崩了"，看不出崩在哪一步。
    tail_part="$(tail -40 "${LOG}" || true)"
    if [ -n "${detail}" ]; then
        body="${detail}
--- log tail (last 40 lines) ---
${tail_part}"
    else
        body="${tail_part}"
    fi
    # GitHub workflow command 的转义规则：% -> %25，换行 -> %0A，回车 -> %0D
    body="$(printf '%s' "${body}" | sed -e 's/%/%25/g' -e 's/$/%0A/')"
    echo "::error title=单测 ${CASE} 失败 (exit ${rc})::${body}"
fi

exit "${rc}"
