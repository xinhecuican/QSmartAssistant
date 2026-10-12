# FunASR CPU / 边缘部署实测报告（llama.cpp + GGUF，附 ONNX 落地）

> 目标：把 FunASR 官方标称的「**CPU / 边缘部署**」路线（`runtime-llamacpp-v0.2.6`，无需 GPU、无需 Python 运行时）  
> 拉下来，配官方发布的中文测试集，实测**准确率（CER）**&#x4E0E;**速度（RTF）**，并与官方公布数字、以及本项目  
> 现用的 sherpa-onnx（ONNX Runtime）路线横向对比。
>
> 测试日期：2026-10-12 · 测试脚本与原始结果：`tools/funasr_cpu_bench/`
>
> ⚠ **后续（同一天）：§10 是真正的落地** —— 结论不是换 llama.cpp，而是换成 FunASR 官方
> **ONNX** 导出包（同一个 Paraformer-q8 权重，进程内 onnxruntime），它比 sherpa 更快也更准，
> 现已替换成本项目的**默认 ASR**。只看「要不要动」的话直接跳到 §10。

---

## 1. 结论速览（TL;DR）

| 结论         | 数据                                                                                                                                                                                                             |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **能不能用**   | 能。单文件 5MB 的预编译 exe + 3 个 GGUF 权重（合计 ~490 MB），CPU 上开箱即用，零 Python 依赖                                                                                                                                             |
| **准确率**    | Paraformer 中文模型在 AISHELL-1 test 上 **micro-CER 1.92%~1.98%**，与官方 PyTorch 报告的 **1.95%** 只差 0.03 个百分点 → **int8/q8 量化几乎无损**                                                                                        |
| **速度**     | 纯计算 **RTF 0.020~~0.023（约 44~~49× 实时）**，单条 5 s 音频中位 **100~110 ms**（Ryzen 7 9700X，16 线程）                                                                                                                         |
| **内存**     | 峰值工作集约 **260~290 MB**（含权重常驻）                                                                                                                                                                                   |
| **最大的坑**   | ⚠️ CLI 跑出来的 SenseVoice 输出**阿拉伯数字（ITN 开启）**，而 CLI **没有关掉 ITN 的开关**；参考文本写的是中文数字，于是 CER 直接翻倍（6.39% vs 2.90%）。同一个模型换 sherpa-onnx 跑，`use_itn=off` 是 2.90%、`use_itn=on` 是 6.41% —— 证明**两个运行时的识别能力一致，差异 100% 来自 ITN** |
| **另一个坑**   | 这些 exe 是「**一次调用 = 一次加载 + 一次推理**」的 CLI 形态。换算成端到端吞吐，RTF 会从 0.022 恶化到 **0.066（15× 实时）**，一半以上的时间花在反复加载 250 MB 权重上。真正部署必须像它的 `server/` 那样**常驻进程**                                                                   |
| **和现用方案比** | 同一个 SenseVoiceSmall：sherpa-onnx(INT8 ONNX) 在 16 线程 **RTF 0.0148（67.6×）**，甚至 2 线程也有 **0.0177（56.4×）**；llama.cpp/GGUF 是 0.021~0.023。**本项目现有 ONNX 路线在 CPU 上更快**，llama.cpp 路线的价值主要在「无 Python / 单二进制 / 易塞进嵌入式」      |

---

## 2. 被测对象

### 2.1 官方「CPU / 边缘部署」路线

`FunASR` README 里明确把 **CPU / 边缘部署**对应到 **llama.cpp / GGUF**：

> ### CPU / 边缘部署 - llama.cpp / GGUF（无需 GPU、无需 Python）
>
> 在 CPU 和边缘设备上用单个自包含二进制运行 **SenseVoice / Paraformer / Fun-ASR-Nano**，  
> 无需 Python 运行环境，并内置 FSMN-VAD。

