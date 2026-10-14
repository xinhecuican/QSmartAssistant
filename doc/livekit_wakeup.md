# livekit-wakeword 唤醒引擎（你好小智）

把自训练的 [livekit-wakeword](https://github.com/livekit/livekit-wakeword) 唤醒词模型  
接入 QSmartAssistant 的 `Wakeup` 模块。当前接入的唤醒词&#x662F;**「你好小智」**  
（分类头 `size_med_hnx3_ca_mw50.onnx`，**conv_attention** medium 规模 + 近音硬负样本×3，  
`FPPH≤1` 工作点 recall **0.8443** / FPPH **0.9220**；另带 153 KB 的省算力备选  
`size_small_hnx2_ca_mw50.onnx`，见 §6.3 / §6.6）。

---

## 1. 推理管线

livekit-wakeword 的推理是**三段 ONNX 前向**，与原版 openWakeWord 同源：

| 阶段      | 模型                             | 输入                           | 输出                                     |
| ------- | ------------------------------ | ---------------------------- | -------------------------------------- |
| 1. 梅尔谱  | `melspectrogram.onnx`          | `(1, n)` float32，16 kHz 原始音频 | `(1, 1, F, 32)` dB 谱，再按 `x/10 + 2` 归一化 |
| 2. 语音嵌入 | `embedding_model_int8_pc.onnx` | `(1, 76, 32, 1)` 梅尔窗         | `(1, 1, 1, 96)` 嵌入                     |
| 3. 分类头  | `size_med_hnx3_ca_mw50.onnx`   | `(N, 16, 96)` 嵌入序列           | `(N, 1)` 置信度 ∈ [0,1]                   |

- 梅尔窗：**76 帧窗、步长 8 帧**；
- 分类器输入 = **最近 16 个嵌入**（2.0 s 音频恰好 16 个）；
- 正样本在数据侧对齐到窗口**末尾**，因此滑窗「右端对齐唤醒词」时得分最高。

C++ 实现（Qt 无关）位于 `Wakeup/Livekit/livekitwakeword.{h,cpp}`；  
Qt 适配层 `Wakeup/Wakeup/livekitwakeup.{h,cpp}` 继承 `WakeupModel`。

---

## 2. 准备模型

`melspectrogram.onnx` / `embedding_model.onnx` 来自 openWakeWord 官方 release，由 `setup.py`  
自动下载；**分类头是本项目自训练产物，已随仓库提交**（`Data/livekit/`），clone 后不需要  
任何参数。只有想换成别的自训练唤醒词时，才用脚本从 livekit-wakeword 训练产物收集：

```bash
python scripts/prepare_livekit_models.py \
    --src  <livekit-wakeword 仓库或 worktree 根目录> \
    --dst  Data/livekit \
    --wakeword g_medium_hnx3_mw50_hnx3m2
```

产出 `Data/livekit/`：

```
melspectrogram.onnx          # 来自 livekit-wakeword/src/livekit/wakeword/resources/
embedding_model.onnx         # fp32 原版（回退用：改 config 的 emb 一行即可）
embedding_model_int8_pc.onnx # int8 量化版（默认，见 §9.4）
size_med_hnx3_ca_mw50.onnx   # 当前分类头（conv_attention medium + 硬负×3）
size_small_hnx2_ca_mw50.onnx # 省算力备选（conv_attention small + 硬负×2，153 KB，阈值 0.46）
```

> **坑（IR version）**：新版 PyTorch 导出的 ONNX 声明 `ir_version = 10`，而本仓库  
> `lib/onnxruntime` 是 **1.17.1**，只支持 `IR ≤ 9`，直接加载会报  
> `Unsupported model IR version: 10`。分类头只用到 opset-18 算子，因此脚本会把  
> 声明版本就地降级为 9（不改图）。若日后升级 onnxruntime ≥ 1.18 则无需降级。

运行期模型放在 `~/.config/QSmartAssistant/Data/livekit/`（把 `Data/livekit/` 拷过去即可）。

---

## 3. 构建与启用

```bash
cmake -S . -B build -DWAKEUP_LIVEKIT=ON
cmake --build build --config RelWithDebInfo --target QSmartAssistant
```

- `WAKEUP_LIVEKIT` 默认 **ON** —— 它是**默认唤醒引擎**（默认安装 =  
  livekit 唤醒 + silero VAD + FunASR(Paraformer-q8) ASR + sherpa TTS + ONNX NLU）。  
  若要退回 sherpa-kws，需显式 `-DWAKEUP_LIVEKIT=OFF -DWAKEUP_SHERPA=ON`。
- 与 `WAKEUP_SHERPA` 同时开启时 **livekit 优先生效**（见 `Wakeup/wakeup.cpp` 的 `#if` 顺序）。
- 依赖 `lib/onnxruntime`（已随仓库提供，`add_custom_lib(onnxruntime)`）。

---

## 4. 配置（`livekit_wakeup`）

```jsonc
"livekit_wakeup": {
    "dir": "livekit",              // 模型子目录（相对 Data/）
    "model": "size_med_hnx3_ca_mw50.onnx",  // 分类头（conv_attention medium + 硬负×3；备选 size_small_hnx2_ca_mw50.onnx）
    "mel": "melspectrogram.onnx",
    "emb": "embedding_model_int8_pc.onnx",  // int8 量化版（fp32 原版 = embedding_model.onnx，见 §9.4）
    "threshold": 0.48,           // 判定阈值（该模型 FPPH<=1 的最优工作点，见 §6.3；小备选用 0.46）
    "trigger_level": 2,            // 连续命中步数（每步 80 ms）
    "refractory": 25,              // 触发后静默步数（约 2 s，防重复唤醒）
    "chunk_bytes": 2560,           // 每次送入 2560 字节 = 1280 采样 = 80 ms
    "window_samples": 32000,       // 滑窗长度，2.0 s，**不要改**
    "threads": 1,                  // onnxruntime intra-op 线程
    "incremental": true,           // 嵌入增量：每步只算最新 1 个窗，见第 7 节
    "incremental_mel": true,       // mel 增量：每步只算新增 8 帧，见 §9.5
    "mel_top_db": 80               // melspectrogram.onnx 内 power_to_db 的 top_db
}
```

一次「你好小智」会连续几十步（约 2 s）得分超阈值，靠 `trigger_level` + `refractory`  
聚合成一次唤醒事件。

---

## 5. 离线评估工具

`tools/livekit_wakeup_eval`（不需要 Qt，只依赖 `lib/onnxruntime`）：

```bash
# 用 MSVC 直接编译
tools/livekit_wakeup_eval/build.bat

# 或用 CMake
cmake -S tools/livekit_wakeup_eval -B tools/livekit_wakeup_eval/build
cmake --build tools/livekit_wakeup_eval/build --config Release
```

模式（`--mode`）：

| 模式                         | 作用                                                             |
| -------------------------- | -------------------------------------------------------------- |
| `features`                 | 读 livekit 的 `.npy` 特征集，只跑分类头，复算官方指标（与 Python `*_eval.json` 对齐） |
| `verify`                   | 用 C++ 完整音频管线抽特征，与 `.npy` 逐元素比对，验证移植无损                          |
| `audio`                    | 对测试集 wav 跑完整管线（mel→emb→分类头）打分并算指标                              |
| `streamlong`               | 拼接长音频逐步推进，校验流式稳态一致性 + 每步耗时                                     |
| `bench`                    | 各段 ONNX 推理耗时分解                                                 |
| `melprobe` / `streamprobe` | 诊断用（梅尔谱长度相关性 / 逐窗对齐）                                           |

公共参数：`--mel --emb --clf`（三个模型路径）、`--threads`。

---

## 6. 验证结果

> **模型代际说明**：训练仓库里「你好小智」有两个代际，指标**不可直接比较**（验证集不同）：
>
> - `nihao_xiaozhi`（medium，全量 5000 条脏语料）在 **3000 条脏验证集**上评；
> - `size_small` / `nihao_xiaozhi_clean`（干净语料，`0.7s ≤ 时长 < 1.5s`）在  
>   **1830 条干净验证集**上评。脏语料里混了大量 VoxCPM「啰嗦」长音频（>4s 召回仅 0.024），  
>   属于标签噪声。同口径（干净集）下脏→干净是 0.744 → 0.833。
>
> **当前部署的是 `size_med_hnx3_ca_mw50`**（2026-10-14 起）：**conv_attention** medium 规模 +  
> 近音/含唤醒词硬负样本在负样本池里放大 **×3**（`hardneg_mult=3`）。选它的理由：  
> `r@FPPH≤1` 两颗种子 **0.8443 / 0.8464（±0.0010，全网格最稳）**、AUT **0.00345**（四代最佳）、  
> 工作点近音误报率 **1.13%**（上一代 1.88%），而体积与单步开销和上一代**完全相同**  
> （923 KB、分类头 0.070 ms、整步 0.92 ms）→ 零成本升级。仓库另带 153 KB 的  
> `size_small_hnx2_ca_mw50.onnx`（`r@FPPH≤1` 0.8536、AUT 0.00431，阈值改 0.46）供低算力设备。
>
> 演进过程：`size_small`（dnn，无硬负）→ `size_hn_mw100`（dnn + 硬负，修近音误报但召回持平）  
> → `size_hn_ca_mw60`（conv_attention + 硬负×1，召回与近音双双改善，**首次达标**）  
> → `size_med_hnx3_ca_mw50`（硬负×3，近音误报再压 40%、种子方差降到 ±0.1pt）。  
> ⚠ 前三代的模型本体（`size_small` / `size_hn_mw100` / `size_hn_ca_mw60` / `nihao_xiaozhi`）  
> 已从 `Data/livekit/` 移除；指标全部留档在 §6.3，模型本体见训练仓库 `output/`。

### 6.1 移植无损性

- **特征级**（`verify`，200 条正样本，1536 维/条）：  
  `max|C++ − Python npy| = 2.3e-05`，`mean = 3.2e-06` —— float32 舍入量级。  
  （mel / emb 两段与分类头代际无关，此结论对任何分类头都成立。）  
- **分类头指标**（`features`，1830 正 / 33204 负，18.45 h）以最初的 `size_small` 为样例与 Python  
  `output/size_small/size_small_eval.json` **逐项完全一致**（后续每一代都按同样方式复验，  
  当前 `size_med_hnx3_ca_mw50` 的 C++ 尺子结果 `0.8344 / 0.7590 / AUT 0.0034 / thr 0.48` 与  
  训练仓库 `scripts_eval_hn.py` 的输出一致到末位）：

| 指标               | C++                           | Python                                  |
| ---------------- | ----------------------------- | --------------------------------------- |
| recall @0.5      | 0.8180                        | 0.8180327868852459                      |
| accuracy @0.5    | 0.9085                        | 0.9084592316548865                      |
| FPPH @0.5        | 2.0058                        | 2.0057824358511023                      |
| AUT              | 0.0094                        | 0.009361116212873163                    |
| 最优阈值             | 0.55                          | 0.55                                    |
| 最优 recall / FPPH | 0.7934 / 0.9216               | 0.7934426229508197 / 0.9215757137694255 |
| 混淆矩阵 @0.5        | tp=1497 fn=333 fp=37 tn=33167 | —                                       |

- 该模型在 **FPPH ≤ 1** 的工作点上召回 **0.7934 / FPPH 0.9216**，  
  相比上一代 `nihao_xiaozhi` 的 0.6070 / 0.8674（同为 FPPH≤1 口径）提升约 **+18.6pt**。  
> ⚠ 上表是**第一代 `size_small`** 时代的移植验证记录（数值只对那一代表头成立），  
> 保留它是为了说明 C++/Python 两把尺子等价；当前代的指标见 §6.3。

### 6.2 流式（实际部署路径）

`streamlong`：40 s 连续音频、500 个 80 ms 步长、抽检 119 步（`size_small` 分类头，结论与代际无关）。

| 模式             | 与无状态参考         | 阈值翻转  | 每步耗时     | 占 80 ms   |         |      |
| -------------- | -------------- | ----- | -------- | --------- | ------- | ---- |
| 精确（每步重算 16 个窗） | **逐位一致 (0.0)** | 0/119 | 9.10 ms  | 11.4%     |         |      |
| 增量（每步只算 1 个窗）  | max            | Δ     | ≤ 2.2e-2 | **0/119** | 1.05 ms | 1.3% |

原始指标 JSON 归档在 `doc/livekit_wakeup_results/`  
（当前部署模型为 `size_med_hnx3_ca_mw50`；`size_hn_ca_mw60` / `size_hn_mw100` /  
`size_small` 为历史代际留档；`features_metrics.json`、`audio_metrics.json` 为更早的  
`nihao_xiaozhi` 留档）。

### 6.3 代际指标对比（1830 正 / 33204 负 = 18.45 h）

| 指标               | `size_small`（一代）     | `size_hn_mw100`（二代） | `size_hn_ca_mw60`（三代） | **`size_med_hnx3_ca_mw50`（当前）** |
| ---------------- | -------------------- | ------------------- | --------------------- | ------------------------------- |
| 架构 / 负样本         | dnn / 无硬负            | dnn / 有硬负           | conv_attention / 硬负×1 | **conv_attention / 硬负×3**       |
| recall @0.5      | 0.8180               | 0.7443              | 0.8153                | **0.8344**                      |
| FPPH @0.5        | 2.0058               | **0.2711**          | 0.4879                | 0.7590                          |
| AUT（越小越好）        | 0.0094               | 0.0084              | 0.0040                | **0.00345**                     |
| 最优阈值（FPPH≤1）     | 0.55                 | 0.42                | 0.45                  | **0.48**                        |
| 最优 recall / FPPH | 0.7934 / 0.9216      | 0.7929 / 0.9216     | 0.8459 / 0.9758       | **0.8443 / 0.9220**             |
| 混淆矩阵 @0.5        | tp=1497 fn=333 fp=37 | tp=1362 fn=468 fp=5 | tp=1492 fn=338 fp=9   | tp=1527 fn=303 **fp=14**        |
| **近音误报率 @工作点**   | 3.75%                | 2.70%               | 1.88%                 | **1.13%**                       |

> 省算力备选 **`size_small_hnx2_ca_mw50`**（153 KB，conv_attention small + 硬负×2）：  
> `recall@0.5 0.8383 / FPPH@0.5 0.4879 / AUT 0.00431`，工作点 **thr 0.46 → recall 0.8536 /  
> FPPH 0.7589**，近音误报 2.40%。切换只需改 config 里 `model` + `threshold` 两行。

**当前模型满足用户目标 `FPPH<1 且 recall>0.82`**（0.9220 < 1 且 0.8443 > 0.82），且 AUT 是  
四代最好。分类头 `classifier(batch1) = 0.070 ms/步`，整步 **0.92 ms = 80 ms 预算的 1.15%**  
（`--mode bench` 实测，见 §6.6）。并行跑的另一颗种子 `g_medium_hnx3_mw50_hnx3m` 是  
`0.8464 / 0.8670`、AUT 0.00387、近音误报 1.58% —— 按 `AUT + r@FPPH≤1 + 近音误报@工作点`  
三件套，部署的 `..._hnx3m2` 在 AUT（0.00345 vs 0.00387）和近音误报（1.13% vs 1.58%）上更好、  
recall 只低 0.21pt，故取它；两者差 0.21pt 也说明 `medium×3` 这一格**本身很稳**。

逐短语近音误报率 @工作点（三代 `ca_mw60` → 当前，完整表见 §10.3）：  
`你好吗小智` 0.017→**0.000**、`嘿小智` 0.017→**0.000**、`呼叫小智` 0.033→**0.000**、  
`小智小智` 0.033→**0.000**、`你好小智同学` 0.033→**0.000**、  
`你好小` 0.017→0.033、`你好小智呀` 0.050→0.050、`你好呀小智` 0.100→**0.083**。  
工作点下 15 行误报里（`0.0113 × 1332`），**真正的来源只剩四个**：`你好呀小智` 0.083（5 行）、  
`你好小智呀` 0.050（3 行）、`您好小智` 0.056（2 行）、`你好小` 0.033（2 行）——都是  
「离唤醒词只差一个音节」；剩下 3 行是下面那条无效样本 `尼好小智`。

⚠⚠ **但逐短语数字本身不可靠**：硬负样本测试集只有 **444 clip / 1332 行 / 26 短语**，  
且分布极不均（`你好小芝` 6 条、`您好小智` 12 条、**`尼好小智` 仅 1 条、`你号小智` 2 条**）。  
`尼好小智` 报出的 `1.00` 就是那**一条** clip，而它的 ASR 转写是「**哎呀**好小智」  
——音频里真的含「好小智」，触发是合理的。**凡涉及 `尼好小智`/`你号小智` 的逐短语数字一律作废**；  
其余短语 0.033 与 0.083 的差别也只等于 1~5 条 clip，落在噪声内，不足以给模型排序。  
（行数→clip 数 ≈ ÷3：`你好小芝` 18 行 = 6 条、`您好小智` 36 行 = 12 条、  
`尼好小智` 3 行 = 1 条、`你号小智` 6 行 = 2 条。）

增量 mel 流式路径与分类头代际无关：`--mode incmel` 下增量近似 **0 次阈值翻转**，  
对当前 `size_med_hnx3_ca_mw50` 复验同样成立（见 §9.5）。

### 6.4 为什么换 `conv_attention`

大网格（**85 个模型**，见训练仓库 `output/hn/RESULTS.md`）里，此前从未试过的轴起了决定性作用：

- **`model_type: conv_attention`**（repo 默认值，但 `configs/size_*.yaml` 一直写的是 `dnn`）：  
  同配置（medium/mw60）`r@FPPH≤1` 从 0.7574 → **0.8459**，AUT 0.00935 → **0.00398**。  
  参数量与 `dnn` 几乎一样（945 KB vs 951 KB ONNX），运行时代价可忽略。  
  `dnn` 是纯 MLP，对 16 帧 × 96 维嵌入序列只能全连接硬记，分不开「你好小智」和  
  「你好吗小智」这种只差一个音节的对手；`rnn` 也能到 AUT 0.00398（0.8142）。
- **`steps: 6000 → 12000`** 与架构**不单调互补**：
  | 配置                             | 6000 步     | 12000 步                   |
  | ------------------------------ | ---------- | ------------------------- |
  | medium / mw60 / conv_attention | **0.8459** | 0.8109（两种子 0.8109/0.7978） |
  | large / mw60 / conv_attention | –（未跑 6k） | **0.8568**（单种子，§10.4 已被 `large×2` 超过） |
  | medium / mw50 / conv_attention | –（未跑 6k）   | 0.8339 / 0.8464           |
  | dnn（各档）                        | 基准         | +3.5~4.5pt                |
- ⚠⚠ **run-to-run 方差是本仓库最大的方法学陷阱**（trainer 无 seed 控制）：
  | 配置                                     | 种子1        | 种子2    | 差         |
  | -------------------------------------- | ---------- | ------ | --------- |
  | medium / mw60 / conv_attention / 6000  | **0.8459** | 0.7568 | **8.9pt** |
  | large / mw70 / conv_attention / 6000   | 0.8027     | 0.8464 | 4.4pt     |
  | medium / mw50 / conv_attention / 12000 | 0.8339     | 0.8464 | 1.3pt     |
  | medium / mw60 / conv_attention / 12000 | 0.8109     | 0.7978 | 1.3pt     |
  步数多的族（12000）明显更稳（1.3pt vs 4~9pt）—— 这本身就是一个选型理由。  
  **结论：必须多种子，按 `AUT + r@FPPH≤1 + 近音误报@工作点` 三件套选，  
  不要凭单次结果。** 当前部署的 `...hnx3m2`（0.8443）是**可复现**的点（补种子 0.8464，差 0.21pt），  
  而单次更高的 `g_large_hn_mw60_ca12k_x1`（0.8568）/ `g_large_hnx2_mw60_hnx2b`（0.8650）  
  都只有单种子且体积 4×，故不采纳（见 §10.4）。

### 6.5 已证伪的方向（不要再试）

- **长样本过采样**（1.44s 档 ×3）：0.8126 → 0.7896，且其它时长档还变差。
- **正样本滑窗 ASR 定位裁剪**（第二轮）：改变了与测试集不一致的对齐约定，更差。
- **语速扰动**（同一句话 resample 到 0.85×/1.18×，正样本 9402 → 21938 行）：  
  0.8219 / 0.8098，对照 0.8459 —— 它**确实**把 1.44 s 档从 0.647 抬到 0.706，  
  但每个其它档都退、AUT 也退。
- 这三条共同指向同一个根因：**模型对 1.44 s 档系统性打低分**（阈值 0.05 时该档  
  recall 0.997，信息完全在；训练/测试的「时长-召回」曲线几乎重合），  
  即「越长越可能是负样本」的**时长先验**，不是听不出唤醒词。  
  ⚠ 关键补充：VoxCPM 的输出时长是**量化**的（只有 0.80/0.96/1.12/1.28/1.44 s 五档），  
  这条捷径在训练集上**永远不被惩罚**，所以三种「改变正样本时长分布」的尝试  
  全部失败也就不奇怪了。要动它，得从**训练时的分层采样/加权**或**真人正样本**入手。


### 6.6 规模 × 硬负上采样 × 速度（全量矩阵）

每格 **2 个种子**（trainer 无 seed 控制）。三把尺子由训练仓库 `scripts_eval_hn.py` 算  
（与 C++ 尺子逐位一致），速度由 `scripts_eval_matrix.py` 调本仓库  
`--mode bench --threads 1` 实测 —— **取 `classifier(batch1)`**，`batch512` 只是吞吐量，  
会低估流式单步延迟。

| 规模                    | 上采样    | **r@FPPH≤1**（均值±半极差） | 硬负@工作点         | clf(batch1) | 整步      | 占 80 ms | 大小         |
| --------------------- | ------ | -------------------- | -------------- | ----------- | ------- | ------- | ---------- |
| small                 | ×1     | 0.8353 ±0.0237       | 1.28–2.85%     | 0.033 ms    | 0.88 ms | 1.09%   | **153 KB** |
| **small（随仓库提供）**      | **×2** | **0.8443 ±0.0093**   | 1.65–2.40%     | 0.034 ms    | 0.88 ms | 1.10%   | **153 KB** |
| small                 | ×3     | 0.8268 ±0.0257       | 0.75–1.35%     | 0.033 ms    | 0.91 ms | 1.14%   | **153 KB** |
| medium                | ×1     | 0.8446 ±0.0099       | 1.73–2.18%     | 0.070 ms    | 0.93 ms | 1.16%   | 923 KB     |
| medium                | ×2     | 0.8322（1 种子）         | 1.28%          | 0.070 ms    | 0.91 ms | 1.14%   | 923 KB     |
| **medium（当前部署）**      | **×3** | **0.8454 ±0.0010**   | 1.13–1.58%     | 0.070 ms    | 0.92 ms | 1.15%   | 923 KB     |
| large                 | ×1     | 0.8561 ±0.0071       | 1.58–2.55%     | 0.172 ms    | 1.02 ms | 1.28%   | 3847 KB    |
| **large**             | **×2** | **0.8629 ±0.0021**   | 1.50–1.88%     | 0.173 ms    | 1.02 ms | 1.29%   | 3847 KB    |
| **large**             | **×3** | **0.8549 ±0.0047**   | **1.05–1.20%** | 0.171 ms    | 1.01 ms | 1.26%   | 3847 KB    |
| 上一代 `size_hn_ca_mw60` | ×1     | 0.8459               | 1.88%          | 0.075 ms    | 0.96 ms | 1.20%   | 923 KB     |

（`×N` = 硬负样本在负样本池里复制 N 份，占比 31% / 48% / 58%。全部为  
`conv_attention` + `steps=12000` + mw 50~60。）

**速度结论**：整步 0.88–1.05 ms，占 80 ms 预算的 **1.1%–1.3%** —— 三个规模都极其宽裕。  
分类头只占整步的 4%（small）/ 8%（medium）/ 17%（large），**大头是 mel+emb 这个固定成本**，  
所以 large 只比 medium 多 0.14 ms/步，真正的代价是**模型 4×**（923 KB → 3847 KB）。  
small 省的是体积（153 KB）和分类头本身（0.033 ms，medium 的一半），整步只快 0.04 ms。

**选型（已落地）**：**已部署 `medium ×3`**（±0.0010 最可复现，体积/速度与上一代完全相同 →  
零成本升级）；**同批随仓库提供 `small ×2`**（153 KB / 0.88 ms）给低算力设备。  
若还要继续压误触发 → `large ×3`（硬负 1.05%）；要 recall → `large ×2`（0.8629，代价 4× 体积）。
⚠ `small ×1` 方差最大（0.8115 vs 0.8590，差 4.8 pt）→「small 也能到 0.85」不是稳定结论。

⚠⚠ **`mw=55` 是幸运种子的教科书案例**：`mw55` 曾拿到全网格最高 **0.8776**（AUT 0.00312、  
1.44 s 档 0.768 也最好），补的第二个种子却只有 **0.8158**（差 **6.2 pt**，均值 0.8467，  
并不优于 mw60）。**任何单次结果都必须补种子后才可采纳。**

---

## 7. 空间换性能

`bench` 分解（单线程，32 kHz 无关）：

```
mel(32000 samples)   0.47 ms
emb(batch1)          0.59 ms      ← 瓶颈
mel + emb(batch16)   10.75 ms
classifier(batch1)   0.0067 ms
classifier(batch512) 0.206 ms (0.00040 ms/样本)
```

> 换成 `size_small` 后，分类头开销进一步降到 **0.0004 ms/样本**（medium 是 0.0019），  
> 在流式管线里已完全可忽略（每 80 ms 步总耗时 1.07 ms = 1.34%）。

**瓶颈是嵌入 CNN**（约 0.59 ms/窗），不是梅尔谱。参考实现每个 80 ms 步长要把  
16 个窗全部过一遍嵌入网络（10.3 ms）；而滑动窗口只移动 8 个梅尔帧，其中 15 个窗  
是上一批的「旧内容」。

`incremental: true`（默认）用**环形队列缓存最近 16 个嵌入**：只在首次填满窗口时  
批量算满 16 个窗，之后每步只算最新 1 个窗 —— 用约 6 KB 常驻内存换来  
**约 8.7× 的推理加速**（9.1 ms → 1.05 ms 每步）。

代价：旧嵌入是用「相邻窗口的梅尔归一化基准」算出来的，与「每步全部重算」相比存在  
微小数值偏差（实测 max|Δscore| ≤ 3.1e-2，158 步内 0 次阈值翻转）。要求绝对确定性时把  
`incremental` 设为 `false`。

第二级「空间换性能」是**梅尔谱增量**（`incremental_mel`，默认 on）：每步只把新出现的  
8 帧音频过 mel 模型，mel 成本从 0.47 ms 降到 0.05 ms，整步 0.99 → 0.62 ms。  
原理、边界条件与等价性实测见 **§9.5**。

评估工具侧也用「空间换性能」：`features` 模式把全部 33204×16×96 特征（约 200 MB）  
一次性载入内存并按 512 批量推理，分类头摊到 **0.0019 ms/样本**。

---

## 8. 已知约束

1. **梅尔谱输入长度必须恒定**。`melspectrogram.onnx` 内部用 `ReduceMax` 做  
   `power_to_db` 的 top_db 截底，长度变化会改变截底基准；喂明显更短的缓冲（实测 < 约  
   22000 采样）会让输出退化成静音底（全 `-8`）。因此**参考路径**必须攒满  
   `window_samples` 才开始推理（这也是 openWakeWord 固定喂 16000 采样的原因）。  
   ⚠ 注意这条**只约束「把整段喂进去」的用法**：`incremental_mel`（§9.5）恰恰是靠  
   「分段喂 + 事后重贴全局底」绕开它，所以那条路径反而以短输入为常态。
2. **分类头 IR version 降级**（见第 2 节），升级 onnxruntime 后可去掉。
3. 单条测试音频必须是 2.0 s（32000 采样）才能与官方指标对齐；  
   更短的音频两者都不可靠。
4. `threads > 1` 时 onnxruntime 的浮点归约**理论上**可能不稳定（唤醒场景原建议  
   `threads: 1`）。**2026-10-13 实测与这条相悖，见 §9.2**：跨运行、跨线程配置的  
   score 序列逐位一致。该条改为「按机器实测决定」，不再默认 1。
5. **别用 `--mode audio` 在 `output/nihao_xiaozhi/positive_test` 上测 `size_small`**。  
   那批 wav 来自**脏**语料，其中约 39% 是 VoxCPM 啰嗦长音频；增强把它们裁成 2 s 窗口后，  
   唤醒词往往并不在窗口内 → 属于错误标签。实测同样 60 条 wav：  
   `size_small` recall 0.37、旧 `nihao_xiaozhi` recall 0.45，**两者都远低于各自的验证集成绩**  
   （0.818 / 0.581）。这是数据分布问题，不是移植回退。  
   判定移植是否正确请用 `--mode features`（第 6.1 节，已与 Python 逐项对齐）。

---

## 9. 还能不能再快（2026-10-13 实测）

复现入口在仓库内 `tools/livekit_wakeup_eval/`（独立 exe，**只依赖 onnxruntime，  
不依赖 Qt、不需要 moc**）：

```bash
# 构建：按同目录 CMakeLists.txt，或直接用同口径的 cl 命令行自行编译
cmake -S tools/livekit_wakeup_eval -B tools/livekit_wakeup_eval/build
cmake --build tools/livekit_wakeup_eval/build --config Release

cd tools/livekit_wakeup_eval/build
M=../../../Data/livekit/melspectrogram.onnx
E=../../../Data/livekit/embedding_model_int8_pc.onnx
C=../../../Data/livekit/size_med_hnx3_ca_mw50.onnx
./livekit_wakeup_eval --mode bench    --mel $M --emb $E --clf $C --threads 1      # §9.1 分段耗时
./livekit_wakeup_eval --mode streamlong --seconds 60 --mel $M --emb $E --clf $C  # §9.3 增量 vs 精确
./livekit_wakeup_eval --mode incmel   --seconds 60 --mel $M --emb $E --clf $C    # §9.5 等价性 A/B
```

`bench`：分阶段计时（mel/emb/clf）与 mel 长度标度。`streamlong`：逐 80 ms 推流，  
对比增量模式与精确模式的每步耗时。`incmel`：增量梅尔谱的逐值等价性 A/B（见 §9.5）。  
⚠ §9.2 的线程扫描是一次性探针（`threads=1/2/4` 各 3 遍），没有随仓库提供；  
结论本身可用 `streamlong` 改 `--threads` 复现个大概。

### 9.1 现状分解（Ryzen / MSVC Release / `window_samples=32000`）

| 项                           | threads=1        | threads=2   | threads=4   |
| --------------------------- | ---------------- | ----------- | ----------- |
| mel(32000 samples, 197 帧)   | 0.47 ms          | 0.32 ms     | 0.23 ms     |
| emb(batch1，增量模式每步 1 窗)      | 0.55 ms          | 0.44 ms     | 0.30 ms     |
| classifier(batch1)          | 0.007 ms         | 0.007 ms    | 0.008 ms    |
| **pushInt16 端到端 / 80 ms 步** | **0.99–1.03 ms** | **0.73 ms** | **0.67 ms** |
| 占实时                         | 1.24%            | 0.91%       | 0.84%       |

- 三模型 `init()`（加载 + `ORT_ENABLE_ALL` 图优化）只要 **23 ms** → 启动侧没有优化空间。
- 非推理开销（int16→float、喂料缓冲、环形队列拼接、每步的临时 vector 分配）  
  在 0.99 ms 端到端里**测不出来**（≈ mel+emb+clf 之和），不值得动。

### 9.2 线程数：文档顾虑不成立，纯配置 1.4x

```
threads=1  ms/step=0.9916  run-to-run max|diff| = 0.000e+00
threads=2  ms/step=0.7356  run-to-run max|diff| = 0.000e+00
threads=4  ms/step=0.6728  run-to-run max|diff| = 0.000e+00
[跨线程] threads=2 vs 1 : max|diff| = 0.000e+00   1.35x
[跨线程] threads=4 vs 1 : max|diff| = 0.000e+00   1.47x
```

score **逐位相同**（`fabs(float − float) == 0`，不是"小于某个 eps"）。所以  
「`threads>1` 浮点归约不稳定」在本机不成立，把 `threads` 从 1 提到 2 是**零风险**  
的 1.35x。注意这是**每台机器各自确定性**：同一台机器上跨运行/跨线程都逐位一致，  
换机器不保证逐位一致——但唤醒是阈值判定，且增量模式本身已有 2.2e-2 的偏差，  
1e-7 量级的线程差异完全无关紧要。  
⚠ 反方向的代价：ORT 的 worker 线程默认忙等（spinning），多线程会**抢占**同机的  
录音/ASR/TTS。树莓派这类少核设备建议先测再上，或配 `session.intra_op.allow_spinning=0`。

### 9.3 mel 是全线性成本，但每步只新增 8 帧 —— 已实现增量 mel（1.6x）

```
mel(1280 smp = 0.08s)   0.0476 ms   frames=5
mel(2560 smp = 0.16s)   0.0499 ms   frames=13
mel(6400 smp = 0.40s)   0.1176 ms   frames=37
mel(16000 smp = 1.00s)  0.2533 ms   frames=97
mel(31360 smp = 1.96s)  0.4734 ms   frames=193
mel(32000 smp = 2.00s)  0.4651 ms   frames=197
```

**≈2.4 µs/帧，几乎纯线性**（1280 采样的固定开销只有 ~0.035 ms）。而流式每步只推进  
8 个梅尔帧 —— 原来却把 197 帧整段重算（0.47 ms 里约 **0.44 ms 是纯冗余**）。

**已实现**（`Params::incrementalMel`，默认 on，见 §9.5）：每步只把新出现的 8 帧音频  
（1632 采样）喂给 mel 模型，再对环形缓冲重贴一次全局底。实测：

| 模式                               | ms/步          | 占 80 ms        |
| -------------------------------- | ------------- | -------------- |
| `incremental_mel: false`（每步整窗重算） | 0.99–1.06     | 1.23–1.32%     |
| `incremental_mel: true`（默认）      | **0.62–0.65** | **0.77–0.81%** |

（`--mode incmel` 三种信号实测 **1.55–1.67x**；绝对耗时随机器/信号在 ±5% 内浮动。）

⚠ **别指望缩窗**：`window_samples=32000` 是**硬下限**。32000 → 197 帧，  
16 个嵌入需要 76 + 15×8 = **196 帧**；`frames(31360)=193` 只够 15 个窗，  
分类器直接不跑。2.0 s 已经是最小值。

### 9.4 其余候选（未实测收益，按性价比排）

1. **emb CNN int8 量化 —— 已实测（2026-10-13）**。`embedding_model.onnx` 是  
   `Conv×20 + LeakyRelu×19 + Max×19 + MaxPool×5`，**动态量化只覆盖 MatMul/Gemm，  
   对纯 CNN 没用**；实际用的是 **static QDQ**（`quantize_static`，per-channel int8 权重 +  
   uint8 激活），校准集 = 训练仓库 300 条真实音频的 mel 窗（positive/negative/background  
   train 各 120/120/60 条）。产出 `embedding_model_int8_pc.onnx`（**417 KB**，fp32 1296 KB），  
   opset 13，仓库自带 onnxruntime（API 17）可直接加载，**无需再降 ir_version**。
   - **速度**（本仓库 `--mode incmel`，`threads=1`，60 s burst 信号）：  
     整步 0.633 → **0.464 ms（1.37x）**，占 80 ms 的 0.79% → 0.58%；  
     单窗 emb 0.577 → 0.391 ms（**1.48x**）。`threads=2` 时整步 0.476 → 0.380 ms。  
     流式一致性：int8 下「增量 vs 精确」的 max|Δ| 从 1.6e-3 变 2.9e-2（与 fp32 已接受的  
     增量近似同一量级），726 步 **0 次阈值翻转**。
   - **准确率**（训练仓库 `scripts_quant_eval.py`：clean-corpus 分类头 + 真实测试集，  
     FP32 重抽取与存档 .npy 逐位一致 max|diff|=0 作 sanity）：
     | 口径                                                                                 | fp32          | int8          | Δ                |
     | ---------------------------------------------------------------------------------- | ------------- | ------------- | ---------------- |
     | test（1.73 h 负样本）recall@0.5 / FPPH                                                  | 0.8328 / 6.92 | 0.8279 / 5.77 | −0.49 pt / −1.15 |
     | test recall@FPPH≤1                                                                 | 0.6721        | 0.6519        | **−2.02 pt**     |
     | union（10.4 h，含训练集）recall@0.5 / FPPH                                                | 0.8507 / 4.04 | 0.8419 / 3.37 | −0.88 pt / −0.67 |
     | union recall@FPPH≤1                                                                | 0.7612        | 0.7513        | −0.99 pt         |
     | AUT 0.01244 → 0.01307（test）/ 0.00889 → 0.00965（union）。嵌入余弦相似度                      |               |               |                  |
     | mean 0.996 / p01 0.974。DET 对比图见 `doc/livekit_wakeup_results/int8_vs_fp32_det.png`。 |               |               |                  |
   - **结论**：≈**0.5 pt recall@0.5（最坏口径 2 pt recall@FPPH≤1）换 1.37x 整步 + 3.1x 体积**。  
     值不值得换默认值是产品取舍；换法是 `livekit_wakeup.emb: embedding_model_int8_pc.onnx`  
     （无需改代码），不换也不影响其他优化。
   - 复现：训练仓库（`Worktrees/livekit-wakeword/main-d751d36b`）  
     `scripts_quant_embedding.py`（量化 + 保真度）→ `scripts_quant_eval.py`（准确率 + DET）；  
     结果归档 `doc/livekit_wakeup_results/{int8_accuracy_report,embedding_quant_fidelity}.json`。
2. **能量门控** —— `Wakeup` 的 `WAKEUP` 态会对**每个** 80 ms 块跑完整三段。  
   对 RMS 低于阈值的块直接复用上一个分数（跳过 mel+emb+clf），常驻 CPU 近乎归零，  
   零精度风险（阈值取远低于语音电平即可）。适合树莓派常驻。
3. **`session.intra_op.allow_spinning=0`** —— 降的是**空闲 CPU 占用**（ORT 默认忙等），  
   不是单步延迟，可能轻微抬高延迟。
4. **不推荐**：把步长从 80 ms 拉到 160 ms（省一半算力）—— 16 个嵌入的时间跨度  
   会从 1.96 s 变成 3.16 s，分类器的感受野变了，必须重训/重验。

剩余上限（实测，2026-10-13）：`threads=1` + 增量嵌入 + 增量 mel + **int8 emb** =  
**0.464 ms/步（0.58% of 80 ms）**，比最初的精确路径（9.2 ms）累计 **~20x**。  
此时 mel（0.05 ms）已可忽略，emb 是唯一大头；再往下只剩能量门控（候选 2）。

---

## 9.5 增量 mel 怎么做到等价（实现说明）

**结论**：不需要拆图、不需要 `onnx` 包、不需要复刻 FFT，直接对**原模型**分段调用即可，  
每步 mel 从 197 帧降到 8 帧（0.47 → 0.05 ms）。

**图结构**（`inspect_onnx.py` 解析 `melspectrogram.onnx`，29 个节点，opset 13）：

```
input ──Conv(kernel=512, stride=160, pads=0)×2──> real/imag
      ──(real²+imag²)──> MatMul(melW) ──> mel 功率谱
      ──Clip(amin)──Log──Mul(10/ln10)──Div──Sub──> db_abs        ← 逐帧局部！
db_abs ──ReduceMax(全图)──> M ；output = Clip(db_abs, M - top_db, hi)
```


两个关键事实：

1. **`db_abs` 只由本帧那 512 个采样决定**。分帧是 `Conv(kernel=512, stride=160, pads=0)`，  
   无 padding、无跨帧归一化，所以整窗调用与分段调用算出的同一帧数值相同  
   （实测差异只有 Conv/GEMM 在不同输入长度下的浮点噪声，≈1e-6 dB，偶尔 6e-4）。  
   帧数与输入长度的关系是严格线性的：`frames(n) = (n - 512) / 160 + 1`。
2. **唯一的全局量是末尾的 top_db 截底**：`output = max(db_abs, M - top_db)`，  
   `M = max(db_abs)`，`top_db = 80`（实测：正弦信号 `out.max() - out.min() = 80.0000`）。

于是可以「分段调用 + 事后重贴全局底」。设第 i 段（8 帧）算出的结果是  
`v_i = max(db, M_i - T)`，`M_i` 是该段自己的最大值。恒等式：

```
max( max(x, a), b ) == max(x, a, b)
```

只要**每段都是整个滑动窗口的子集**，就有 `M_i ≤ M_win`，即 `a ≤ b`，于是

```
max(v_i, M_win - T) == max(db, M_win - T) == 参考值
```

**这要求每段都落在窗口内** —— 所以实现上从第 1 步就开始累积帧（每步 8 帧），  
而不是等窗口攒满再整窗算一次（整窗那一发的 `M` 之后可能高于当前窗口的 `M`，  
那个洞正好被这条不变量堵住）。环形缓冲容量 = 窗口帧数 197，帧随窗口滑出即弃。

**实测等价性**（`--mode incmel --seconds 60 --threads 1`，基准 = 每步整窗重算  
mel + 重算 16 个窗嵌入）：

| 信号                   | 仅增量嵌入（原默认） | 仅增量 mel   | 增量嵌入 + 增量 mel |
| -------------------- | ---------- | --------- | ------------- |
| sweep（平稳）            | 0.000e+00  | 2.086e-07 | 2.086e-07     |
| burst（1 s 吵 / 1 s 静） | 1.573e-03  | 1.800e-04 | 1.572e-03     |
| ramp（−60 dB 斜坡）      | 0.000e+00  | 1.490e-07 | 1.490e-07     |

（表内为每种组合 726 步的 max|Δscore|，全部 **0 次阈值翻转**。`burst` 走的是最狠的  
路径：窗口最大值剧烈起伏、大量梅尔格被顶在截断底上。）

**要点**：增量 mel 自己引入的偏差（≤1.8e-04）比**现已在用的**增量嵌入（1.6e-03）  
还小约一个数量级；两者叠加时被后者吞没。要绝对确定性时  
`incremental_mel: false` + `incremental: false` 可回到逐位一致。

⚠ `mel_top_db`（默认 80）只影响「低于窗口最大值 −80 dB」的静音格，换 mel 模型时  
需要重新测定。测法：把一段幅度恒定的正弦喂给 `--mode melprobe`，打印  
`mel.max() - mel.min()`，读数就是 `top_db`（`.buildtmp/livekit_probe/probe_topdb.py`  
是等价的 Python 版本）。

---

## 10. 误触发治理：近音 / 含唤醒词短语（2026-10-14）

用户报告「你好吗小智」「你好小智呀」也会被识别，目标 **FPPH < 1 且 recall > 0.82**。  
完整过程与结果见训练仓库 `output/hn/RESULTS.md`；脚本都在训练仓库根目录。

### 10.1 根因

`configs/nihao_xiaozhi.yaml` 的负样本设计原则**刻意排除**了「与唤醒词几乎同音、或完整  
包含唤醒词」的短语（原话：这类样本与正样本标签冲突，会教模型连真正的「你好小智」一起压制）。  
所以模型从没见过「你好吗小智 / 你好小智呀」→ 必然误触发。

### 10.2 ⚠⚠ VoxCPM 会把文本「纠正」成唤醒词

`尼好小智` 有 **86%**、`你号小智` 76%、`你好小芝` 72% 的概率被 TTS 直接念成「你好小智」。  
这类「负样本」其实是**错标正样本 = 毒数据**。

⇒ 所有硬负样本**必须先过 ASR 质检**再进训练集。用项目里已验证的 FunASR Paraformer-q8  
探针（`.buildtmp/funasr_probe/obj/funasr_probe.exe`，批处理模式一次加载跑完 3000+ 条）。

质检规则必须**逐短语**设计，且 ASR 自己会把「智」写成「志/之/芝」，比较前要同音归一化：

| 组           | 短语                        | 规则                         | 丢弃率    |
| ----------- | ------------------------- | -------------------------- | ------ |
| A 含唤醒词+额外成分 | 你好吗小智、你好小智呀、是小智吗…         | 转写==「你好小智」→ 丢（TTS 吞掉了额外成分） | **0%** |
| B 近音/末字替换   | 尼好小智、你号小智、你好小芝、您好小智、你好小之  | 转写含「小智/小志」→ 丢              | 36–86% |
| B 末字完全不同    | 你好小紫/小纸/小指、嘿小智、小智、小智呀、你好小 | 同上                         | 0–9%   |

结果：train 2600 条保留 2272（87.4%），test 520 条保留 444。**没有这道质检，  
训练集里就是 328 条毒数据。**

### 10.3 效果

| 短语                                    | baseline `size_small` | `size_hn_mw100` | `size_hn_ca_mw60` | **当前 `size_med_hnx3_ca_mw50`** |
| ------------------------------------- | --------------------- | --------------- | ----------------- | ------------------------------ |
| 你好吗小智（你点名）                            | 0.07                  | 0.02            | 0.02              | **0.00**                       |
| 你好小智呀（你点名）                            | 0.05                  | 0.03            | 0.05              | 0.05                           |
| 嘿小智                                   | 0.12                  | 0.03            | 0.02              | **0.00**                       |
| 你好呀小智                                 | 0.13                  | 0.05            | 0.10              | **0.08**                       |
| 您好小智                                  | —                     | —               | 0.08              | **0.06**                       |
| 你好小                                   | 0.12                  | 0.05            | 0.02              | 0.03                           |
| 呼叫小智 / 小智在吗 / 小智小智 / 你好小智机器人 / 你好小智同学 | 0.02–0.03             | 0.00            | 0.00–0.03         | **0.00**                       |
| **合计近音误报率 @工作点**                      | **3.75%**             | 2.70%           | 1.88%             | **1.13%**                      |

聚合指标见 §6.3（误报率已改为按**部署工作点阈值**统计，而不是固定 0.5 —— 只看 @0.5  
会把「阈值低所以近音误报高」错当成模型缺陷）。

### 10.4 已达标（2026-10-14 更新）

**先是 `size_hn_ca_mw60.onnx` 首次同时满足 `FPPH<1 且 recall>0.82`**：  
`recall@FPPH≤1 = 0.8459`、`FPPH = 0.9758`、`AUT = 0.0040`。

起作用的不是「再加负样本」，而是两个此前从未试过的轴（详见 §6.4）：

1. **`model_type: conv_attention`**（+8.8pt，AUT 减半以上）—— 真正的主因；
2. `mw`（负样本权重）细粒度扫描：60 附近是最优工作点。

原「数据问题，需要真人录音」的判断**要修正**：那是建立在 `dnn` 上的结论。  
在 dnn 上确实无解（第一轮 19 个模型、阈值全扫描），换架构后有 2.6pt 的净余量。

**第六~九轮又把 `hardneg_mult` 这条新轴吃干**（详见 §10.5），最终部署  
**`size_med_hnx3_ca_mw50.onnx`**：`recall@FPPH≤1 = 0.8443`、`FPPH = 0.9220`、  
`AUT = 0.00345`（四代最佳），工作点近音误报率 **1.13%**（上一代 1.88%），两颗种子只差  
**0.21pt**（全网格最稳），而体积与单步开销与上一代**完全相同** → 零成本升级。  
同批随仓库提供 153 KB 的 `size_small_hnx2_ca_mw50.onnx`（`0.8536 / 0.7589`）给低算力设备。

全网格 `r@FPPH≤1` 的**单次最好**是 `large ×2` 的 `g_large_hnx2_mw60_hnx2b`（**0.8650**），  
其次 `large ×1` 的 `g_large_hn_mw60_ca12k_x1`（0.8568）—— 但都要 **4× 体积**（3.8 MB vs  
0.92 MB），且 `large ×3` 虽把硬负压到 1.05% 却只剩 0.8549 recall（均值 0.8549 ±0.0047，  
并不比 medium×3 的 0.8454 ±0.0010 稳）→ 为 +0.2~2.1pt 付 4 倍常驻内存，对树莓派不划算，  
故不采用（见 §6.6 的速度/体积结论）。  
真人录音仍是提升上限的正确方向（1.44 s 档和 TTS 音色覆盖仍有瓶颈，见 §10.5）。

### 10.5 仍有瓶颈：模型对长句打低分（时长先验）

把测试正样本按**源音频时长**分档（VoxCPM 输出量化为 0.80/0.96/1.12/1.28/1.44 s 五档）：

| 时长档        | n clips | `size_hn_mw100` | 三代 `ca_mw60` | **当前 `hnx3_mw50`** | `large/ca12k_x1` | 语速扰动 0.85×/1.18× |
| ---------- | ------- | --------------- | ------------ | ------------------ | ---------------- | ---------------- |
| 0.80 s     | 59      | 0.853           | 0.893        | 0.887              | 0.898            | 0.859            |
| 0.96 s     | 158     | 0.916           | **0.939**    | 0.928              | 0.932            | 0.882            |
| 1.12 s     | 172     | 0.851           | 0.864        | 0.882              | 0.874            | 0.837            |
| 1.28 s     | 119     | 0.790           | 0.843        | 0.829              | 0.829            | 0.801            |
| **1.44 s** | 102     | **0.592**       | **0.647**    | 0.644              | **0.719**        | 0.706            |

⚠ 当前部署款在这个瓶颈上**没有改进**（1.44 s 档 0.644 vs 三代 0.647）—— `hardneg_mult`  
压的是近音误报，动不了时长先验。上表数值为各档**最优阈值**下的 recall。

三个关键证据说明这不是「听不出来」，而是**打分尺度偏移**：

- 阈值降到 0.05 时 1.44 s 档 recall 恢复到 **0.997**（其它档 0.99+）；
- **训练集与测试集**的「时长-召回」曲线几乎重合（训练集 1.44 s 档也只有 0.666）  
  → 不是泛化问题，是模型在该档**欠拟合/带先验**；
- ASR 质检显示 1.3–1.5 s 档 94.8% 确实念了唤醒词 → 不是标签噪声。

**已证伪的三条路**（见 §6.5）：长样本过采样（×3 → 0.7896）、滑窗 ASR 定位裁剪（0.7962）、  
语速扰动（0.8219/0.8098，对照 0.8459）。三者都只改善 1.44 s 档，却牺牲其余档与 AUT。

**第六~九轮：`hardneg_mult` 是有效的新轴**（此前只调过 `mw`）：

| 轮 | 内容                                                 | 结论                                                         |
| - | -------------------------------------------------- | ---------------------------------------------------------- |
| 6 | `hardneg_mult = 2/3`；large/ca12k mw 扫描；steps 20000 | ×3 把硬负误报压到 1.05% 且 recall 0.8503；steps 20000（0.8432）**过头** |
| 7 | 给 hnx3/hnx2 补第二种子；large/mw55 补第一种子                 | hnx3/hnx2 确认；**mw55 被证伪为幸运种子**（0.8776 vs 0.8158，差 6.2 pt）  |
| 8 | medium/hnx3、medium/hnx2 补种子                        | medium/hnx3 **0.8454 ±0.0010**，全矩阵最稳                       |
| 9 | **small + conv_attention** × mult 1/2/3            | small 象限首次可用（×2 = 0.8443，153 KB）                           |

完整矩阵（recall / FPPH / 硬负 / 速度 / 体积）见 **§6.6**。

⚠⚠ **尺子也要修**：硬负测试集只有 444 clip，`尼好小智`/`你号小智` 分别只剩 1/2 条  
（VoxCPM 有 76~86% 概率把这两个短语念成唤醒词，**生不出**干净负样本）→ 这两个短语  
要么真人录音补，要么从指标里剔除；其余短语每档也应加到 ≥60 条再谈逐短语结论。  
⚠ 另外 `stage_train` 不会重组语料：`--hardneg-mult` 必须在 **`--stage corpus`** 阶段  
生效并落到独立 `--dst`，train 阶段还要传同样的值，否则会出现「模型名叫 hnx2、语料却是 mult=1」。

### 10.6 复现

```bash
R=<livekit-wakeword 训练仓库>
cd "$R"
python scripts_gen_hardneg.py --n-train 2600 --n-test 520 --shard 0 --shards 2   # 生成（2 分片）
python scripts_asr_qc.py            # ASR 质检 -> keep_hardneg_{train,test}.json
python scripts_hn_corpus.py --stage features   # 硬负样本增强 + 抽特征
python scripts_hn_corpus.py --stage corpus     # 组装 output/size_hn/
python scripts_hn_corpus.py --stage pos-speed  # 语速扰动正样本特征（§10.5，已证伪）
python scripts_hn_corpus.py --stage train --size medium --mw 60 \
    --model-type conv_attention --tag s1       # 第三代部署配置（ca_mw60）
# 硬负样本上采样（第六~九轮，必须 corpus 阶段生效并落独立 dst）：
python scripts_hn_corpus.py --stage corpus --hardneg-mult 3 --dst output/size_hn_hnx3
python scripts_hn_corpus.py --stage train --dst output/size_hn_hnx3 --hardneg-mult 3 \
    --size medium --mw 50 --model-type conv_attention --steps 12000 --tag hnx3m2
    # ↑ 当前部署：output/g_medium_hnx3_mw50_hnx3m2/  ->  Data/livekit/size_med_hnx3_ca_mw50.onnx
python scripts_hn_corpus.py --stage train --dst output/size_hn_hnx2 --hardneg-mult 2 \
    --size small --mw 50 --model-type conv_attention --steps 12000 --tag sca_m2b
    # ↑ 随仓库提供的小备选：  ->  Data/livekit/size_small_hnx2_ca_mw50.onnx
bash scripts_hn_round6.sh                      # 第六轮整批（两路并行）
bash scripts_hn_round9.sh                      # 第九轮：small + conv_attention 象限
python scripts_eval_all.py                     # 全部模型统一评估 + 达标标记
python scripts_eval_matrix.py                  # 规模 × 上采样 × 速度/体积 全量矩阵 -> output/hn/MATRIX.md
python scripts_pareto.py                       # recall vs 近音误报 Pareto 图
python scripts_pos_diag.py --clf <onnx>        # 按音色/时长/cfg 拆正样本 recall
python scripts_patch_ir.py <onnx>              # ir_version 10 -> 9
```

⚠ **复现当前部署款时 `--hardneg-mult` 必须在 `--stage corpus` 就生效**并落到独立 `--dst`，  
train 阶段再传同一个值（`stage_train` 只从 `--dst` 拷 `.npy`，不会重组语料）。  
⚠ 导出的 ONNX 是 `ir_version = 10`，本仓库 `lib/onnxruntime`（1.17.1）只支持 IR ≤ 9，  
入库前要用 `scripts_patch_ir.py`（训练仓库，等价于本仓库  
`scripts/prepare_livekit_models.py::patch_ir_version`）降级（不改图）。  
⚠⚠ 切模型后**运行期数据根 `~/.config/QSmartAssistant/Data/` 也要手工同步**  
（`file(COPY Data/)` 只在构建时覆盖，不构建就不生效）。

⚠⚠ 训练没有 seed 控制，run-to-run 方差是本仓库最大的方法学陷阱：同配置两个种子可以差  
0.8~8.9pt（`mw55` 甚至 6.2pt）。**选型必须多跑种子**，按  
`AUT + r@FPPH≤1 + 近音误报@工作点` 三件套选，不要凭单次结果。
