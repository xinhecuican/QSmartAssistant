# rasa NLU vs 微调 LLM —— 同口径对照评测

> **第二版**：第一版（`git` 里那份 40.00% 的对照）测的是**未改动**的 rasa。
> 这一版把 rasa 侧的四层修改全部做完、并把评测本身的两个坑修掉之后重新对比。

**结论先说（三条，按重要性排序）**

1. **rasa 的 40% exact 里有 15.4 pt 是一个纯粹的软件 bug，和模型能力无关。**
   受控 A/B（同一份 config、同一份语料、同一个随机种子，**只换那两个自定义抽取器的代码**）：
   exact **41.54% → 56.92%**，意图准确率**完全不变**，`应带/实际槽位` 从 25/33 收敛到 25/18。
   两个组件在把「换一首」的「一」抽成 `n=1`，还把 DIET 已经抽对的结果**删掉**。
   修完之后，四种错误类型里的 **E1 伪数字 15 → 0、E2 伪日期 3 → 1**。

2. **改完之后的 rasa，意图分类已经不输 LLM，而且泛化到未见实体时反超。**
   - 65 条手写留出集：意图 **100.00% vs 98.46%**（rasa 反超），exact 90.77% vs 93.85%（差 **3.08 pt**）
   - 19 条未见实体探针：意图 **100.00% vs 100.00%**，exact **94.74% vs 63.16%**（rasa **反超 31.58 pt**）
   - 延迟：P50 **34–42 ms vs 200 ms**、P90 **37–46 ms vs 395 ms**，rasa 快 **≈5.5–9.6×**；
     体积 28 MB vs ~4 GB

   也就是说，**上一版"rasa 差 LLM 53.85 pt"这个判断，绝大部分是评测与配置的坑，不是模型能力的差距**。

3. **⚠ 上一版报告里所有 rasa 的对照数字都缺一个前提：随机种子。**
   rasa 3.6.19 的 `rasa train nlu` **没有** `--random-seed` 参数，而 DIET 在 `random_seed` 未配置时
   用的是 `int(time.time())`（`rasa/utils/tensorflow/models.py:92`）。
   **同一份 `nlu.yml` + 同一份 `config.yml` 重训两次，65 条留出集上 exact 差 3 条（±4.6 pt）、意图差 0–3 条。**
   显式写 `random_seed: 42` 之后，重训逐项完全一致。
   → 本报告里**控了种子的对照才当因果**，没控种子的阶梯只当趋势看。

- 评测日期：2026-10-10
- 硬件：AMD Radeon RX 9070 XT（gfx1201 / 16 GB），torch 2.13.0+rocm10.0.0，Python 3.12
- rasa 侧：Python 3.10、rasa 3.6.19、TF/CPU、chinese_rbtl3 + jieba + DIET
- 数据：`tools/nlu_llm_finetune/data/sft_holdout.jsonl`（**65 条手写未见句**，16 意图）
  与 `tools/nlu_llm_finetune/data/probe_unseen.jsonl`（**19 条未见实体探针**）

---

## 0. 这一版修正了上一版的哪些结论

| 上一版的说法 | 这一版的事实 | 为什么变 |
|---|---|---|
| rasa 意图 67.69%，差 LLM 30.8 pt | 最终 **100.00%**，反超 LLM 1.54 pt | 语料从 132 → 358 条，且修掉 16 处会被静默丢弃的实体标注 |
| rasa exact 40.00%，差 LLM 53.85 pt | 最终 **90.77%**，差 **3.08 pt** | 规则抽取器 + 语料 + 词典 + trend 语义，四层叠加 |
| 「rasa 的瓶颈是标注量，不是模型容量」 | 只对了一半：**瓶颈是分词/跨度边界 + 标注量**，不是先验知识 | 见 §6 探针：rasa 94.74% 反超 LLM 63.16% |
| 「rasa 想追平得补到 ~600 条例句，成本超过微调」 | 358 条就到 90.77% 了，**补数据的边际收益在 350 条附近已见顶** | 剩下的 6 条错例全是实体跨度问题，再加同类句子没用 |
| 自定义词典已生效（第一版没检查） | **从来没生效过**：config 里拼成了 `dicionary_path` | 组件读 `config["dictionary_path"]`，拼错即静默不加载 |
| LLM 侧 exact 93.85% 是"全面碾压" | LLM 侧数字不变，但**探针集上被 rasa 反超 31.58 pt** | 探针集是本轮新增的评测集（见 §6） |

### 0.1 第二轮修正（时间槽归一化那一轮，见 `doc/time_slot_normalization.md`）