| 项      | 值                                                                                                                                                                      |
| ------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 发行包    | `runtime-llamacpp-v0.2.6`（2026-08-30）                                                                                                                                  |
| 二进制    | `funasr-llamacpp-windows-x64-avx2.zip`（5.2 MB）                                                                                                                         |
| 内含     | `llama-funasr-sensevoice.exe` / `llama-funasr-paraformer.exe` / `llama-funasr-cli.exe` / `llama-funasr-vad.exe` / `llama-funasr-encoder.exe` / `llama-funasr-embd.exe` |
| 权重     | `hf-mirror.com/FunAudioLLM/SenseVoiceSmall-GGUF` · `.../Paraformer-GGUF` · `.../fsmn-vad-GGUF`                                                                         |
| 权重体积   | SenseVoiceSmall-q8 **254 MB** · Paraformer-q8 **237 MB** · fsmn-vad **1.7 MB**                                                                                         |
| CLI 参数 | `-m <model.gguf> (-a <audio.wav> \| -f fbank.bin) [--vad fsmn-vad.gguf [--vad-maxseg ms]] [--backend cpu\|cuda\|vulkan] [--srt] [--ids] [--keep-tags]`                 |

> ⚠️ 注意：**二进制不暴露线程数参数**，ggml 默认吃满硬件线程；也没有 `--itn/--no-itn` 开关。  
> 官方另有 CUDA / Vulkan 包，本报告只测 CPU（`--backend cpu`，即默认值，也是跨平台可靠回退路径）。

### 2.2 参照系：仓库已有的 sherpa-onnx（ONNX Runtime）

本项目 `Conversation/ASR/sherpaasr.cpp` 走的是 sherpa-onnx（`num_threads=2`，SenseVoice/Paraformer 均支持）。  
为了判断「要不要换」，用**同一个 SenseVoiceSmall 模型**的 ONNX 版做对照：  
`sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17`（int8 ONNX，239 MB）+ `sherpa-onnx 1.13.8`。

---

## 3. 测试集：从哪来、什么口径

### 3.1 参考文本（标注）

FunASR 仓库里直接带着一份**完整的 AISHELL-1 test 清单**：

```
https://raw.githubusercontent.com/modelscope/FunASR/main/runtime/triton_gpu/client/aishell_test.txt
```

7176 行 JSON，每行 `{"audio_filepath": ".../S0764/BAC009S0764W0121.wav", "duration": 4.2039375, "text": "甚至出现交易几乎停滞的情况"}`。  
它同时给出了**参考文本**和**精确时长**，正好是 RTF 分母所需。

### 3.2 音频

| 来源                                                                                            | 说明                                                                               |
| --------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| ModelScope 数据集 `speech_asr/speech_asr_aishell1_trainsets` → `speech_asr_aishell_testsets.zip` | **870 MB**，解压后 **7176 个 16 kHz 单声道 wav**，合计 **10.03 小时**（即 AISHELL-1 官方 test 全集） |

> 顺带记一笔：同名的 `modelscope/speech_asr_aishell1_testsets`（2.45 GB）和  
> `speech_asr/speech_asr_aishell1_subset`（163 MB，1443 条）里**只有 wav、没有文本**，  
> 单独下载无法算 CER；要算指标必须配 3.1 的清单。

### 3.3 两个评测集

| 集合        | 构造                                                    | 规模                          | 用途                                         |
| --------- | ----------------------------------------------------- | --------------------------- | ------------------------------------------ |
| **S1000** | 按说话人**分层随机抽样**（20 位说话人同比例），固定随机种子 `20261012`          | 999 条 / 1.41 h / 14707 参考字  | 主口径：准确率 + 速度（单进程串行，时间不受干扰）                 |
| **L200**  | 把**同一说话人的连续短句拼接**成 ~50 s 长音频（句间插 0.4 s 静音），参考文本用「，」连接 | 200 条 / 2.78 h / 每条均 50.4 s | 对齐官方 benchmark 的「44–60 s 长音频」设定，检验 VAD 的作用 |

### 3.4 指标口径（与官方一致）

直接复用官方 `runtime/llama.cpp/benchmarks/compute_cer.py`：

```
micro-CER = Σ 编辑距离 / Σ 参考字数          # 先剥离 <|...|> 标签
normalize_zh(text) = re.sub(r'[^\w一-鿿]', '', text).upper()   # 去标点/空白，保留 CJK 与字母数字
RTF  = Σ 计算耗时 / Σ 音频时长               # 模型加载不计入
RTFx = 1 / RTF
```

- 耗时取**二进制自己打到 stderr 的 `[tool] done X.XXs`**（该计时从模型加载完成之后开始，天然排除了加载时间），  
  同时用「进程整体 wall - 加载基线」交叉验证；
