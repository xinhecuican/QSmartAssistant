# QSmartAssistant

A modular, fully offline, low-occupancy, high-efficiency dialogue robot/smart speaker

# Features

- [x] Developed using C++/Qt, most deep learning models use onnxruntime to improve running speed
- [x] Fully modularized preprocessing, wake-up, voice endpoint detection, ASR, TTS, NLU, making it very simple to develop components. Has a plugin mechanism that allows you to customize your own plugins
- [x] Supports livekit, porcupine, openwakeword, sherpa, duilite, snowboy wake-up models, and can freely customize wake-up words. Supports cobra, fvad, silero, duilite, sherpa models. Combined with webrtc and speex, it can accurately wake up and reduce false wake-up rate.
- [x] Supports streaming ASR and TTS
- [x] Supports onnx / rasa / llm NLU backends, allowing you to collect dialogue corpus to train NLU
- [x] Supports custom plugins, can choose which plugins to load and the loading order
- [x] Supports MQTT
- [ ] Supports multi-machine joint control

# Default installation

The default combo is unified across platforms since 2026-10:

```
livekit-wakeword  wake-up  +  silero VAD  +  sherpa ASR  +  sherpa TTS  +  ONNX NLU
```

Everything is cross-platform and pure `onnxruntime` — **no Python / TensorFlow /
PyTorch at runtime**. The NLU backend is a self-trained ONNX model
(`rbtl3` BertModel + intent head + per-token slot head) that replaces rasa's DIET;
`rasa` is still compiled and can be re-enabled by setting `nlu.backend` to `"rasa"`.
Measured on the same 65-sample holdout set: intent 98.46% / slot-exact 93.85% at
P50 ≈ 1 ms and ~127 MB RSS, vs. rasa's 100% / 92.31% at ≈42 ms and ~1982 MB.

# Processing Flow

![QSmartAssistant Process](https://image.xinhecuican.tech/img/lowpower_robot%E6%B5%81%E7%A8%8B.png)

The voice signal input from the microphone is preprocessed to remove noise, reverberation, and echo, then sent to the wake-up engine. If successfully awakened, Vad is activated to collect the speaker's commands. The voice signal is then submitted to ASR to convert it to text. NLU obtains the intent from the text, then hands it over to the plugin system for processing, and finally uses TTS to speak out the processing result.

# Hardware Environment

Test machines:

- ARM Raspberry Pi 4B 64-bit
- x86 Ubuntu 22.04

Memory requirements: When fully offline, the occupancy is approximately between 1.2G and 1.4G, at least 2G of memory is recommended, and 2G of virtual memory is required.

# Quick Start

Supported on x86_64 Ubuntu 22.04 and Raspberry Pi 4B/5 (64-bit). Full details:
[Linux build & deployment guide](doc/linux.md). The same command works on Windows
(the script picks `.ps1` automatically).

Getting **livekit wakeup + silero VAD + sherpa ASR + sherpa TTS + ONNX NLU** up and
running takes four steps:

```bash
git clone https://github.com/xinhecuican/QSmartAssistant
cd QSmartAssistant

# 1. install the minimal end-to-end chain (wakeup + VAD + ASR + TTS + NLU)
python3 scripts/setup.py --profile minimal

# 2. build (auto-detects Qt6 and the architecture)
bash scripts/build_linux.sh

# 3. dependency self-check (plugins are dlopen'ed, ldd on the exe cannot see them)
bash scripts/check_runtime_linux.sh

# 4. run (default config is RelWithDebInfo; step 2 prints the exact path)
./build/RelWithDebInfo/bin/QSmartAssistant
```

`--profile minimal` is exactly that chain. Where each piece comes from:

| Stage | Model / files | Source |
| ----- | ------------- | ------ |
| wakeup | `Data/livekit/{size_med_hnx3_ca_mw50,melspectrogram,embedding_model_int8_pc}.onnx` | **committed to the repo** |
| VAD | `Data/silero_vad.onnx` | **committed to the repo** |
| ASR | `paraformer-zh-q8` (FunASR official ONNX export / Paraformer-q8, **default backend**) | downloaded by `setup.py` (~230 MB) |
| NLU | `Data/onnx_nlu/{vocab.txt,rules.json}` + `Data/nlu_schema.json` | **committed to the repo** |
| NLU | `Data/onnx_nlu/nlu_int8.onnx` (~57 MB int8 weights) | **GitHub Release**, fetched by `setup.py` |
| TTS | `vits-zh-aishell3` | downloaded by `setup.py` (~140 MB) |

> The **wakeword classification head** (`Data/livekit/size_med_hnx3_ca_mw50.onnx`,
> medium + conv_attention + 3x near-homophone hard negatives) is a
> self-trained model that is not part of the openWakeWord release, so it is
> **committed to this repo** together with mel/emb — no `--livekit-src` /
> `--livekit-head` needed after a clone. A 153 KB low-cost alternative,
> `size_small_hnx2_ca_mw50.onnx`, also ships with the repo (switch the
> `livekit_wakeup.model` line, and set the threshold to 0.46). Use those flags
> only to install a different self-trained wakeword.
>
> The **ONNX NLU int8 weights** (`nlu_int8.onnx`, ~57 MB) are too large for git, so
> they are published on
> [Releases](https://github.com/xinhecuican/QSmartAssistant/releases/tag/onnx_nlu)
> (tag `onnx_nlu`) and fetched by `setup.py` from
> `.../releases/download/onnx_nlu/nlu_int8.onnx` (override with `--nlu-url`).
> To prepare them by hand, drop the file at `Data/onnx_nlu/nlu_int8.onnx`.
>
> The config file has been **YAML** since 2026-10 (`Data/config.yaml`; the template
> `Data/default_config.yaml` carries per-option inline comments). Legacy JSON is
> still supported: if only `config.json` is present, the program writes out a
> `config.yaml` on first start and uses it (the old JSON is kept, never deleted,
> so you can always diff or roll back).

Enable at boot:

```bash
sudo cp scripts/qsmartassistant.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now qsmartassistant
```

# Installation and Configuration

See [Installation Guide](doc/install.md) and [Configuration Guide](doc/config.md)

[Plugin configuration details](doc/plugin.md)