| 上一轮的说法 | 现在的事实 | 为什么变 |
|---|---|---|
| §6「探针集上 rasa 反超 LLM 31.58 pt」 | **作废**：修好后 **rasa 100% / LLM 100%，打平** | `make_probe.py` 硬编码了一份 2026-10 之前的 system prompt，与训练时的 prompt **不是同一份**（缺延时提示、码表格式也不同）。LLM 拿一份没见过的提示词答题，输出跑成了 `f=book,l=shrine` 这种离谱值。改成运行时 `bd.build_prompt()` 生成后，两边**双双 19/19** |
| rasa exact 90.77%（v1） | **92.31%**（v2，+14 条延时语料） | 补语料的副作用是正的 |
| LLM exact 93.85%（v1） | **92.31%**（v1 在同一份**新** prompt 上的实测） | 那个 93.85% 是在**旧 prompt** 的留出集上量的，与现在不同口径。同一口径下 v1=92.31%、v2=90.77%，**差 1 条样本** |
| 没提过延时/定时能力 | 新增专项探针：rasa **18.18% → 100%**，LLM **45.45% → 90.91%** | `30分钟后开启热水器` 这类说法此前**语料里根本不存在** |
| —— | ⚠ 发现两处**尺子缺陷**（1 处已修、1 处仅存证） | ① 探针 prompt 硬编码（已修，见上）；② `rasa_to_dsl` 同码取第一个（`--prefer-synonym` **默认关**，见 §5.3） |
| —— | ⚠ 延迟数字此前是**跨口径**比较 | v1 的 `hf_metrics.json` 记的是 `timing=e2e`，v2 记的是 `timing=gen`。已补跑 v1 的 `gen` 口径 |
| —— | ⚠ 留出集上 **≤1 条样本的差异不作结论** | 65 条集上 1 条 = 1.54 pt；实测同一 adapter 在**同一口径下**复跑 2~3 次结果完全一致，但换口径/换 prompt 会动 1 条 |

---

## 1. 评测口径

### 1.1 两个后端共用同一把尺子

rasa 自带的 `rasa test nlu` 报的是它自己的口径（intent/entity 分开、词级 entity F1、夹带
cross-validation），和 LLM 侧的"整行 DSL 完全一致率"不是一回事。所以指标实现只有一个：

```
tools/nlu_llm_finetune/
├── eval_lora.py        ← report() 在这里：意图准确率 / exact / 槽位 P-R-F1 / 延迟分位
├── eval_rasa.py        ← 用 Agent.load() 跑 rasa，输出映射回 DSL 后交给同一个 report()
├── train_rasa.py       ← 训练（见 §4，必须钉 random_seed）
├── ab_extractor_fix.py ← 受控 A/B：只换抽取器代码（见 §3）
├── analyze_errors.py   ← 错例归因 E1 伪数字 / E2 伪日期 / E3 意图错 / E4 实体漏抽
├── check_entity_alignment.py ← 复现 rasa 的 jieba 分词，揪出跨边界的实体标注
└── summarize.py        ← 把 output/*.json 汇成对照表（本报告的表格都是它生成的）
```

`eval_rasa.py` 只做「rasa 输出 → DSL」的映射，其中两处必须对齐，否则 rasa 会被白扣分：

- **实体名/角色 → 单字母码**：`domain.yml` 里是 `human`(role `singer`) /
  `location`(role `home`|`city`)，schema 里是 `g` / `l` / `c`。
- **`t` 归一化**：schema 规定 trend 只有 `大`/`小`，而 rasa 标的是原样跨度
  （增大/减小/调大/调小/大/小）。走 `build_dataset.canon_trend()`。

### 1.2 两份评测集，角色不同

| 评测集 | 条数 | 用途 | 污染状态 |
|---|---|---|---|
| `sft_holdout.jsonl` | 65 | 主表；16 个意图各 2–10 条手写未见句 | **已被用于迭代**（见下方说明） |
| `probe_unseen.jsonl` | 19 | 泛化探针：**词典里有、两侧训练集都没有**的歌手/歌名/城市/家具×房间 | 未用于调参 |

> **诚实说明**：修 rasa 的第三层（T2b）时，我是看了留出集错例再去补语料/改标注的。
> 从那一刻起，65 条留出集就不再是干净的 held-out，它变成了一个"开发集"。
> 所以本报告把**探针集（19 条，全程没拿来调参）**当作更可信的泛化估计，
> 并在 §6 单独讲它。两边的数字都给出，读者可以各取所需。

两份集合与两侧训练语料的重叠都是 **0 条完全重叠**，且探针集不含任何 holdout 专属词
（检查脚本：`check_nlu_leak.py`、`make_probe.py` 自带的泄漏告警）。

### 1.3 延迟口径

- rasa：计时包住整个 `Agent.parse_message()`，含 jieba 分词 + BERT 特征 + DIET 推理；
  **前 3 次不计时**（首次调用会触发 TF 图的惰性初始化，实测能污染到 1.5 s 级）。
- LLM：`--timing e2e`，含 chat template 与分词（比纯 `generate` 只多 1 ms）；同样预热 3 次。
- 两边都是单句冷延迟，**未做批处理 / 量化 / KV-cache 复用**。

---

## 2. rasa 侧改了什么

### 2.1 四层修改

| 层 | 改了什么 | 为什么非要改 | 效果（see §3/§5） |
|---|---|---|---|
| **T1** | 重写 `custom_number_extractor.py` / `custom_time_extractor.py` | 底层的 Microsoft Recognizers-Text 是**通用**中文数字/日期解析器，没有领域约束；且组件**先删掉 DIET 的 `number` 结果再塞自己的** | exact **+15.38 pt**（受控） |
| **T2** | `nlu.yml` 132 → 358 条例句；把 16 处跨边界的实体标注改成可对齐的写法 | DIET 是 few-shot，≤5 条例句的意图基本全灭；另有 16 条标注被 rasa **静默丢弃** | 意图 +20 pt（未控种子） |
| **T2b** | 修 `dicionary_path` 拼写；给 FALLBACK 补 20 条噪声样本 | 拼错 → 自定义分词词典**从未加载**；补数据后 FALLBACK 占比被稀释，语气词被 CHAT/CLOSE 吸走 | exact +15.38 pt（未控种子） |
| **T2c** | 新增 `custom_trend_normalizer.py`；补 ~20 条 trend 语料 | DSL 规定 `t` 表示**动作方向**（「声音太小了」→ `t=大`），而 rasa 标的是字面词 | 见 §5.4（净收益 0，但把语义对齐了） |