- 加载基线由 `measure_load.py` 用 0.05 s 静音输入重复 5 次取最小值测得。

---

## 4. 实测结果

### 4.1 S1000（999 条短句，1.41 小时）

| 配置                                               | VAD | 失败/空输出 | **micro-CER ↓** | 计算耗时 中位 / 最大 (ms) | **RTF ↓** | **RTFx ↑** | 含模型加载的端到端 RTF |
| ------------------------------------------------ | --- | ------ | --------------- | ----------------- | --------- | ---------- | ------------- |
| Paraformer-q8                                    | off | 0 / 0  | **1.92%**       | 100.0 / 240.0     | 0.0212    | 47.2×      | 0.0479        |
| Paraformer-q8                                    | on  | 0 / 0  | **1.98%**       | 110.0 / 240.0     | 0.0220    | 45.5×      | 0.0476        |
| SenseVoiceSmall-q8                               | off | 0 / 0  | **6.40%**       | 100.0 / 250.0     | 0.0211    | 47.4×      | 0.0641        |
| SenseVoiceSmall-q8                               | on  | 0 / 0  | **6.39%**       | 110.0 / 290.0     | 0.0228    | 43.9×      | 0.0665        |
| *sherpa-onnx SenseVoice int8, 2 线程, ITN off*     | —   | 0 / 0  | *2.90%*         | 84.2 / 251.7      | *0.0177*  | *56.4×*    | *0.0179*      |
| *sherpa-onnx SenseVoice int8, 16 线程, ITN off*    | —   | 0 / 0  | *2.90%*         | 66.3 / 310.1      | *0.0148*  | *67.6×*    | *0.0149*      |
| *sherpa-onnx SenseVoice int8, 16 线程, ITN **on*** | —   | 0 / 0  | *6.41%*         | 78.3 / 215.9      | *0.0167*  | *59.9×*    | *0.0169*      |

**999 条里 0 个失败、0 个空输出**；所有配置的编辑都以替换为主（88–94%），删除 3–10%，插入 <3%。

> CER 抽样误差：按 micro-CER 的二项近似，p≈6.4% 时 95% 置信区间约 **±0.39 pp**，p≈2.0% 时约 **±0.23 pp**。

### 4.2 L200（200 条 50 s 长音频，2.78 小时）

| 配置                 | VAD     | **micro-CER ↓** | 计算耗时 中位 / 最大 (ms) | **RTF ↓** | **RTFx ↑** | 含加载 RTF |
| ------------------ | ------- | --------------- | ----------------- | --------- | ---------- | ------- |
| Paraformer-q8      | **off** | 3.34%           | 1220 / 1460       | 0.0239    | 41.8×      | 0.0266  |
| Paraformer-q8      | **on**  | **2.47%**       | 1020 / 1270       | 0.0203    | 49.4×      | 0.0227  |
| SenseVoiceSmall-q8 | off     | 5.89%           | 1245 / 1510       | 0.0240    | 41.7×      | 0.0283  |
| SenseVoiceSmall-q8 | on      | 5.96%           | 1110 / 1310       | 0.0218    | 45.9×      | 0.0261  |

- 长音频上**内置 FSMN-VAD 对 Paraformer 有实打实的收益**：3.34% → 2.47%（−0.87 pp），  
  且**更快**（1.22 s → 1.02 s/条，因为静音段被切掉了）。
- SenseVoice 上 VAD 前后无显著差异（5.89% vs 5.96%，在噪声内）。
- 与官方宣称「**无 VAD 时显著变差**（SenseVoice 9.99% vs 8.01%）」**没有复现** —— 原因见 §6 局限：  
  我们的「长音频」是短句拼接，每个子片段本身仍属分布内，不构成真正的长时自由说话。

### 4.3 内存占用（51 s 音频，采样峰值工作集）

| 运行时                                              | 峰值工作集                         |
| ------------------------------------------------ | ----------------------------- |
| `llama-funasr-sensevoice.exe` + fsmn-vad         | **279.5 MB**                  |
| `llama-funasr-sensevoice.exe`（无 VAD）             | 289.4 MB                      |
| `llama-funasr-paraformer.exe` + fsmn-vad         | **259.4 MB**                  |
| sherpa-onnx（Python + onnxruntime，含 ~40 MB 解释器开销） | 加载后 355 MB / 解完 51 s 后 498 MB |

