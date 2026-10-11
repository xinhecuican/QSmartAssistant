# 在 Linux 上编译部署

本文是**部署目标平台**的主文档。Windows 侧见 [windows.md](windows.md)。

支持两条路径，依赖矩阵不同：

| 目标 | 架构 | 说明 |
| --- | --- | --- |
| x86_64 Ubuntu 22.04 | `x86_64` | 主目标，构建快（onnxruntime/sherpa-onnx 均有官方预编译包） |
| 树莓派 4b/5（64 位） | `aarch64` | 官方也有 aarch64 包，但 sherpa-onnx 源码构建需 30–60 分钟 |

---

## 一、前置依赖

### 1.1 系统包

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake ninja-build pkg-config git curl wget \
    python3 python3-venv python3-pip \
    libgl1-mesa-dev libegl1-mesa-dev \
    libasound2-dev libpulse-dev        # Qt Multimedia
    libxcb1-dev libxkbcommon-dev       # Qt xcb 平台插件
    libssl-dev                         # 网易云官方 API 的 RSA 签名（非 CNG 平台必需）
    binutils file                      # ldd/nm，check_runtime 与 dump_exports 需要
```

> `libssl-dev` 只影响网易云音乐的 `apiMode: "official"`。不装也能编译，
> CMake 会定义 `NETEASE_OFFICIAL_NO_CRYPTO` 降级为"签名不可用"，
> `node` 模式不受影响。

### 1.2 Qt 6

Linux **没有** Windows 那条 `Qt >= 6.8.4` 的硬限制（那是 MSVC 编译器的要求）。
两种装法：

```bash
# A) 发行版包（简单，版本可能偏旧）
sudo apt install qt6-base-dev qt6-multimedia-dev libqt6network6