### 2.2 T1 到底修了什么

原始实现有两个致命动作：

```python
# ① 把 DIET 已经抽对的 number 全删掉，再塞自己的结果
exclude_entities = [e for e in entities if e['entity'] != 'number']
message.set(ENTITIES, exclude_entities + extracted_entities, add_to_output=True)
```

```python
# ② 输出零宽 / 被截断的跨度（Recognizers-Text 的中文 offset 本来就是错的）
TEXT: 换一首        → number  value=1  start=1  end=1    ← 零宽
TEXT: 今天          → time    value=2026-10-10 start=0 end=1  ← 只覆盖「今」
TEXT: 明后天        → time    value=2026-10-12 start=1 end=2  ← 只覆盖「后」
TEXT: 三十          → time    value=2026-09-30 start=0 end=1  ← 只覆盖「三」
```

重写后改成 **合并不覆盖**，并加了三道闸：跨度不可用就丢（零宽 / 截断）、
量词搭配不当数值（`一首|一下|一个|一点|一遍|…`）、完整数词识别（`百分之七十`、`四十五`）。

### 2.3 顺手挖出来的三个"静默失败"

这三个都没有任何报错，全靠交叉检查才发现：

1. **`dicionary_path` 拼写错误**（少一个 `t`）。组件读 `config["dictionary_path"]`，
   于是 `data/jieba_userdict/dict.txt` 里的 ~180 个家具/房间/城市/歌手词**一个都没进词典**。
   证据：修好之前模型包里的 `train_JiebaTokenizer` 资源是空的。
2. **16 处实体标注被静默丢弃**。rasa 只对"跨 jieba token 边界"的实体标注发一条
   `UserWarning: Misaligned entity annotation`，然后**丢掉实体、保留样本**。
   `开 [灯]`（中间有空格 → jieba 给 `开[0:1] 灯[2:3]`，边界集合是 {0,1,3}，实体的 start=2 不在里面）之类
   的写法一共 6 处来自原始文件。用 `check_entity_alignment.py` 复现分词后一次全揪出来。
3. **`t` 有 6 种写法**（增大/减小/调大/调小/大/小）。schema 的 hints 明文写 `增减用 t=大/小`，
   所以这属于"标注没按 schema 走"，LLM 侧靠 `canon_trend()` 收敛，rasa 侧靠新加的 normalizer。

### 2.4 trend 语义：为什么需要一个组件

```
声音太小了   → 字面「小」，但用户要**调大**  → t=大
太吵了       → 字面「吵」，但用户要**调小**  → t=小
把声音调大点 → 字面「大」，正常            → t=大
```

LLM 侧训练数据是按语义标的（`augment_dataset.py` 的 `TREND_SEMANTIC`），
rasa 侧如果标字面词，同一个 DSL 字段在两边含义就不同 —— 评测时会被 `canon_trend()`
掩盖，但接进产品就是真 bug。所以加了 `CustomTrendNormalizer`（`is_trainable=False`，
挂在所有实体抽取器之后）：先按字表归一化，再看跨度左侧/内部有没有
`太|这么|那么|有点|过于|好` 这类抱怨标记，有就取反。

---

## 3. 受控 A/B：只改抽取器代码值多少分

阶梯式打补丁的问题在于「每一步都建在上一版之上」，说不清单步的因果。
所以把 T1 单独拎出来做一次干净的 A/B（脚本 `ab_extractor_fix.py`）：

- 两臂都是 **git HEAD 的 `config.yml` + git HEAD 的 `nlu.yml`**（原始 132 条）
- 两臂都显式 `random_seed: 42`
- 两臂之间**只有 `components/custom_{number,time}_extractor.py` 的内容不同**
- 各自铺成一个独立的最小 rasa 工程（`Data/rasa/_ab/<arm>/`），不动工作区

| arm | 意图准确率 | exact | 槽位 P/R/F1 | 应带/实际槽位 | E1 伪数字 | E2 伪日期 | E3 意图错 |
|---|---|---|---|---|---|---|---|
| **A** 原始抽取器 | 75.38% (49/65) | 41.54% (27/65) | .365/.365/.365 | 25 / 33 | 15 | 3 | 16 |
| **B** 重写抽取器 | 75.38% (49/65) | **56.92% (37/65)** | **.558/.519/.538** | 25 / 18 | **0** | **1** | 16 |
| 差 | **±0** | **+15.38 pt** | +0.173 | 少报 15 条 | −15 | −2 | ±0 |

三个关键读法：

1. **意图一模一样（49/65 两边相同，连 E3 都同为 16 条）** —— 反过来验证了这两个组件确实
   在 DIET 下游，不影响分类。这也说明 A/B 的设计是对的，没有混入别的变量。
2. **exact 的 +15.38 pt 全部来自"少报了 15 个槽位"**，与语义能力无关。
3. 上一版报告里我用"把误报的 n/d 删掉再计分"估了个**反事实上界 53.85%**，
   真实重写后是 **56.92%** —— 比估计值还高，因为新实现除了"不误报"，
   还把原来漏掉的正确实体（如 `音量调到[三十]`）补了回来。

