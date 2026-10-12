# livekit-wakeword 唤醒引擎（你好小智）

把自训练的 [livekit-wakeword](https://github.com/livekit/livekit-wakeword) 唤醒词模型
接入 QSmartAssistant 的 `Wakeup` 模块。当前接入的唤醒词是**「你好小智」**
（分类头 `size_small.onnx`，DNN **small** 规模，干净语料训练）。

---

## 1. 推理管线

livekit-wakeword 的推理是**三段 ONNX 前向**，与原版 openWakeWord 同源：

| 阶段 | 模型 | 输入 | 输出 |
|---|---|---|---|
| 1. 梅尔谱 | `melspectrogram.onnx` | `(1, n)` float32，16 kHz 原始音频 | `(1, 1, F, 32)` dB 谱，再按 `x/10 + 2` 归一化 |
| 2. 语音嵌入 | `embedding_model.onnx` | `(1, 76, 32, 1)` 梅尔窗 | `(1, 1, 1, 96)` 嵌入 |
| 3. 分类头 | `size_small.onnx` | `(N, 16, 96)` 嵌入序列 | `(N, 1)` 置信度 ∈ [0,1] |

- 梅尔窗：**76 帧窗、步长 8 帧**；
- 分类器输入 = **最近 16 个嵌入**（2.0 s 音频恰好 16 个）；
- 正样本在数据侧对齐到窗口**末尾**，因此滑窗「右端对齐唤醒词」时得分最高。

C++ 实现（Qt 无关）位于 `Wakeup/Livekit/livekitwakeword.{h,cpp}`；
Qt 适配层 `Wakeup/Wakeup/livekitwakeup.{h,cpp}` 继承 `WakeupModel`。

---

## 2. 准备模型

模型不从仓库自带，用脚本从 livekit-wakeword 训练产物收集：

```bash
python scripts/prepare_livekit_models.py \
    --src  <livekit-wakeword 仓库或 worktree 根目录> \
    --dst  Data/livekit \
    --wakeword size_small
```

产出 `Data/livekit/`：

```
melspectrogram.onnx    # 来自 livekit-wakeword/src/livekit/wakeword/resources/
embedding_model.onnx
size_small.onnx        # 来自 output/size_small/
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
    "model": "size_small.onnx",  // 分类头（small 规模，50k 参数 / 217 KB）
    "mel": "melspectrogram.onnx",
    "emb": "embedding_model.onnx",
    "threshold": 0.55,           // 判定阈值（该模型的官方最优工作点）
    "trigger_level": 2,            // 连续命中步数（每步 80 ms）
    "refractory": 25,              // 触发后静默步数（约 2 s，防重复唤醒）
    "chunk_bytes": 2560,           // 每次送入 2560 字节 = 1280 采样 = 80 ms
    "window_samples": 32000,       // 滑窗长度，2.0 s，**不要改**
    "threads": 1,                  // onnxruntime intra-op 线程
    "incremental": true            // 空间换性能开关，见第 6 节
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

| 模式 | 作用 |
|---|---|
| `features` | 读 livekit 的 `.npy` 特征集，只跑分类头，复算官方指标（与 Python `*_eval.json` 对齐） |
| `verify` | 用 C++ 完整音频管线抽特征，与 `.npy` 逐元素比对，验证移植无损 |
| `audio` | 对测试集 wav 跑完整管线（mel→emb→分类头）打分并算指标 |
| `streamlong` | 拼接长音频逐步推进，校验流式稳态一致性 + 每步耗时 |
| `bench` | 各段 ONNX 推理耗时分解 |
| `melprobe` / `streamprobe` | 诊断用（梅尔谱长度相关性 / 逐窗对齐） |

公共参数：`--mel --emb --clf`（三个模型路径）、`--threads`。

---

## 6. 验证结果

> **模型代际说明**：训练仓库里「你好小智」有两个代际，指标**不可直接比较**（验证集不同）：
> - `nihao_xiaozhi`（medium，全量 5000 条脏语料）在 **3000 条脏验证集**上评；
> - `size_small` / `nihao_xiaozhi_clean`（干净语料，`0.7s ≤ 时长 < 1.5s`）在
>   **1830 条干净验证集**上评。脏语料里混了大量 VoxCPM「啰嗦」长音频（>4s 召回仅 0.024），
>   属于标签噪声。同口径（干净集）下脏→干净是 0.744 → 0.833。
>
> **当前部署的是 `size_small`**：small 规模在 3 种子重复实验中与 medium/large
> 统计上无差别（@FPPH≤1 召回 0.811±0.010 / 0.815±0.002 / 0.801±0.010），
> 但体积只有 medium 的 1/4、large 的 1/11。

### 6.1 移植无损性

- **特征级**（`verify`，200 条正样本，1536 维/条）：
  `max|C++ − Python npy| = 2.3e-05`，`mean = 3.2e-06` —— float32 舍入量级。
  （mel / emb 两段与模型代际无关，此结论对 `size_small` 同样成立。）
- **分类头指标**（`features`，1830 正 / 33204 负，18.45 h）与 Python
  `output/size_small/size_small_eval.json` **逐项完全一致**：

| 指标 | C++ | Python |
|---|---|---|
| recall @0.5 | 0.8180 | 0.8180327868852459 |
| accuracy @0.5 | 0.9085 | 0.9084592316548865 |
| FPPH @0.5 | 2.0058 | 2.0057824358511023 |
| AUT | 0.0094 | 0.009361116212873163 |
| 最优阈值 | 0.55 | 0.55 |
| 最优 recall / FPPH | 0.7934 / 0.9216 | 0.7934426229508197 / 0.9215757137694255 |
| 混淆矩阵 @0.5 | tp=1497 fn=333 fp=37 tn=33167 | — |

- 该模型在 **FPPH ≤ 1** 的工作点上召回 **0.7934 / FPPH 0.9216**，
  相比上一代 `nihao_xiaozhi` 的 0.6070 / 0.8674（同为 FPPH≤1 口径）提升约 **+18.6pt**。

### 6.2 流式（实际部署路径）

`streamlong`：40 s 连续音频、500 个 80 ms 步长、抽检 119 步（`size_small` 分类头）。

| 模式 | 与无状态参考 | 阈值翻转 | 每步耗时 | 占 80 ms |
|---|---|---|---|---|
| 精确（每步重算 16 个窗） | **逐位一致 (0.0)** | 0/119 | 9.10 ms | 11.4% |
| 增量（每步只算 1 个窗） | max\|Δ\| ≤ 2.2e-2 | **0/119** | 1.05 ms | 1.3% |

原始指标 JSON 归档在 `doc/livekit_wakeup_results/`
（`size_small_features_metrics.json` 为当前部署模型的指标；
`features_metrics.json`、`audio_metrics.json` 为上一代 `nihao_xiaozhi` 的留档）。

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

评估工具侧也用「空间换性能」：`features` 模式把全部 33204×16×96 特征（约 200 MB）
一次性载入内存并按 512 批量推理，分类头摊到 **0.0019 ms/样本**。

---

## 8. 已知约束

1. **梅尔谱输入长度必须恒定**。`melspectrogram.onnx` 内部用 `ReduceMax` 做
   `power_to_db` 归一化，长度变化会改变归一化基准；喂明显更短的缓冲（实测 < 约
   22000 采样）会让输出退化成静音底（全 `-8`）。因此流式实现**必须攒满
   `window_samples` 才开始推理**（这也是 openWakeWord 固定喂 16000 采样的原因）。
2. **分类头 IR version 降级**（见第 2 节），升级 onnxruntime 后可去掉。
3. 单条测试音频必须是 2.0 s（32000 采样）才能与官方指标对齐；
   更短的音频两者都不可靠。
4. `threads > 1` 时 onnxruntime 的浮点归约可能不稳定；唤醒场景建议 `threads: 1`。
5. **别用 `--mode audio` 在 `output/nihao_xiaozhi/positive_test` 上测 `size_small`**。
   那批 wav 来自**脏**语料，其中约 39% 是 VoxCPM 啰嗦长音频；增强把它们裁成 2 s 窗口后，
   唤醒词往往并不在窗口内 → 属于错误标签。实测同样 60 条 wav：
   `size_small` recall 0.37、旧 `nihao_xiaozhi` recall 0.45，**两者都远低于各自的验证集成绩**
   （0.818 / 0.581）。这是数据分布问题，不是移植回退。
   判定移植是否正确请用 `--mode features`（第 6.1 节，已与 Python 逐项对齐）。