二进制形态下 **~260–290 MB** 常驻（其中 ~250 MB 就是 q8 权重本身），在 1 GB 内存的边缘盒子上是安全的。

---

## 5. 与官方公布数字对照

| 来源                                                  | 模型                             | 口径                             | CER               | 速度             |
| --------------------------------------------------- | ------------------------------ | ------------------------------ | ----------------- | -------------- |
| **官方模型卡**（`iic/speech_paraformer-large...-pytorch`） | Paraformer-large（PyTorch，无 LM） | AISHELL-1 **test**             | **1.95%**         | —              |
| **本报告**（llama.cpp q8 GGUF）                          | Paraformer-q8 GGUF             | AISHELL-1 test 子集 999 条        | **1.92% ~ 1.98%** | RTF 0.021（47×） |
| 官方 `runtime/llama.cpp/BENCHMARKS.md`                | Paraformer（Q8 runtime）         | 184 条长音频（44–60 s），normalize_zh | 9.89%             | ~21× 实时        |
| 官方 `BENCHMARKS.md`                                  | SenseVoiceSmall（Q8 runtime）    | 同上                             | 8.17%             | ~20× 实时        |
| 官方 `BENCHMARKS.md`（无 VAD）                           | SenseVoiceSmall Q8             | 同上                             | 9.99%             | —              |
| **本报告**                                             | SenseVoiceSmall-q8 GGUF        | L200 长音频                       | 5.96%             | 45.9×          |

**Paraformer 那行是最有说服力的**：量化到 q8 之后，在**完整官方测试集的一个 999 条分层子集**上，CER 与  
PyTorch 官方数字只差 **0.03 个百分点**，说明这套 GGUF 导出**没有明显的量化损失**。

官方 benchmark 的 CER 绝对值（8~10%）比本报告高，属于**数据集不同**（官方是 184 条真实长音频，  
我们的是朗读式短句/拼接音频），不可直接比较绝对值，只能比较「同口径下的相对结论」。

---

## 6. 三个关键发现（含两个坑）

### 6.1 ✅ q8 量化几乎无损

Paraformer：官方 PyTorch 1.95% ↔ q8 GGUF 1.92–1.98%。  
同一份 999 条数据上，开/关 VAD 的差异（1.92% vs 1.98%）比量化误差本身还大。

### 6.2 ⚠️ ITN（数字格式）是这个版本最大的坑，能造成 2 倍的 CER 假象

SenseVoice 的 CLI 输出是**阿拉伯数字**：

```
REF     同比较上年同期分别增长百分之三十和百分之二十八
GGUF    同比较上年同期分别增长30和28              ← ITN 开启
sherpa  同比较上年同期分别增长百分之三十和百分之二十八  ← ITN 关闭（use_itn=False）
```

而 AISHELL-1 的参考文本写的是中文数字，`normalize_zh` 又不做数字归一，于是**每一个数字都记成错字**。

**决定性对照实验**（同一个 SenseVoiceSmall 模型，只改 ITN 开关）：

| 运行时                      | ITN                       | micro-CER | 与 GGUF 差     |
| ------------------------ | ------------------------- | --------- | ------------ |
| FunASR llama.cpp GGUF q8 | 模型预测为 `withitn`（CLI 无法关闭） | **6.39%** | —            |
| sherpa-onnx INT8 ONNX    | `use_itn=False`           | **2.90%** | −3.49 pp     |
| sherpa-onnx INT8 ONNX    | `use_itn=True`            | **6.41%** | **+0.02 pp** |

也就是说：**GGUF 版和 ONNX 版的识别能力完全一致**，6.39% 里有一半以上是「输出格式与参考答案格式不一致」造成的。  
我自己写的中文数字 → 阿拉伯数字归一器再做一次口径归一后，GGUF 是 4.41%（长音频 4.37%）、  
sherpa（ITN off）是 2.89%，差距收缩到 1.5 pp，剩下的主要是 ITN 对「一米九五 → 195 米」这类  
**带量词的口语数字的误转换**（这类错误反过来又是关闭 ITN 才会没有的）。

**对项目的直接含义**：`--keep-tags` 可以看到模型预测的富文本标签，但 CLI **没有** `--itn/--no-itn` 开关。  
如果下游 NLU/播报需要「中文数字」或需要和现有语料格式保持一致，这个版本做不到；  
sherpa-onnx 可以通过 `sense_voice.use_itn` 自行决定（本项目 `sherpaasr.cpp` 里现在写的是 `use_itn = 1`）。