# B) 官方/aqtinstall 版（版本可控）
export QT_ROOT=/opt/Qt/6.8.4/gcc_64      # 树莓派用 gcc_arm64
```

`build_linux.sh` 会依次探测 `/opt/Qt/*/gcc_64`、`~/Qt/*/gcc_64`、
`$QT_ROOT`、以及发行版的 `/usr/lib/<triplet>/cmake/Qt6`。

### 1.3 架构相关

`onnxruntime` 与 `sherpa-onnx` 官方只提供 **x86_64** 与 **aarch64** 预编译包。
32 位 ARM（树莓派 32 位系统）不在支持范围内。

脚本按 `uname -m` 自动选择，不需要手动指定。

---

## 二、一键构建

```bash
bash scripts/build_linux.sh                       # 自动探测 Qt6
bash scripts/build_linux.sh --qt-dir /opt/Qt/6.8.4/gcc_64
bash scripts/build_linux.sh --with-rasa          # 连 rasa venv 一起装
bash scripts/build_linux.sh --preset linux-arm64  # 树莓派
```

或用 CMake preset：

```bash
export QT_ROOT=/opt/Qt/6.8.4/gcc_64
cmake --preset linux-gcc && cmake --build --preset linux-gcc -j8
```

`build_linux.sh` 做的事（对应 `build_windows.ps1`）：

1. 探测架构与 Qt6；
2. 缺 `lib/onnxruntime` 或 `lib/sherpa_onnx` 时调用对应安装脚本；
3. 生成 `Data/config.yaml`（**不覆盖已有**，模板为 `Data/default_config.yaml`）与 `Data/plugin_order.txt`；
   > 配置自 2026-10 起为 YAML，模板内嵌逐项注释；若目录里只有老 `config.json`，
   > 程序启动时会**自动生成 `config.yaml`** 并使用它（老 json 保留不删）。
4. `cmake` configure + build；
5. 找可执行文件，有 `linuxdeployqt` 就打 AppImage（见 §5）。

### 默认模块组合

与 Windows 版一致，选**跨平台、纯 onnxruntime**方案：

| 模块 | 选项 | 理由 |
| --- | --- | --- |
| 唤醒 | `WAKEUP_LIVEKIT=ON` | livekit-wakeword 自训练唤醒词，纯 onnxruntime，跨平台 |
| VAD | `VAD_SILERO=ON` | 纯 onnxruntime，跨平台 |
| ASR / TTS | `ASR_SHERPA=ON` / `TTS_SHERPA=ON` | sherpa-onnx，跨平台 |
| NLU | `NLU_ONNX=ON` | 进程内 onnxruntime 跑自训练双头模型，无 Python/HTTP |

CMake 默认值本身就是这套，且**全平台统一**（`Wakeup/CMakeLists.txt`）。
`NLU_RASA` 默认关闭，已退居**兜底后端**；需要时用 `-DNLU_RASA=ON` 并把
`config.yaml` 的 `nlu.backend` 改成 `"rasa"`。
**`duilite`（厂商私有 `.so`）默认关闭** —— 需要时显式加 `-DWAKEUP_DUILITE=ON`
并自备该库。

---

## 三、NLU：默认 ONNX（进程内），rasa 为兜底

**默认** `nlu.backend = "onnx"`：进程内 onnxruntime 跑自训练的 rbtl3 双头
模型（意图头 + 逐 token 槽位头），完整复刻了 rasa 的 DIET +
EntitySynonymMapper + 规则归一化，但**没有 Python / TensorFlow / PyTorch /
HTTP 服务 / 端口**。实测对比 rasa：

| 指标 | ONNX NLU | rasa |
| --- | --- | --- |
| 常驻内存 RSS | ~127 MB | ~1982 MB |
| 延迟 P50 | ~1 ms | ~42 ms |
| holdout 意图准确率 | 98.46% | 100% |
| holdout exact 匹配 | 93.85% | 92.31% |

`Data/onnx_nlu/{vocab.txt, rules.json}` 与 `Data/nlu_schema.json` 很小，
**随仓库提供**；int8 权重 `nlu_int8.onnx`（约 57MB）不适合进 git，挂在 GitHub
Release（tag `onnx_nlu`）上：
<https://github.com/xinhecuican/QSmartAssistant/releases/download/onnx_nlu/nlu_int8.onnx>。
`scripts/setup.py --profile minimal` 会自动下载并把运行期那份补齐（仓库 / 运行期里
已有那份则直接复用，也可用 `--nlu-url` 换源）。**无需任何额外安装**。

### 切回 rasa 兜底后端（可选）

只有需要人工扩充语料、重训 DIET 时才用得上：

```bash
bash scripts/install_rasa.sh
# 并把 config.yaml 的 "nlu": {"backend": "rasa"}
```

### ⚠️ 为什么这个脚本必须钉死版本

`rasa 3.6.19` 只支持 **Python 3.8–3.10**，且：

| 包 | 必须钉 | 原因 |
| --- | --- | --- |
| `transformers` | `==4.36.2` | rasa 有 `from transformers import TFPreTrainedModel`，而 **5.x 删除了全部 TF 支持** → ImportError，服务起不来、NLU 全哑。报错发生在 import 期，很容易被误判成"模型坏了"。 |
| `tokenizers` | `<0.19` | transformers 4.36 的要求 |
| `huggingface-hub` | `<0.21` | 新版移除了 rasa 用到的部分 API |
| `emoji` | `==1.7` | rasa 依赖 |

已实测可用的组合：`transformers 4.36.2 / tokenizers 0.15.2 / huggingface-hub 0.20.3`。

> `setuptools < 70.4` 这个旧约束**现在已不必要** —— 实测 setuptools 78.1.0 下
> rasa 3.6.19 import 正常，且过旧的 setuptools 无法构建部分新 wheel。

### venv 位置

默认装到 `lib/rasa/`（Windows 侧是 `lib/rasa\Scripts\`）。
应用通过 `Conversation/NLU/rasanlu.cpp` 的 `resolveRasaVenv()` 自动定位，
**默认配置下不需要改任何配置文件**。

探测顺序：

1. `config.yaml` 的 `rasa.python_venv`（留空即自动探测）
2. 环境变量 `QSA_RASA_VENV`
3. `~/.config/QSmartAssistant/rasa-venv`
4. `lib/rasa/bin/activate`（源码树，build 目录往上两级）
5. `lib/rasa`

探测函数**同时认 Windows（`Scripts/`）与 POSIX（`bin/`）两种布局**，
所以同一份配置值在两个平台上都能用。

### 服务是怎么起来的

| 平台 | 机制 |
| --- | --- |
| Linux | `Data/start-rasa.sh`（`source` venv 的 activate 后 `nohup rasa run --enable-api`） |
| Windows | 进程内 `QProcess` 直接拉 `rasa.exe` |

Linux 脚本由 `rasanlu.cpp` 的 `runRasash()` 调用，它会注入
`QSA_DATA_DIR=<Data 目录>`，脚本据此定位 `rasa.pid` / `rasa.log`，
**不依赖 HOME 推导**。

日志：`~/.config/QSmartAssistant/Data/rasa/rasa.log`
PID： 同目录 `rasa.pid`

### 手工验证

```bash
source lib/rasa/bin/activate
cd ~/.config/QSmartAssistant/Data/rasa
rasa run --enable-api -m models/default.tar.gz
# 首次加载约 30-60s
curl -s http://127.0.0.1:5005/status
curl -s http://127.0.0.1:5005/model/parse \
     -H 'Content-Type: application/json' \
     -d '{"text":"30分钟后开启热水器"}'
