# QSmartAssistant

一个模块化，全过程可离线，低占用率，高效率的对话机器人/智能音箱

# 特点

- [x] 使用c++qt开发，深度学习模型大多选择onnxruntime，提高运行速度
- [x] 前处理，唤醒，语音端点检测，ASR，TTS，NLU全模块化，可以非常简单的开发组件。拥有插件机制，可以定制自己的插件
- [x] 默认安装：livekit-wakeword 唤醒 + silero VAD + sherpa ASR/TTS + ONNX NLU，全跨平台、纯 onnxruntime
- [x] 支持livekit,porcupine,openwakeword,sherpa,duilite,snowboy唤醒模型，可以自由定制唤醒词。支持cobra,fvad,silero,duilite,sherpa模型。搭配webrtc和speex，可以精确唤醒并降低误唤醒率。
- [x] 支持流式ASR和TTS
- [x] 支持onnx/rasa/llm三种NLU后端，可以自己收集对话语料训练NLU
- [x] 支持自定义插件，可以选择加载哪些插件及加载顺序
- [x] 支持mqtt
- [ ] 支持多机器联合控制

# 处理流程

![QSmartAssistant流程](https://image.xinhecuican.tech/img/lowpower_robot%E6%B5%81%E7%A8%8B.png)

麦克风输入的语音信号经过前处理处理掉噪音，混响和回声后，送入唤醒引擎。如果成功唤醒则激活Vad来收集说话人的命令。再将语音信号提交给ASR转换成文字。NLU根据文本获得其中的意图，然后交由插件系统处理，最后使用TTS将处理结果说出来。

# 硬件环境

测试机器：

- arm 树莓派4b 64位
- x86 ubuntu22.04

内存需求：全过程离线的情况下占用大约在1.2G到1.4G之间，推荐至少2G内存，并且需要开2G的虚拟内存

# 快速开始（Linux，主目标平台）

支持 x86_64 Ubuntu 22.04 与树莓派 4b/5（64 位）。完整说明见
[Linux 构建部署指南](doc/linux.md)。

跑通 **livekit 唤醒 + silero VAD + sherpa ASR + sherpa TTS + ONNX NLU** 这条
最小全流程只需要四步：

```bash
git clone https://github.com/xinhecuican/QSmartAssistant
cd QSmartAssistant

# 1. 装齐最小全流程（唤醒 + VAD + ASR + TTS + NLU 的库与模型）
python3 scripts/setup.py --profile minimal

# 2. 编译（自动探测 Qt6 与架构）
bash scripts/build_linux.sh

# 3. 依赖自检（插件是 dlopen 的，ldd 主程序看不到）
bash scripts/check_runtime_linux.sh

# 4. 运行（默认配置 RelWithDebInfo，路径由第 2 步脚本打印）
./build/RelWithDebInfo/bin/QSmartAssistant
```

`--profile minimal` 装的就是这条链路，各环节的模型来源如下 —— 唤醒 / VAD 的模型与
ONNX NLU 的词表、规则表**已随仓库提交**，clone 下来即可用；ASR / TTS 模型与
ONNX NLU 的权重由 `setup.py` 自动获取：

| 环节 | 模型 / 文件 | 来源 |
| ---- | ---------- | ---- |
| 唤醒 | `Data/livekit/{size_small,melspectrogram,embedding_model}.onnx` | **随仓库提交** |
| VAD  | `Data/silero_vad.onnx` | **随仓库提交** |
| ASR  | `sherpa-onnx-paraformer-zh-small` | `setup.py` 下载（~74 MB） |
| NLU  | `Data/onnx_nlu/{vocab.txt,rules.json}` + `Data/nlu_schema.json` | **随仓库提交** |
| NLU  | `Data/onnx_nlu/nlu_int8.onnx`（~57 MB，int8 权重） | **GitHub Release**，`setup.py` 下载 |
| TTS  | `vits-zh-aishell3` | `setup.py` 下载（~140 MB） |

> **livekit 唤醒词**的分类头（`Data/livekit/size_small.onnx`）是本项目**自训练**的
> 产物，openWakeWord 官方 release 里没有，所以**连同 mel / emb 一起直接提交进了
> 仓库**，clone 后不用再传 `--livekit-src` / `--livekit-head`。只有想换成别的自
> 训练唤醒词时，才需要指定来源：
>
> ```bash
> python3 scripts/setup.py --profile minimal --livekit-src /path/to/livekit-wakeword
> # 或直接给文件
> python3 scripts/setup.py --profile minimal --livekit-head /path/to/size_small.onnx
> ```

> **ONNX NLU 的 int8 权重**（`nlu_int8.onnx`，约 57 MB）体积偏大，不随仓库提交，
> 而是放在 [Releases](https://github.com/xinhecuican/QSmartAssistant/releases/tag/onnx_nlu)
> 上（tag `onnx_nlu`）；`setup.py` 会自动从
> `https://github.com/xinhecuican/QSmartAssistant/releases/download/onnx_nlu/nlu_int8.onnx`
> 拉取（可用 `--nlu-url` 换成镜像或本地文件）。想手动准备时，把文件放到
> `Data/onnx_nlu/nlu_int8.onnx` 即可，安装器会优先复用仓库 / 运行期里已有的那份。

安装完成后开机自启：

```bash
sudo cp scripts/qsmartassistant.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now qsmartassistant
```

Windows 用同一条命令（脚本会自动选 `.ps1`）：

```powershell
python scripts\setup.py --profile minimal
.\scripts\build_windows.ps1          # 需要 Qt ≥ 6.8.4 (msvc2022_64)
```

# 安装器说明

`scripts/setup.py` 是**统一的跨平台安装入口**，把原先分散的
`install_*.sh` / `install_*.ps1` / `fetch_models.py` 收敛成一处，
带依赖图、断点续装与安装后自检。

| profile       | 组件                                                  | 体积      | 说明                            |
| ------------- | --------------------------------------------------- | ------- | ----------------------------- |
| `minimal`（默认） | livekit-wakeup → silero VAD → sherpa ASR → sherpa TTS → onnx NLU | ~290 MB | **最小全流程**，语音助手完整链路            |
| `standard`    | minimal + llama.cpp                                 | ~1.4 GB | 加上本地 GGUF 大模型（chat / llm_nlu） |
| `full`        | standard + 网易云 node 服务                              | ~1.5 GB | 全部组件                          |

> **默认 NLU 是进程内 ONNX 模型**（随仓库提供，无 Python / TensorFlow / PyTorch /
> HTTP 服务，常驻约 127 MB），不再是 rasa。`rasa-nlu` 已退居**兜底后端**，
> 不在任何默认 profile 里；只有把 `config.yaml` 改成
> `nlu: {backend: "rasa"}` 时才需要 `python3 scripts/setup.py --only rasa-nlu`
> （体积 ~1.2 GB，首次启动加载模型 30–60 s）。

常用参数：

```bash
python3 scripts/setup.py --dry-run          # 只打印计划，不实际执行
python3 scripts/setup.py --verify-only      # 只做安装后自检
python3 scripts/setup.py --only onnx-nlu    # 只装某几项（依赖会自动带上）
python3 scripts/setup.py --skip llama       # 跳过某些组件
python3 scripts/setup.py --force            # 已安装也重装
python3 scripts/setup.py --hf-base https://hf-mirror.com   # 走镜像
```

组件实现方式是**复用** `scripts/install_*.{sh,ps1}`，不在 Python 里重写 ——
那些脚本里积累了大量踩坑处理（transformers 版本钉、llama.cpp 包名规则、
venv 布局差异等），重新实现等于把坑再踩一遍。

# 手动安装（可选）

不使用统一入口时，各步骤如下：

```bash
cp Data/default_config.yaml Data/config.yaml
cp Data/plugin_order_default.txt Data/plugin_order.txt

cd scripts
./install_openssl.sh          # 网易云官方 API 的 RSA 签名（可选）
./install_sherpa_onnx.sh
./install_onnxruntime.sh
# NLU 默认用进程内 ONNX（产物随仓库 Data/onnx_nlu/），无需装 rasa。
# 只有要切回 rasa 兜底后端时才跑：./install_rasa.sh
cd ..
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWAKEUP_LIVEKIT=ON -DWAKEUP_DUILITE=OFF \
      -DVAD_SILERO=ON -DVAD_DUILITE=OFF -DNLU_ONNX=ON -DNETEASE_USE_JS=ON -B build/
cmake --build build/ -j$(nproc)
# config.yaml 中的配置需要按需进行更改
```

> 上面这套 `-D` 其实就是 CMake 的默认值（默认安装 = livekit 唤醒 + silero VAD
> + sherpa ASR/TTS + onnx NLU），显式写出来只是为了让命令自解释。
> 想换回 rasa 时加 `-DNLU_RASA=ON` 并把 `config.yaml` 的 `nlu.backend` 改成 `"rasa"`。
>
> 配置文件自 2026-10 起为 **YAML**（`Data/config.yaml`，模板 `Data/default_config.yaml`
> 内嵌逐项注释），并**兼容老 json**：若目录里只有 `config.json`，程序启动时会
> 自动生成 `config.yaml` 并使用它（老 json 保留不删，便于回滚对比）。

`install_rasa.sh` **必须钉死 `transformers==4.36.2` / `tokenizers<0.19` /
`huggingface-hub<0.21`**：rasa 3.6.19 有 `from transformers import TFPreTrainedModel`，
而 transformers 5.x 删除了 TF 支持，直接 ImportError、NLU 全哑。
详见 [linux.md §3](doc/linux.md)。

# 安装和配置

见[安装教程](doc/安装.md)和[配置教程](doc/配置.md)

[插件配置详见](doc/插件.md)

# Windows 专属

Windows（MSVC + 官方 Qt6，默认使用跨平台模块组合）的独有部分只有构建：

```powershell
.\scripts\build_windows.ps1 -QtDir "C:\Qt\6.8.4\msvc2022_64"
python scripts\fetch_models.py     # 或用上面的 setup.py 统一入口
```

要求 **Qt ≥ 6.8.4**（`msvc2022_64`）。详见 [Windows 构建指南](doc/windows.md)。

两个平台的脚本对照见 [linux.md §9](doc/linux.md)。