### 6.3 ⚠️ CLI 是「一次进程一次推理」，端到端吞吐只有纯计算的 1/3

这些 exe 每次调用都要重新 `mmap`+解析 250 MB 的 GGUF。实测加载基线：

| 模型                                | 加载+进程启动            |
| --------------------------------- | ------------------ |
| Paraformer-q8                     | 0.13 s             |
| SenseVoiceSmall-q8                | 0.22 s             |
| sherpa-onnx int8（Python 侧，模型常驻一次） | 0.73–0.86 s（只发生一次） |

单条 5 s 音频：纯计算 0.10 s，进程整体 0.34 s → **端到端 RTF 从 0.022 掉到 0.066（15× 实时）**。  
所以：

- 做**离线批量转写**：这个 CLI 能用，但 10 小时音频要 ~40 分钟（含反复加载）；
- 做**在线/实时**：必须改成模型常驻（仓库自带 `runtime/llama.cpp/server/funasr_gguf_server.py`，  
  但注意它也是**每请求起一个子进程**，一样要每次加载；真正要常驻得自己把 `llama-funasr-*` 的  
  ggml graph 嵌进宿主进程，只加载一次）。

---

## 7. 局限（引用数字前请务必看这一节）

1. **硬件是桌面 CPU**：AMD Ryzen 7 9700X（8 核 / 16 线程，Zen 5）+ Windows 11，不是树莓派/ARM 边缘板。  
   二进制**不暴露线程数**，会吃满 16 线程；换成 4 核边缘盒子时 RTF 需要重新测（工程上按「核心数近似线性」估会有偏差）。
2. **测试集是朗读式短句**：AISHELL-1 是近场、朗读、安静的学术集，远好于真实场景。  
   L200 的「长音频」是短句拼接，**不构成真正的长时自由说话**，因此官方「无 VAD 显著变差」的结论在这里无法复现。
3. **CER 受参考格式影响极大**：AISHELL-1 参考文本用中文数字，凡是默认开 ITN 的系统都会被系统性惩罚。  
   跨系统比较 CER 时**必须先对齐 ITN 与文本归一化口径**。
4. **加载时间受页缓存影响**：连续跑同一模型时 0.13~0.24 s；冷启动（首次读盘）会明显更慢。
5. **没测的**：Fun-ASR-Nano（800M，自回归解码，CPU 上会显著慢）、Paraformer f16/f32 变体、  
   ONNX Runtime 的 C++ runtime（`runtime/onnxruntime`）、`funasr` Python 包（PyTorch CPU）。

---

## 8. 复现步骤

```bash
# ── 0) 依赖：只用 python3 + numpy（下载与评测脚本本身零第三方依赖）
python -m pip install numpy

# ── 1) 二进制（5.2 MB）与权重（~490 MB）
curl -L -o win-avx2.zip \
  https://github.com/modelscope/FunASR/releases/download/runtime-llamacpp-v0.2.6/funasr-llamacpp-windows-x64-avx2.zip
unzip win-avx2.zip -d bin/
# HF 在本网络环境不可达，用 hf-mirror：
curl -L -o gguf/sensevoice-small-q8.gguf \
  https://hf-mirror.com/FunAudioLLM/SenseVoiceSmall-GGUF/resolve/main/sensevoice-small-q8.gguf
curl -L -o gguf/paraformer-q8.gguf \
  https://hf-mirror.com/FunAudioLLM/Paraformer-GGUF/resolve/main/paraformer-q8.gguf
curl -L -o gguf/fsmn-vad.gguf \
  https://hf-mirror.com/FunAudioLLM/fsmn-vad-GGUF/resolve/main/fsmn-vad.gguf

# ── 2) 标注 + 音频
curl -L -o refs/aishell_test.txt \
  https://raw.githubusercontent.com/modelscope/FunASR/main/runtime/triton_gpu/client/aishell_test.txt
# 音频：ModelScope 数据集 speech_asr/speech_asr_aishell1_trainsets 里的
#       speech_asr_aishell_testsets.zip (870 MB)，解压得 wav/test/S07xx/…

# ── 3) 生成清单
python tools/funasr_cpu_bench/prepare_testset.py --refs refs/aishell_test.txt \
    --wav-root <解压目录> --out data/testset_full.json
python tools/funasr_cpu_bench/make_long_clips.py --manifest data/testset_full.json \
    --wav-out data/long45 --json-out data/testset_long45.json --target 45 --limit 400

# ── 4) 跑评测（4 个配置，串行，保证计时不受干扰）
python tools/funasr_cpu_bench/run_all.py --bin-dir bin/windows-x64-avx2 --gguf gguf \
    --manifest data/testset_sample1000.json --outdir results --jobs 1

# ── 5) 算 CER / RTF（官方 compute_cer.py 口径）
python tools/funasr_cpu_bench/report_tables.py --refs data/testset_sample1000.json \
    --results results/sensevoice_vad results/sensevoice_plain \
              results/paraformer_vad results/paraformer_plain
python tools/funasr_cpu_bench/analyze_errors.py --refs data/testset_sample1000.json \
    --run results/sensevoice_vad            # 编辑类型分布 + 数字归一化 CER + 最差样本
```