> 已发布的原始模型在留出集上是 **40.00%**，A 臂是 41.54%。
> 两者差 1 条 —— 正是 §4 要讲的种子噪声。

---

## 4. ⚠ 随机种子：不钉住 `random_seed` 的评测都不算数

### 4.1 机制

`D:\libs\python310\Lib\site-packages\rasa\utils\tensorflow\models.py`：

```python
def __init__(self, random_seed: Optional[int] = None, **kwargs):
    ...
    if random_seed is None:
        random_seed = int(time.time())      # ← 每秒都不同
    self.random_seed = random_seed
    self._set_random_seed()                 # random / numpy / tf / keras 全套
```

`random_seed` 的来源是 **DIETClassifier 组件配置**（`nlu/classifiers/diet_classifier.py:188`，
`RANDOM_SEED: None`）。而 `rasa train nlu` 在 3.6.19 里**没有 `--random-seed` 参数**，
`rasa train`（顶层）也没有。所以：**默认情况下每次 `rasa train` 出来的 DIET 权重都不一样。**

### 4.2 实测（同一份 `nlu.yml` + 同一份 `config.yml`）

| 训练 | 意图准确率 | exact | 槽位 F1 | 应带/实际槽位 |
|---|---|---|---|---|
| 不钉种子（`int(time.time())`） | 95.38% (62/65) | 87.69% (57/65) | 0.769 | 25 / 22 |
| 不钉种子（**再训一次**，数据配置完全没动） | **100.00% (65/65)** | **89.23% (58/65)** | **0.809** | 25 / 23 |
| `random_seed: 42` | 100.00% (65/65) | 90.77% (59/65) | 0.849 | 25 / 24 |
| `random_seed: 42`（重训） | 100.00% (65/65) | **90.77% (59/65)** | **0.849** | 25 / 24 |
| `random_seed: 7` | 96.92% (63/65) | 87.69% (57/65) | 0.779 | 25 / 22 |

- **钉 42 重训两次 → 逐项完全一致**（连 `应带/实际槽位`、槽位 P/R/F1 都一模一样）。
  也就是说 `random_seed` 一写，训练就是确定的。
- **换种子差 2–3 条**（±3.1 ~ ±4.6 pt exact）。
- 不钉种子的两次差 3 条 —— 和"换种子"的幅度同一个量级，属于同一类噪声。

### 4.3 怎么读这份报告

阶梯 T2 / T2b / T2c 是**在发现这个坑之前**测的，都没有控种子。具体的后果：

| 版本 | 意图 | exact | 说明 |
|---|---|---|---|
| 阶梯 T2b | 100.00% | 87.69% | 与 T2c 只差一版数据 |
| 阶梯 T2c | 95.38% | 87.69% | |
| 阶梯 T2c（重训） | 100.00% | 89.23% | ← **同一份数据重训就 +3 条意图** |

所以 T2b 与 T2c 之间的差别**不能当成"trend 语料有害"的证据** —— 重训一次就翻过来了。
唯一有因果效力的结论是：

- §3 的 T1 A/B（控了种子、只差一个变量）
- §5 的最终方案 vs LLM（控了种子，且重复训练一致）
- 「补语料有用、修词典/对齐有用」这类**幅度远大于噪声**的结论（20 pt vs 3 pt 量级）

代价是把 `random_seed: 42` 写进 `Data/rasa/config.yml` 的 DIETClassifier 段。已在仓库里。

---

## 5. 主结果

### 5.1 65 条手写留出集

| 版本 | 意图准确率 | exact | 槽位 P/R/F1 | 应带/实际槽位 | P50 / P90 |
|---|---|---|---|---|---|
| rasa 原始（原件备份在 `_orig_default_pretrain.tar.gz`） | 67.69% (44/65) | 40.00% (26/65) | .347/.375/.361 | 25 / 32 | 41 / 48 ms |
| A/B arm A 原始抽取器（seed 42） | 75.38% (49/65) | 41.54% (27/65) | .365/.365/.365 | 25 / 33 | 48 / 54 ms |
| A/B arm B 重写抽取器（seed 42） | 75.38% (49/65) | 56.92% (37/65) | .558/.519/.538 | 25 / 18 | 36 / 39 ms |
| 阶梯 T2 补语料（未控种子） | 87.69% | 72.31% | .680/.640/.659 | 25 / 18 | 46 / 51 ms |
| 阶梯 T2b 词典/对齐（未控种子） | 100.00% | 87.69% | .800/.740/.769 | 25 / 21 | 36 / 39 ms |
| 阶梯 T2c trend 语义（未控种子） | 95.38% | 87.69% | .800/.740/.769 | 25 / 22 | 36 / 39 ms |
| **最终（seed 42，已发布）** | **100.00% (65/65)** | **90.77% (59/65)** | **.880/.820/.849** | 25 / 24 | **34–42 / 37–46 ms** |
| 最终方案 seed 7（佐证噪声） | 96.92% | 87.69% | .800/.760/.779 | 25 / 22 | 38 / 42 ms |
| **LLM Qwen3.5-2B + LoRA** | 98.46% (64/65) | **93.85% (61/65)** | **.893/.875/.884** | 25 / 28 | 200 / 395 ms |

### 5.2 19 条未见实体探针

