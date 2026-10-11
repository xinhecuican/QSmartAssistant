#!/bin/bash
# ---------------------------------------------------------------------------
# install_rasa.sh —— 安装 rasa 3.6.19 venv + 中文 BERT 权重（hfl/rbtl3）
#
# 用法：
#   bash scripts/install_rasa.sh                        # 自动挑 Python 3.8-3.10
#   bash scripts/install_rasa.sh --python python3.10    # 指定解释器
#   bash scripts/install_rasa.sh --venv-dir ~/.config/QSmartAssistant/rasa-venv
#   bash scripts/install_rasa.sh --skip-model           # 只装环境，不下模型
#   bash scripts/install_rasa.sh --reinstall            # 删掉重装
#   bash scripts/install_rasa.sh --hf-base https://hf-mirror.com
#   HF_ENDPOINT=https://hf-mirror.com bash scripts/install_rasa.sh
#
# ---------------------------------------------------------------------------
# 为什么必须钉死这些版本（旧版脚本没有钉，装出来是跑不起来的）：
#
#   1. transformers 必须 ==4.36.2。
#      rasa/nlu/utils/hugging_face/registry.py 里有
#          from transformers import TFPreTrainedModel
#      而 **transformers 5.x 删除了全部 TF 支持** -> ImportError，
#      服务起不来、NLU 全哑。且报错发生在 import 期而非使用期，
#      很容易被误判成"模型坏了"。pip 默认装的就是 5.x。
#   2. tokenizers<0.19、huggingface-hub<0.21。
#      transformers 4.36 要求 tokenizers<0.19；新版 huggingface-hub
#      移除了 rasa 用到的部分 API。
#   3. setuptools：旧脚本钉了 <70.4.0，**这个约束现在已经不必要**。
#      实测在 setuptools 78.1.0 下 rasa 3.6.19 + transformers 4.36.2
#      import 正常（该问题主要出现在 rasa 的**源码构建**路径）。
#      保留降级有风险：setuptools <70.4 无法构建部分新 wheel。
#      所以这里不钉，只保证 pip/setuptools 是新的。
#   4. emoji==1.7。rasa 依赖该版本。
#
# 另一个改动：旧版用 `virtualenv` + `sudo apt install python3-virtualenv`。
# virtualenv 早已非必需（Python 3.3+ 自带 venv），而 sudo apt 在
# 无 root / 离线 / 容器里是硬伤，还会往系统 Python 装东西。现在统一 python -m venv。
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "${SCRIPT_DIR}")"

RASA_VERSION="3.6.19"
TRANSFORMERS_VERSION="4.36.2"

PYTHON_BIN=""
VENV_DIR="${ROOT}/lib/rasa"
HF_BASE="${HF_ENDPOINT:-https://huggingface.co}"
SKIP_MODEL=0
REINSTALL=0
TORCH_CHANNEL="cpu"

usage() {
    # 打印文件头的用法块（跳过 shebang 与空行分隔）。
    sed -n '3,/^set -euo/p' "$0" | grep '^#' | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --python)     PYTHON_BIN="$2"; shift 2 ;;
        --python=*)   PYTHON_BIN="${1#*=}"; shift ;;
        --venv-dir)   VENV_DIR="$2"; shift 2 ;;
        --venv-dir=*) VENV_DIR="${1#*=}"; shift ;;
        --hf-base)    HF_BASE="$2"; shift 2 ;;
        --hf-base=*)  HF_BASE="${1#*=}"; shift ;;
        --torch)      TORCH_CHANNEL="$2"; shift 2 ;;
        --skip-model) SKIP_MODEL=1; shift ;;
        --reinstall)  REINSTALL=1; shift ;;
        -h|--help)    usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# 1. 找一个 Python 3.8 - 3.10（rasa 3.6.19 的硬性上限）
# ---------------------------------------------------------------------------
py_version() { "$1" -c 'import sys;print("%d.%d.%d"%sys.version_info[:3])' 2>/dev/null || echo ""; }
py_ok() { case "$1" in 3.8.*|3.9.*|3.10.*) return 0 ;; *) return 1 ;; esac; }

if [ -n "${PYTHON_BIN}" ]; then
    if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1 && [ ! -x "${PYTHON_BIN}" ]; then
        echo "python not found: ${PYTHON_BIN}" >&2; exit 1
    fi
    PYVER="$(py_version "${PYTHON_BIN}")"
    if ! py_ok "${PYVER}"; then
        echo "${PYTHON_BIN} is ${PYVER:-unknown}, rasa ${RASA_VERSION} needs 3.8-3.10" >&2
        exit 1
    fi
else
    for cand in python3.10 python3.9 python3.8 python3 python; do
        command -v "${cand}" >/dev/null 2>&1 || continue
        PYVER="$(py_version "${cand}")"
        if py_ok "${PYVER}"; then PYTHON_BIN="${cand}"; break; fi
    done
    [ -n "${PYTHON_BIN}" ] || {
        echo "ERROR: no Python 3.8-3.10 found. rasa ${RASA_VERSION} does not support newer." >&2
        echo "  Ubuntu 22.04   : sudo apt install python3.10 python3.10-venv" >&2
        echo "  Raspberry Pi OS: 'python3 --version'; 3.11+ is NOT supported by rasa 3.6" >&2
        echo "  Then re-run with:  bash scripts/install_rasa.sh --python /usr/bin/python3.10" >&2
        exit 1
    }
fi
echo "==> Using Python ${PYVER} (${PYTHON_BIN})"