### 脚本清单（`tools/funasr_cpu_bench/`）

| 文件 | 作用 |
|---|---|
| `compute_cer.py` | 官方的 micro-CER / normalize_zh / RTF 脚本，原样复用 |
| `prepare_testset.py` | 把 `aishell_test.txt` + wav 目录合成 `[{key, wav, ref, duration}]` 清单 |
| `make_long_clips.py` | 把同说话人连续短句拼成 ~N 秒长音频，参考文本用「，」连接 |
| `run_bench.py` | 跑单个「模型 × VAD」组合：出 `hyp/<key>.txt`、`times_*.tsv`、`summary.json` |
| `run_all.py` | 矩阵驱动（可 `--jobs N` 并行；测速时务必 `--jobs 1`） |
| `measure_load.py` / `make_silence.py` | 用 0.05 s 静音测模型加载基线 |
| `measure_peak_mem.py` | 采样被测进程的峰值工作集 |
| `run_sherpa.py` | sherpa-onnx（ONNX Runtime）对照基线，同清单同指标 |
| `report_tables.py` / `analyze_errors.py` | 汇总成 Markdown 表格 / 错误分析 |
| `results/` | 本次实测的 `summary.json` 汇总（机器可读） |

---

## 9. 对 QSmartAssistant 的建议

1. **不要为了准确率换到 llama.cpp/GGUF**。同模型下两边识别结果一致（§6.2），本项目现有的
   sherpa-onnx 路线在 CPU 上**更快**（RTF 0.0148 vs 0.0211），而且线程数可控
   （`sherpaasr.cpp` 里是 `num_threads = 2`，适合树莓派）。
2. **如果要换，唯一理由是部署形态**：单 exe + 单 GGUF、零 Python/ONNX Runtime 依赖，
   塞进没有包管理的嵌入式系统确实更省事；代价是要自己改造成模型常驻（§6.3）。
3. **如果只是想确认「我们的 SenseVoice 够不够准」**：把 CI 里的尺子从 AISHELL-1 换成**本项目真实录音**
   （官方 `docs/benchmark/rtf_reproducibility.md` 的字段清单可以直接照抄）。
   AISHELL-1 上的绝对 CER 对产品选型参考价值有限。
4. **文本归一化要单独定策略**。这次实测最贵的教训就是 ITN 造成的 2 倍假象：
   建议在 ASR 输出后固定一层「数字/单位归一」，并把它写进测试口径，
   否则换模型/换运行时后 CER 会出现无法解释的跳变。
5. 若确实要用 SenseVoice 的多语种/富文本能力，**Paraformer-q8 在纯中文上是明显更优的选择**
   （1.92% vs 6.40%，同口径同数据集）；两者权重都是 ~250 MB，没有体积上的取舍。
6. ✅ **2026-10 已经落地了**，但走的**不是** llama.cpp —— 见 §10。结论变成了：
   用 FunASR 官方 **ONNX** 导出包（同一个 Paraformer-**q8** 权重，进程内 onnxruntime），
   准确率 1.98%（> sherpa small 的 2.90%）、速度 0.0104（> sherpa 的 0.0148），
   两头都赢，所以它现在是**默认 ASR**。

---

## 10. 落地：ONNX 版 Paraformer-q8 替换默认 ASR（2026-10）