| 版本 | 意图准确率 | exact | 槽位 P/R/F1 | 应带/实际槽位 | P50 / P90 |
|---|---|---|---|---|---|
| **final（seed 42）** | **100.00% (19/19)** | **94.74% (18/19)** | **.947/.947/.947** | 19 / 18 | **40 / 44 ms** |
| final 同配置 seed 7 | 94.74% | 89.47% | .895/.895/.895 | 19 / 17 | 40 / 47 ms |
| 阶梯 T2b | 100.00% | 94.74% | .947/.947/.947 | 19 / 18 | 45 / 46 ms |
| 阶梯 T2c（未控种子） | 84.21% | 78.95% | .895/.895/.895 | 19 / 17 | 37 / 39 ms |
| **LLM Qwen3.5-2B + LoRA** | **100.00% (19/19)** | 63.16% (12/19) | .737/.684/.710 | 19 / 19 | 489 / 548 ms |

### 5.3 最终差距（改完的 rasa vs LLM）

| 指标 | rasa（seed 42） | LLM | 谁赢 |
|---|---|---|---|
| 留出集 意图准确率 | **100.00%** | 98.46% | rasa +1.54 pt |
| 留出集 exact | 90.77% | **93.85%** | LLM +3.08 pt |
| 留出集 槽位 F1 | 0.849 | **0.884** | LLM +0.035 |
| 探针 意图准确率 | **100.00%** | **100.00%** | 平 |
| 探针 exact | **94.74%** | 63.16% | **rasa +31.58 pt** |
| 探针 槽位 F1 | **0.947** | 0.710 | **rasa +0.237** |
| P50 / P90 延迟 | **34–42 / 37–46 ms** | 200 / 395 ms | **rasa 快 ≈5.5× / ≈9.6×** |
| 磁盘 | **28 MB** | ~4 GB 基座 + 87 MB adapter | rasa 小 145× |
| 冷加载 | 22–30 s | ≈35 s | 同量级 |

> ⚠ **上面两张表和 §5.3 已于第二轮部分失效**，原因有两个（详见 §0.1）：
> ① 探针集那一行是 prompt 硬编码 bug 的产物，修好后两边都是 **100%**，不是 rasa 反超；
> ② LLM 的 200/395 ms 是 `timing=e2e` 口径，rasa 的是 `parse_message` 全包——两者不在同一口径上，
> 且 v2 的 LLM 记的是 `timing=gen`。
>
> **同口径（`timing=gen`）重测后的真实对照**：
>
> | 指标 | rasa v2 | LLM v1 | LLM v2 |
> |---|---|---|---|
> | 留出集 65 意图 / exact / F1 | 100% / **92.31%** / .860 | 96.92% / 92.31% / .884 | 98.46% / 90.77% / .842 |
> | 留出集 P50 / P90 | **42 / 45 ms** | 212 / 420 ms | 223 / 440 ms |
> | 探针 19 exact | **100%** | **100%** | **100%** |
> | 延时探针 11 exact | **100%** | 45.45% | 90.91% |
>
> 结论变成：**两边在留出集/探针上已经打平（差 ≤1 条样本）**，
> rasa 的绝对优势只剩**延迟 5×** 与**体积 145×**；
> 而**延时/定时**这种「语料里没写过就说不出」的能力，两边都能靠补语料学会。
> 完整数据见 `doc/time_slot_normalization.md` §5。

### 5.4 关于 T2c 那一层（trend 语义）

T2c 加完 trend 语料（+23 条 CHANGE_VOL）之后，留出集上是「3 条 trend 槽位换 3 条意图」，
exact 净变化 0。当时看起来像"trend 语料扰动了 DIET 的决策边界"。
但 §4.2 表明同一份数据重训就能翻 3 条，**这个"扰动"更可能就是种子**。

不过 trend 语义层本身有两个**确定的**收益，和准确率无关：

1. rasa 的输出与 DSL 定义对齐了（原来是字面词，接进产品会反向调音量）。
2. 抱怨句（`怎么声音这么小`、`能不能小声点`）从"完全不产 `t`"变成"正确产出 `t=大/小`"，
   在最终模型上这两条都对。

所以这一层**保留**，但不能把它的收益算成 exact 的提升。

---

## 6. 未见实体探针：上一版的核心假设是错的

> ### ⚠ 本节结论已于第二轮**作废**，保留原文仅供追溯
>
> 原因是**评测脚本的 bug，不是模型的问题**：`make_probe.py` 当时把 system prompt
> **硬编码**在文件里，与 `build_dataset.build_prompt()` 生成的训练用 prompt
> **不是同一份**（缺 `延时用 d=` 提示、码表书写格式也有差异）。
> LLM 被拿一份从没见过的提示词考，于是输出退化成了
> `把书房的灯打开 → 11|f=book,l=shrine` 这种明显的 off-distribution 产物。
>
> 修法：让 `make_probe.py` 也走 `bd.build_prompt()`（与 `make_delay_probe.py` 一致）。
> 重跑后（三套评测集的 system prompt 已校验**逐字一致**，610 字符）：
>
> | | 意图 | exact | 槽位 P/R/F1 | P50 / P90 |
> |---|---|---|---|---|
> | rasa `nlu-s42-v2` | 100.00% | **100.00% (19/19)** | 1.000 / 1.000 / 1.000 | 40 / 45 ms |
> | LLM v1 `nlu-lora` | 100.00% | **100.00% (19/19)** | 1.000 / 1.000 / 1.000 | 409 / 492 ms |
> | LLM v2 `nlu-lora-v2` | 100.00% | **100.00% (19/19)** | 1.000 / 1.000 / 1.000 | 401 / 481 ms |
>
> **打平。** 所以「rasa 反超 LLM 31.58 pt」和「LLM 缺先验知识」都不成立 ——
> LLM 的预训练先验完全够用（王菲/张国荣/七里香都标对了），
> 之前的差距是喂错了提示词。下面 §6.1~§6.3 的原文保留，请配合本注阅读。