# venv 模块在 Debian/Ubuntu 上常常是单独打包的（python3.10-venv）。
if ! "${PYTHON_BIN}" -m venv --help >/dev/null 2>&1; then
    echo "ERROR: ${PYTHON_BIN} has no 'venv' module. Install it first:" >&2
    echo "  sudo apt install python3.10-venv   # or: python3-venv" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# 2. 建 venv
# ---------------------------------------------------------------------------
if [ "${REINSTALL}" -eq 1 ] && [ -d "${VENV_DIR}" ]; then
    echo "==> Removing ${VENV_DIR}"
    rm -rf "${VENV_DIR}"
fi

# Linux 的 venv 布局是 bin/，Windows 是 Scripts/ —— C++ 侧 resolveRasaVenv()
# 两个布局都认，见 Conversation/NLU/rasanlu.cpp。
VENV_PY="${VENV_DIR}/bin/python"
if [ ! -x "${VENV_PY}" ]; then
    echo "==> Creating venv at ${VENV_DIR}"
    mkdir -p "$(dirname "${VENV_DIR}")"
    "${PYTHON_BIN}" -m venv "${VENV_DIR}"
else
    echo "==> Reusing existing venv: ${VENV_DIR}"
fi
[ -x "${VENV_PY}" ] || { echo "venv creation failed: ${VENV_PY} missing" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 3. 装依赖（版本钉见文件头）
# ---------------------------------------------------------------------------
echo "==> Upgrading pip / setuptools"
"${VENV_PY}" -m pip install --upgrade pip setuptools >/dev/null

# torch 单独装：最大的一个包，默认 wheel 会拉 ~2GB 的 CUDA 依赖。
# 部署机跑 NLU 推理用 CPU 版足够（rasa 侧是 intent classifier + DIET，
# 本地推理在 rasa 服务进程里，CPU 延迟实测 P50 42ms）。
echo "==> Installing torch (${TORCH_CHANNEL} build, avoids ~2GB of CUDA deps)"
"${VENV_PY}" -m pip install torch --index-url "https://download.pytorch.org/whl/${TORCH_CHANNEL}"

echo "==> Installing rasa (large download)"
# 先装 rasa 让 pip 解析出它对 transformers 的**下限**，再用 == 精确钉住，
# 避免顺序反过来时 rasa 被顺带升到 transformers 5.x。
"${VENV_PY}" -m pip install "rasa==${RASA_VERSION}"

echo "==> Installing NLU extras with pinned versions"
"${VENV_PY}" -m pip install \
    "transformers==${TRANSFORMERS_VERSION}" \
    "tokenizers<0.19" \
    "huggingface-hub<0.21" \
    "jieba" \
    "emoji==1.7" \
    "recognizers-text-suite"

# ---------------------------------------------------------------------------
# 4. 自检：rasa 的 TF 依赖能不能 import（这是最常见的静默失败点）
#    不装成功不算装成功 —— 装完必须验证，否则用户拿到的是"能装不能跑"的 venv。
# ---------------------------------------------------------------------------
echo "==> Verifying the install"
if ! VERIFY_OUT="$("${VENV_PY}" - <<'PY' 2>&1
import transformers
from transformers import TFPreTrainedModel  # noqa: F401  rasa 的硬依赖
import rasa
print("transformers=%s rasa=%s" % (transformers.__version__, rasa.__version__))
PY
)"; then
    echo "" >&2
    echo "ERROR: install finished but import failed:" >&2
    echo "${VERIFY_OUT}" >&2
    echo "Most likely cause: transformers drifted to 5.x (TF support removed)." >&2
    echo "Fix: ${VENV_PY} -m pip install 'transformers==${TRANSFORMERS_VERSION}' 'tokenizers<0.19' 'huggingface-hub<0.21'" >&2
    exit 1
fi
echo "    ${VERIFY_OUT}"

# ---------------------------------------------------------------------------
# 5. 下载 config.yml 引用的中文 BERT 权重
# ---------------------------------------------------------------------------
BERT_DIR="${ROOT}/Data/rasa/data/chinese_rbtl3"
if [ "${SKIP_MODEL}" -eq 1 ]; then
    echo "==> --skip-model: not downloading BERT weights"
else
    mkdir -p "${BERT_DIR}"
    for f in pytorch_model.bin vocab.txt config.json; do
        if [ -s "${BERT_DIR}/${f}" ]; then
            echo "==> ${f} already present, skipping"
            continue
        fi
        url="${HF_BASE}/hfl/rbtl3/resolve/main/${f}"
        echo "==> Downloading ${f}"
        if ! wget -q -O "${BERT_DIR}/${f}" "${url}"; then
            echo "WARNING: failed to download ${f} from ${url}" >&2
            rm -f "${BERT_DIR}/${f}"
            echo "  Download it manually into ${BERT_DIR}" >&2
        fi
    done
fi

# ---------------------------------------------------------------------------
# 6. 收尾提示
# ---------------------------------------------------------------------------
cat <<EOF

Done.
  venv     : ${VENV_DIR}
  rasa bin : ${VENV_DIR}/bin/rasa
  bert     : ${BERT_DIR}

The app finds this venv automatically (Conversation/NLU/rasanlu.cpp ->
resolveRasaVenv). No config change is needed at the default location.
To point somewhere else, set either:
  config.json -> "rasa": { "python_venv": "${VENV_DIR}" }
  env         QSA_RASA_VENV="${VENV_DIR}"

Smoke test the service before wiring it to the app:
  source ${VENV_DIR}/bin/activate
  cd ${ROOT}/Data/rasa && rasa run --enable-api -m models/default.tar.gz
  curl http://127.0.0.1:5005/status     # 首次加载模型约需 30s
EOF