```

应得到 `intent.name = OPEN_FURNITURE`、实体含 `time` 与 `furniture`。

---

## 四、模型文件

```bash
python3 scripts/fetch_models.py
```

`install_sherpa_onnx.sh` 顺带下载 ASR（paraformer-zh）与 TTS（matcha-icefall-zh-baker）。
`install_rasa.sh` 下载 `Data/rasa/data/chinese_rbtl3/`（config.yml 引用的中文 BERT）。

NLU 模型本身在仓库里：`Data/rasa/models/default.tar.gz`。

---

## 五、Qt 运行时部署（Linux 与 Windows 的关键差异）

**Windows 用 `windeployqt` 把 Qt 一起打包；Linux 通常不这么做**，而是依赖系统 Qt。
两种选择：

```bash
# A) 打 AppImage（自带 Qt，换机器不用装依赖）
sudo mv linuxdeployqt.AppImage /usr/local/bin/
bash scripts/build_linux.sh          # 自动调用

# B) 依赖系统 Qt（体积小、便于 systemd 部署）
bash scripts/build_linux.sh --no-plugin-deploy
```

选 B 时**务必**做一次依赖自检 —— 缺库时程序会在启动瞬间才报
`error while loading shared libraries`：

```bash
bash scripts/check_runtime_linux.sh [路径/QSmartAssistant]
```

这个脚本会检查主程序**以及所有插件**（插件是运行时 `dlopen` 的，
`ldd` 主程序看不到它们）。

---

## 六、开机自启

```bash
sudo cp scripts/qsmartassistant.service /etc/systemd/system/
sudo vi /etc/systemd/system/qsmartassistant.service   # 改 User / WorkingDirectory / ExecStart
sudo systemctl daemon-reload
sudo systemctl enable --now qsmartassistant
journalctl -u qsmartassistant -f
```

unit 文件里有几处**必须按实际情况改**：`User`、`WorkingDirectory`、`ExecStart`、
`Environment=QSA_DATA_DIR`。

> ⚠️ **不要用 root 运行**。运行期数据固定写在 `~/.config/QSmartAssistant/`
> （CMake 里 `CONFIG_PATH = $HOME/.config/...`），root 跑会把配置写到 `/root/.config`，
> 之后切回普通用户就"配置丢了"。

---

## 七、可选：本地 GGUF 大模型（Chat / LLM-NLU）

当前配置里 `llm.default` 指向 ollama（`http://127.0.0.1:11434`）。
若要改成进程内 llama.cpp：

```bash
bash scripts/install_llama.sh --backend cpu       # 或 vulkan（AMD/NVIDIA/Intel 通用）
python3 scripts/fetch_llm_model.py --quant Q4_K_M
```

然后在 `config.yaml` 的 `llm.backends` 里启用 `local-qwen`，
并把 `llm_nlu.llm` 指向它。
### ⚠️ llama.cpp 的两个已知问题