### 6.1 探针怎么造的

用一批**已经放进 jieba 词典、但两侧训练集都没出现过**的实体造句子：

- 歌曲：吻别 / 红日 / 光辉岁月 / 晴天 / 七里香
- 歌手：王菲 / 张国荣 / 李宗盛 / 罗大佑 / 邓丽君
- 城市：天津 / 重庆 / 厦门 / 青岛
- 家具×房间：打开卧室的空调 / 把书房的灯打开 / 卫生间有人没
- 没见过的数值说法：音量调到七十 / 把声音调到四十五

这样 **token 边界是好的**（词典里有），但**没有监督信号**。
于是可以区分两种瓶颈：

- 若 rasa 能标对 → 瓶颈是分词（词典/边界），补词典就能解决
- 若 rasa 标不出而 LLM 标得出 → 瓶颈是**预训练先验**，补语料也难追

### 6.2 结果：和预期相反

| | 意图 | exact | 槽位 F1 |
|---|---|---|---|
| rasa final（seed 42） | 100.00% | **94.74% (18/19)** | 0.947 |
| LLM | 100.00% | 63.16% (12/19) | 0.710 |

**rasa 反超 31.58 pt。** 唯一错的一条是 `来一首七里香 → 10`（漏了 `s=七里香`）。

LLM 侧错的 7 条是同一类：**把没见过的歌手名当成歌名**。

```
播放王菲的歌   gold=10|g=王菲      pred=10|s=王菲
想听张国荣     gold=10|g=张国荣    pred=10|s=张国荣
放李宗盛的歌   gold=10|g=李宗盛    pred=10|s=李宗盛
来首罗大佑的   gold=10|g=罗大佑    pred=10|g=罗大佑   （"的"字尾没吃掉，值也不对）
```

### 6.3 怎么解释

- **rasa 的优势来自"标注好边界就照猫画虎"**：`[X](singer)` 这个模式在训练集里出现过 48 次，
  换一个没见过的名字进同一个位置，DIET 照样标 —— 它不需要"知道王菲是谁"。
- **LLM 的劣势恰恰来自它知道太多**：Qwen 知道"王菲是歌手"，但在 few-shot DSL 里
  `s=` 和 `g=` 的判别规则（带「的歌/的」是歌手？还是取决于词位？）学得不够稳，
  于是靠常识去猜，猜反了。这也解释了为什么它 `应带/实际槽位` 是 19/19 全报满却只对 12 条。

**结论：rasa 剩下的差距不是"没有先验知识"，而是分词边界 + 类内多样性。**
这直接改变了上一版"要多花标注成本才能追平"的判断 —— 补到 ~350 条、并把词典和对齐修对，
就基本追平了；再往上加的瓶颈在别处（见 §7）。

---

## 7. 剩余错例

### 7.1 rasa final：6 条（留出集）+ 1 条（探针）

| 输入 | 期望 | 实际 | 归因 |
|---|---|---|---|
| 想听陈奕迅 | `10\|g=陈奕迅` | `10\|g=迅` | **三字人名边界崩**，只吃到最后一个字 |
| 放孙燕姿的遇见 | `10\|s=遇见,g=孙燕姿` | `10\|g=孙燕姿` | 漏多槽里的 `s` |
| 来首李荣浩的模特 | `10\|s=模特,g=李荣浩` | `10\|g=李荣浩` | 同上 |
| 放一下夜曲 | `10\|s=夜曲` | `10` | 未见歌名，边界没敢标 |
| 把声音调大点 | `3\|t=大` | `3\|t=调` | trend 跨度切在「调」上（应收敛到「大」） |
| 客听的灯开一下 | `11\|f=灯,l=客厅` | `11\|f=灯` | 同音错字「客听」没进词典 → 房间没抽到 |
| *（探针）* 来一首七里香 | `10\|s=七里香` | `10` | 未见歌名，边界没敢标 |

**7 条全是"实体跨度画不准"，0 条意图错。**

### 7.2 LLM：4 条（留出集）+ 7 条（探针）

| 输入 | 期望 | 实际 | 归因 |
|---|---|---|---|
| 客听的灯开一下 | `11\|f=灯,l=客厅` | `11\|f=灯` | 训练集无「灯 × 客厅」组合 |
| 有人在家吗 | `8` | `8\|l=家` | 标注口径未定：「家」算不算 location |
| 外面下雨了吗 | `15` | `15\|c=外面` | 模板 `{city}下雨吗` 过度泛化 |
| 西红柿炒鸡蛋 | `0` | `11\|f=西红柿炒鸡蛋` | FALLBACK 噪声覆盖不足 |
| 播放王菲的歌 等 7 条 | `10\|g=X` | `10\|s=X`（或漏） | **未见歌手被当成歌名**（§6.2） |

**11 条也全是槽位/口径问题，只有 1 条意图错。**

### 7.3 两边错例的性质对照

| | 意图错 | 实体错 | 错的根因 |
|---|---|---|---|
| rasa final | **0** | 7 | 中文专有名词的 token 边界；缺同音变体 |
| LLM | 1 | 10 | 预训练常识与 DSL 约定冲突；训练数据覆盖 |