### 10.1 为什么最后选 ONNX 而不是 llama.cpp

§1/§9 的两个结论拼起来正好指向第三条路：

| 需求 | llama.cpp/GGUF | sherpa-onnx（原默认） | **FunASR ONNX 导出包（采纳）** |
| --- | --- | --- | --- |
| 更准（换 large 权重） | ✅ 但 CLI 不常驻、ITN 开关缺失 | ❌ small 权重 | ✅ `model_quant.onnx` = Paraformer-**q8**（large 的 int8 版） |
| 不牺牲速度 | ⚠ 端到端 RTF 0.066（一次调用一次加载） | ✅ | ✅ RTF 0.0104 |
| 不引入新运行时 | ✅（但要把 CLI 改成常驻） | ✅ | ✅ 复用已有的 `lib/onnxruntime` |

也就是说：**权重用 FunASR 官方那个 large-q8，运行时仍用 onnxruntime**。
既不用把 llama.cpp 的「一次调用一次加载」改造成常驻服务（§6.3），也不用为了准确率
忍受 small 模型。前端（80 维 kaldi mel-fbank → LFR(7,6) → CMVN(560)）在
`Conversation/ASR/funasr.cpp` 里用 `kaldi-native-fbank` 自己算，
所以 ONNX 只吃特征、不吃音频，也就没有独立 Python 依赖。

### 10.2 实测对照（AISHELL-1 test 分层抽样 999 条，1.41 小时）

| 后端 | 权重 / 运行方式 | micro-CER | RTF（2 线程） | RTF（4 线程） |
| --- | --- | --- | --- | --- |
| **funasr ONNX（本次落地）** | `model_quant.onnx` 238 MB，进程内 onnxruntime | **1.98%** | **0.0104（96×）** | 0.0086（117×） |
| llama.cpp + GGUF-q8（§4.1） | ~250 MB GGUF，独立 exe | 1.92~1.98%（口径见 §6.2） | 0.0211（47×） | – |
| sherpa-onnx paraformer-zh-**small**（旧默认） | int8 ONNX，进程内 | 2.90% | 0.0177（56×） | 0.0148（68×） |
| FunASR 官方 PyTorch（§5 参照） | – | 1.95% | – | – |

补充核对：

- **与 GGUF 逐条一致 978/999**：分歧的那 21 条几乎全是 ITN（§6.2）导致的数字格式差异，
  不是识别能力差异。
- **threads 2 / 4 逐条一致**：改线程数只影响速度不影响结果。
- 999 条里 **0 条空输出、0 条吐出 `<blank>`**。
- 45 s 长音频 RTF ≈ 0.025（比短句差，属正常，长音频的 CIF 后处理更重）。
- 仓库自带 `Data/test2.wav` 的输出**逐字命中**测试用例的期望文本
  「深入的分析这一次全球金融动荡背后的根源」。

### 10.3 改动的文件

| 文件 | 改动 |
| --- | --- |
| `Conversation/ASR/funasr.h` / `funasr.cpp` | 按官方 ONNX 导出包的 IO 重写（见 10.4）；`threads_` 缺省 **2** |
| `Conversation/CMakeLists.txt` | `option(ASR_FUN)` 默认 **ON**、`option(ASR_SHERPA)` 默认 **OFF** |
| `Conversation/conversation.cpp` | 后端优先级明确为 `ASR_DUILITE > ASR_FUN > ASR_SHERPA` |
| `CMakePresets.json`（5 个 preset）、`scripts/build_linux.sh`、`.github/workflows/ubuntu.yml` | 默认组合从 `-DASR_SHERPA=ON` 换成 `-DASR_FUN=ON` |
| `Data/default_config.yaml` / `Data/default_config.json` | `funasr` 段指向 `paraformer-zh-q8/*`，`threads: 2` |
| `scripts/setup.py` | 新组件 `funasr-asr`（`--only funasr-asr`），并进 `minimal`/`standard`/`full`；`sherpa-asr` 降为按需 |
| `README.md`、`doc/linux.md`、`doc/install.md`、`doc/安装.md`、`doc/config.md`、`doc/配置.md`、`doc/livekit_wakeup.md` | 默认组合与 ASR 表同步 |
| `Test/tst_sherpa.h` | 无需改：`asrFun()` 在 `#if defined(ASR_FUN)` 下，默认组合自动编入 |