1. **包名与 Windows 不同**：Linux 是
   `llama-<tag>-bin-ubuntu-<backend>-<arch>.tar.gz`，而 **CPU 包没有 `-cpu` 段**
   （是 `llama-<tag>-bin-ubuntu-x64.tar.gz`）。照抄 Windows 的 `-cpu-` 会 404。
   脚本已处理，并会显式挡掉"下载到几字节错误页"的情况。
2. **长 prompt decode 失败**：约 300+ token 的 prompt 稳定报
   `failed to find a memory slot for batch`，decode 返回 -1（CPU/Vulkan 都复现，
   调 `n_ctx` 无效）。原先归因于上游 issue #20049，但该 issue 已于 2026-05-24
   被以 stale/bug-unconfirmed 关闭，**从未确认修复**。
   → 长 prompt 场景目前建议走 ollama 端点规避。

排障时可用 Linux 版符号导出工具确认 `.so` 导出了哪些符号：

```bash
bash scripts/dump_exports_linux.sh lib/llama/bin/libllama.so
```

---

## 八、微调（**部署机通常不需要**）

只有微调 Qwen3.5-2B 做 NLU 才需要 GPU。默认的 `nlu.backend = "onnx"` 与
兜底的 `"rasa"` 都用不上。

```bash
bash scripts/setup_rocm_linux.sh --check      # 先体检
bash scripts/setup_rocm_linux.sh --install    # AMD GPU
bash scripts/setup_rocm_linux.sh --gfx gfx1100   # 换卡要改 gfx target
```

详见 §7 里提到的两个坑（rocm 与 torch 必须同时装；`PYTORCH_HIP_ALLOC_CONF`
必须在 `import torch` 之前设置），以及 `doc/nlu_llm_results.md`。

---

## 九、脚本对照表

| 功能 | Linux | Windows |
| --- | --- | --- |
| 一键构建 | `build_linux.sh` | `build_windows.ps1` |
| onnxruntime | `install_onnxruntime.sh` | `install_onnxruntime.ps1` |
| sherpa-onnx | `install_sherpa_onnx.sh` | `install_sherpa_onnx.ps1` |
| ONNX NLU（默认，随仓库提供） | `setup.py --only onnx-nlu` | 同左 |
| rasa（兜底后端，可选） | `install_rasa.sh` | `install_rasa.ps1` |
| llama.cpp | `install_llama.sh` | `install_llama.ps1` |
| 网易云 node 服务 | `install_neteasejs.sh` | `install_netease_js.ps1` |
| ROCm（仅微调） | `setup_rocm_linux.sh` | `setup_rocm_windows.py` |
| 依赖自检 | `check_runtime_linux.sh` | `windeployqt` / `deploy_qt.py` |
| 符号导出 | `dump_exports_linux.sh` | `dump_pe_exports.py` |
| 开机自启 | `qsmartassistant.service` | 任务计划程序 / 服务 |
| 模型下载 | `fetch_models.py` / `fetch_llm_model.py` | 同左（跨平台） |

---

## 十、已知限制

- **duilite / porcupine / snowboy / koala / cobra** 是厂商私有库或不支持 Linux ARM，
  本文的默认组合不含它们。
- **32 位 ARM** 不支持（onnxruntime / sherpa-onnx 官方只出 64 位包）。
- **默认组合不依赖 Python**：唤醒（livekit）、VAD（silero）、NLU（onnx）三个
  关键模块都在进程内跑 onnxruntime，python3.10 只对**可选的** rasa 兜底后端才必要。
- **rasa 3.6.19 最高只支持 Python 3.10**。Ubuntu 24.04 默认 python3.12，
  需要额外装 `python3.10`；树莓派 OS（Bookworm，python3.11）同理。
  （默认的 onnx 后端不需要 Python。）
- **仅在 `NLU_RASA=ON` 且 `nlu.backend = "rasa"` 时**应用才会自己拉起 rasa。
  首次启动时 rasa 加载模型要 30–60s，这段时间内 `/model/parse` 会失败，日志里有
  `rasa request error` —— 属正常，第二次询问即恢复。用 systemd 时建议让 rasa
  先起（见 §6 的说明）。默认的 onnx 后端没有这些问题。