两边的"病"完全不同，这正好是混合方案的理由（§8.3）。

---

## 8. 结论与建议

### 8.1 结论

1. **上一版"rasa 差 53.85 pt"的结论要撤回。** 修正后的量级是：
   - 抽取出错 ≈ **15.4 pt**（纯 bug）
   - 语料不足 ≈ **30 pt**（132 → 358 条）
   - 词典拼错 + 实体标注被丢 ≈ 另有一部分
   - 剩下的 ~3 pt 才是真正的能力差

2. **在 16 类固定指令这个封闭域里，rasa 已经是一个合理选择**：
   65 条留出集意图满分、exact 90.77%；探针集 exact 94.74%（反超 LLM 31.58 pt）；
   延迟快 6–10 倍；体积 28 MB（树莓派友好）。

3. **但这个结论有前提，必须一起说**：
   - 语料从 132 条补到 358 条是**必要条件**，原始语料下怎么调都到不了。
   - 语料是**为这 16 个意图定制的**，扩到新意图/新实体类型要重新标注。
   - 65 条留出集已被用于迭代，**90.77% 这个数字偏乐观**；
     更可信的泛化估计是探针集的 94.74%（但只有 19 条，置信区间很宽）。
   - **不钉 `random_seed` 就没有可复现性**，±3 条的抖动比很多"优化"都大。

4. **LLM 的价值不在这个封闭域内的 exact，而在开集泛化**：
   留出集 exact 93.85%（略胜 3 pt），代价是 6 倍延迟、145 倍体积；
   探针集上反而输 31.58 pt。它的真正用武之地是 rasa 的 16 类覆盖不到的句子。

### 8.2 落地

> 📌 **2026-10 更新**：本节记录的是 rasa 与微调 LLM 的**对比评测结论**，仍然有效。
> 但运行时**默认后端已改为自研 ONNX NLU**（`nlu.backend = "onnx"`），
> rasa 只作兜底保留；同时配置文件已迁移为 YAML
> （`Data/default_config.yaml` / `config.yaml`）。ONNX 版在同一留出集上
> 意图 98.46% / 槽位 exact 93.85%，P50 1 ms、常驻 ~127 MB，
> 相对 rasa 是 42× 延迟、15× 内存的改善。详见 [config.md](config.md)。

- 最终模型已发布到应用实际读取的路径：
  `Data/rasa/models/default.tar.gz`（`Data/default_config.yaml` 里 `rasa.model` 指的就是它）。
- 原始模型保留在 `Data/rasa/models/_orig_default_pretrain.tar.gz`，也可从 git 取回。
- 训练源：`Data/rasa/{config.yml, data/nlu/nlu.yml, components/}`，与发布的模型一一对应，
  并已钉 `random_seed: 42`。
- 中间版本模型留在 `Data/rasa/models_v2/`（`nlu-t2` / `nlu-t2b` / `nlu-t2c` /
  `nlu-t2c-repro` / `nlu-s42` / `nlu-s42b` / `nlu-s7`），供复核；它们不在 git 里。

### 8.3 还能再改什么（按性价比排序）

1. **给 `s`/`g` 加 `RegexEntityExtractor` + lookup table**（当前 7 条错例里 4 条是它）。
   这是最后一块硬骨头，且成本很低：歌手/歌名本身就是有限集合。
2. **把 trend 跨度规范化写进组件**：`把声音调大点` 现在标成 `t=调`，
   应该在 normalizer 里按字表把跨度收敛到含方向字的那个字/词。
3. **把同音变体表补全**（「客听」→「客厅」），或在训练前统一做同音纠错。
4. **给「家」这类词定标注口径**，两侧同改，否则永远是错例。
5. **混合路线的接口**：rasa 用 `intent.confidence` + `entity.confidence` 做阈值，
   低于阈值的整句升到 LLM。当前 rasa 的实体置信度是有的（DIET 输出里有
   `confidence_entity` / `confidence_role`），可以直接用。

---

## 9. 复现

### 9.1 环境

```bash
# rasa 侧必须是 Python 3.8–3.10（本机 D:\libs\python310）
# 关键 pin：transformers==4.36.2（rasa 会 `from transformers import TFPreTrainedModel`，
# 而 transformers 5.x 删掉了 TF 支持）、tokenizers<0.19、huggingface-hub<0.21
```

### 9.2 命令