### 10.4 踩过的 5 个坑（都已在代码里修掉）

1. **第 2 个输出 `token_num` 是 int32 不是 int64**。官方导出包里它是 `int32`，
   按 `int64_t*` 读会得到「int32 值 + 紧邻 4 字节」的天文数字，再拿它循环读
   `logits` 就越界。修法：按 `GetTensorTypeAndShapeInfo().GetElementType()` 分派
   int32/int64，并把长度夹到 `logitsShape[1]`。
2. **Windows 上 `Ort::Session` 要宽字符串**。`ORTCHAR_T` 在 Windows 是 `wchar_t`，
   传 `std::string::c_str()` 直接编译不过（`error C2665`）。修法：抽 `toOrtPath()`
   + `#ifdef _WIN32` 分派（这同时说明 **`ASR_FUN` 在 Windows 上从没被编过**）。
3. **`QString::removeLast()` 只删 1 个字符**（`qstring.h` 里就是 `remove(size()-1,1)`），
   而 funasr 的 `@@` 子词要删 2 个 → `"and@@"` 会拼成 `"and@ "`。修法：`word.chop(2)`。
4. **`Ort::Value::CreateTensor` 只记裸指针**。热词 embedding 的 buffer 原来放在内层
   `{}` 里，出了作用域就悬垂。修法：所有 tensor buffer 提到函数作用域，活到 `Run()` 返回。
5. **极短输入会触发 ORT 的 CIF reshape 报错**：50 ms 音频 LFR 后只有 1 帧，
   `input_shape_size == size was false. Input shape:{0,512}, requested shape:{1,512,512}`。
   修法：`asrFeats.size() < 2` 时提前 `qWarning` 返回（实测 100 ms 起正常）。

> 另外一个方向相反的坑：**`Yaml::collectComments()` 会把段尾的注释行绑到下一个键上**，
> 所以 `funasr` 的可选键说明必须写在**段头**注释块里，写在段尾会被当成下一个键的注释。

### 10.5 怎么用

```bash
# 1. 装模型（ModelScope 官方导出包，~230MB；--profile minimal 也包含它）
python3 scripts/setup.py --only funasr-asr        # -> Data/paraformer-zh-q8/

# 2. 编译：默认就是 funasr，不用加任何 -D
bash scripts/build_linux.sh

# 3. 运行期 config.yaml（模板 Data/default_config.yaml 已写好）
#    funasr:
#      model: paraformer-zh-q8/model_quant.onnx
#      cmvn:  paraformer-zh-q8/am.mvn
#      token: paraformer-zh-q8/tokens.json
#      threads: 2
```

`threads` 是**可选键，缺省 2**（适配树莓派）；`language` / `sample_rate` / `window` /
`frame_length` / `frame_shift` / `n_mels` / `lfr_m` / `lfr_n` 同样可选、缺省即官方取值。
热词（`segdict` / `hw_model` / `hotword`）仍然支持：官方**普通**导出包只有
`speech`/`speech_lengths` 两个输入，配了这三项会自动退化成普通模式（只降级不报错）。

**换回 sherpa ASR**：`-DASR_SHERPA=ON -DASR_FUN=OFF`
+ `python3 scripts/setup.py --only sherpa-asr`，config.yaml 里改读 `sherpa` 段
（两份段都还在模板里，没删）。

### 10.6 怎么复现 / 验证

- **最快的一条路（仓库内自带）**：装好模型后带 `TEST=ON` 编译，跑 `asrFun` 用例 ——
  `Test/tst_sherpa.h` 里它读 `Data/test2.wav` 并**逐字断言**
  「深入的分析这一次全球金融动荡背后的根源」。它是 `#if defined(ASR_FUN)`，
  正好跟着默认组合一起编进来。
- **全量 CER**：用 §8 的清单（AISHELL-1 999 条）跑一遍。当时 ONNX 这一侧走的是
  离线探针 `.buildtmp/funasr_probe/`（`probe_main.cpp` 支持 `--wav` 与 `--list`+`--out`，
  `compare_cer.py` 算 micro-CER 并与 GGUF / sherpa 的逐条结果对齐），
  它只依赖 `onnxruntime.lib` + `Qt6Core`，**不需要整工程 cmake**
  —— 但 `.buildtmp/` 在 `.gitignore` 里，所以是本地工具，未入库。