```bash
# ---- 1) 训练（必须显式 --seed，见 §4）----
cd /d/projects/QSmartAssistant
python tools/nlu_llm_finetune/train_rasa.py --name nlu-final --seed 42 --force
# 产物：Data/rasa/models_v2/nlu-final.tar.gz

# ---- 2) 评测（CWD 必须是 Data/rasa：model_weights 是相对路径）----
cd /d/projects/QSmartAssistant/Data/rasa
/d/libs/python310/python.exe ../../tools/nlu_llm_finetune/eval_rasa.py \
    --model models/default.tar.gz \
    --eval ../../tools/nlu_llm_finetune/data/sft_holdout.jsonl \
    --dump-json   ../../tools/nlu_llm_finetune/output/rasa_s42_metrics.json \
    --dump-errors ../../tools/nlu_llm_finetune/output/rasa_s42_errors.jsonl
# 换 --eval .../probe_unseen.jsonl 即跑探针集

# ---- 3) 受控 A/B：只换抽取器代码 ----
cd /d/projects/QSmartAssistant
python tools/nlu_llm_finetune/ab_extractor_fix.py --seed 42

# ---- 4) 归因与汇总 ----
cd tools/nlu_llm_finetune
python analyze_errors.py output/rasa_s42_errors.jsonl
python summarize.py            # 主表
python summarize.py --probe    # 探针表
python summarize.py --seed     # 种子敏感性
python check_nlu_leak.py       # 确认语料与留出集零重叠
python check_entity_alignment.py   # 确认没有跨边界的实体标注

# ---- 5) LLM 侧（Python 3.12 + ROCm，见 doc/nlu_llm_results.md）----
/d/libs/python312/python.exe tools/nlu_llm_finetune/eval_lora.py \
    --backend hf --base ./models/qwen3.5-2b \
    --adapter tools/nlu_llm_finetune/output/nlu-lora \
    --eval tools/nlu_llm_finetune/data/sft_holdout.jsonl \
    --timing e2e --warmup 3 \
    --dump-json   tools/nlu_llm_finetune/output/hf_metrics.json \
    --dump-errors tools/nlu_llm_finetune/output/hf_errors.jsonl
```

### 9.3 环境坑（rasa 侧，都是踩过的）

1. **rasa 3.6.19 只支持 Python 3.8–3.10。** 本机用 Python 3.10 embeddable 包，
   它没有 `ensurepip`，要手工改 `python310._pth`（放开 `import site`、追加
   `Lib\site-packages`）再用 `get-pip.py` 引导。
2. **`Agent.load()` 不会把 CWD 加进 `sys.path`**，而 `config.yml` 里写的是
   `components.jieba_tokenizer.JiebaTokenizer` 这种项目内相对导入。`rasa train` 会自动处理，
   `Agent.load()` 不会 —— 评测脚本里手动 `sys.path.insert(0, os.getcwd())`。
3. **`os.chdir(rasa_dir)` 会打断相对路径参数。** `model_weights: "data/chinese_rbtl3"`
   是相对 CWD 的，必须先 chdir，再在 chdir 之前把所有路径参数 `abspath()`。
4. **`rasa train nlu` 没有 `--force`**（3.6.19）。要强制重训得删 `.rasa/cache`，
   否则指纹相同会直接复用旧权重，测不出训练确定性。
5. **`rasa train nlu` 也没有 `--random-seed`。** 种子只能写在 config 的 DIETClassifier 段里。
6. **实体标注跨 jieba token 边界会被静默丢弃**（只发 `UserWarning`，样本保留、实体丢掉）。
   改任何 `nlu.yml` 之后都该跑一遍 `check_entity_alignment.py`。
7. **`dicionary_path` 这类拼错不会报错**，只会静默不加载。改 config 后建议核对
   模型包里的 `components/train_*` 目录是否真的出现。

---

## 附：本轮新增/修改的文件

| 文件 | 状态 | 说明 |
|---|---|---|
| `Data/rasa/config.yml` | 改 | 修 `dicionary_path` 拼写；注册 `CustomTrendNormalizer`；**钉 `random_seed: 42`** |
| `Data/rasa/components/custom_number_extractor.py` | 重写 | 合并不覆盖；零宽/截断跨度拒收；量词搭配不当数值 |
| `Data/rasa/components/custom_time_extractor.py` | 重写 | 同上；删掉模块级的调试调用和 `hour()` 错用 |
| `Data/rasa/components/custom_trend_normalizer.py` | 新增 | trend 表面说法 → 语义方向；抱怨句取反 |
| `Data/rasa/components/_orig_backup/` | 新增 | 两个原始组件的备份（供 A/B 使用） |
| `Data/rasa/data/nlu/nlu.yml` | 改 | 132 → 358 条；修 16 处跨边界实体标注 |
| `Data/rasa/data/jieba_userdict/dict.txt` | 改 | 2 → ~180 条（家具/房间/城市/歌手/歌名） |
| `Data/rasa/models/default.tar.gz` | **替换** | 最终模型（seed 42）；原件备份在 `_orig_default_pretrain.tar.gz` |
| `tools/nlu_llm_finetune/train_rasa.py` | 新增 | 训练包装；`--seed` 生成临时配置注入 `random_seed` |
| `tools/nlu_llm_finetune/ab_extractor_fix.py` | 新增 | 受控 A/B（§3） |
| `tools/nlu_llm_finetune/check_entity_alignment.py` | 新增 | 复现 jieba 分词，检测跨边界标注 |
| `tools/nlu_llm_finetune/check_nlu_leak.py` | 新增 | 语料/留出集零重叠检查 |
| `tools/nlu_llm_finetune/make_probe.py` | 新增 | 生成未见实体探针集（§6） |
| `tools/nlu_llm_finetune/analyze_errors.py` | 新增 | 错例 E1–E4 归因 |
| `tools/nlu_llm_finetune/intent_coverage.py` | 新增 | 两侧每意图样本量对照 |
| `tools/nlu_llm_finetune/try_phrasings.py` | 新增 | 批量试探哪些说法能通过实体边界对齐 |
| `tools/nlu_llm_finetune/summarize.py` | 改 | 增加 A/B 臂、受控种子、探针三张表 |
| `tools/nlu_llm_finetune/eval_rasa.py` | 改 | 增加 `--dump-json` / `--dump-errors` / `--rasa-dir` |
| `tools/nlu_llm_finetune/output/*` | 新增 | 全部版本的指标 JSON 与错例 JSONL |
