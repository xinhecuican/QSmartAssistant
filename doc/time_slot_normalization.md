# 时间槽位归一化：从「原文跨度」到「秒偏移」

> 起因：`30分钟后开启热水器` 需要把 **1800** 作为参数传给 HA 脚本。  
> 结论：`time` 槽位以前存的是跨度**原文**（`今天` / `30分钟后`），  
> 下游两个消费者都用不了；现在在 `entity2Slot()` 里统一归一化成  
> **相对当前时刻的秒偏移**，HA 直接拿到 `1800`。
>
> **本轮已落地**：`Utils/TimeNormalizer` + 两端接入 + 补语料重训两端 + 重新评测；
> `Data/rasa/models/default.tar.gz` 已换成 v2（sha256 `e669d975…`）；
> 热水器那条 `useAssistant` 已改为 `false`；
> **`nlu.backend` 已钉到 `rasa`** 并补上 `rasa.python_venv`
> （运行期目录 `~/.config/QSmartAssistant/Data/` 是**独立副本不是软链**，
> 模板与运行期两处都已同步）—— 见 §6。
> 端到端实测：`30分钟后开启热水器` → `time=30分钟` → 归一化 `1800` → HA `duration`。

---

## 1. 以前是什么样：只有「跨度」，没有「归一化」

整条链路是 `文本 → NLU 抽实体 → entity2Slot() → 插件`。

`time` 槽位的历史口径是**原文跨度**，两侧一致：

| 侧              | 例子                                                   |           |             |         |
| -------------- | ---------------------------------------------------- | --------- | ----------- | ------- |
| rasa `nlu.yml` | `[今天](time) 天气怎么样`、`[明天早上](time)天气如何`、`[今后三天](time)` |           |             |         |
| LLM DSL        | \`15                                                 | d=今天`、`15 | d=今天晚上`、`15 | d=明后天\` |

- `CustomTimeExtractor` 的 `value_as_text: True` 也吐原文，  
  它与 DIET 的结果**合并**（不覆盖）。
- `Data/nlu_schema.json` 里 `d` 原本**只挂在 `Weather` 上**：  
  `"Weather": {"entities": ["c","d"]}`；`OPEN_FURNITURE` / `CLOSE_FURNITURE`  
  只有 `f` / `l`。
- 语料里**没有任何相对时长样本**。唯一沾边的是  
  `[热水器](furniture)开[20](number)分钟` —— `20` 被标成 `n`，`分钟` 不是实体。

所以「30分钟后」这类说法，在这套体系里**根本不存在**。

## 2. 为什么非改不可：两个消费者都吃不下原文

### 2.1 `hass` —— 原样塞进 HA 脚本变量

`Plugins/hass/hass.cpp:207 parseValue()`：

```cpp
QRegularExpression keyFinder("\\{(.*)\\|(.*)\\}", QRegularExpression::InvertedGreedinessOption);
...
IntentSlot slot = intent.getSlot(key, success);
if (success) { result += slot.value; }          // ← 原样填进去
else { if (defaultValue != "--") result += defaultValue; }
```

配置里 `OPEN_FURNITURE` 早就接对了：

```json
{ "intent": "OPEN_FURNITURE", "pattern": ".*热水器.*",
  "path": "/api/services/script/turn_on",
  "params": { "entity_id": "script.unknown_3",
              "variables": { "duration": "{time|--}" } } }
```

**但槽位值是 `今天` 这种中文**，HA 的 `delay` 要的是秒数。  
更常见的情况是槽位压根不存在 → 默认值 `--` → `parseObject()` 里  
`if (value != "")` 判定为空 → **整个键都不传**，脚本用自己的默认值。

### 2.2 `Weather` —— 静默当今天

`Plugins/weather/weather.cpp` 要的是 `yyyy-MM-dd[ hh:mm:ss]` 或 `start|end`。  
`QDateTime::fromString("今天", "yyyy-MM-dd")` 是 invalid，  
而 `currentDate.daysTo(invalid)` 恒为 0 →  
**静默播报今天的天气**。看着「能用」纯属巧合。

### 2.3 缺口的位置

`entity2Slot()` 本来就是**语义归一化层** —— 它早就把 `trend` 的原文跨度  
（`调大点` / `太小了`）翻译成 `--HIGHER--` / `--SMALLER--`。  
`time` 只是当年漏掉的那一个。

## 3. 设计

**`time` 槽位承载「相对现在的秒偏移」**（复用现有实体，不改 HA 配置）。

| 输入原文                    | 归一化结果            | 说明                          |
| ----------------------- | ---------------- | --------------------------- |
| `30分钟后` / `30分钟`        | `1800`           | 相对时长                        |
| `半个小时` / `半小时`          | `1800`           |                             |
| `一小时` / `一个小时`          | `3600`           |                             |
| `一个半小时`                 | `5400`           |                             |
| `一刻钟`                   | `900`            |                             |
| `20分半`                  | `1230`           |                             |
| `明天`                    | `34200`          | 到明日 00:00（14:30 时为基准）       |
| `明天早上`                  | `59400`          | 到明日 07:00                   |
| `明天早上8点`                | `63000`          | 到该钟点                        |
| `周末`                    | `379800\|552600` | 区间，沿用 `\|` 分隔               |
| `今后三天`                  | `34200\|293400`  | 区间                          |
| `明后天`                   | `34200\|207000`  | 区间                          |
| 无法识别（`今`/`后`/`三`/`今天晚`） | 空串               | 槽位被丢弃，`{time\|--}` 回退到 `--` |

**粒度约定**（让下游不用额外类型标记就能区分天/小时）：  
只到「日」的表达落在目标日 `00:00:00`，带时段/时刻的落在具体钟点。  
下游按「是否恰好是 `00:00:00`」判断按天还是按小时处理。

**「顺延到未来」三条规则**（只在**没写明确日子**时生效）：

1. 只给钟点/时段（`8点`、`中午`）而今天该时刻已过 → 顺延到明天；
2. 只给月日、没写年份（`3月5日`）而今年该日已过 → 顺延到明年；
3. 显式写了日子（`今天` / `明天` / `今晚` / `周三`）→ 不顺延，  
   即使算出来是过去时刻（「今天早上」在傍晚说就是今天早上）。

**为什么放在 C++ 的 `entity2Slot()` 而不是各自的后端里**：

- 训练语料必须保持**原文跨度**（DIET 只能学 token 对齐的跨度；  
  LLM 也不该在推理时依赖「当前时间」，否则语料无法静态化）；
- 归一化逻辑只有一份，rasa 与 LLM 两个后端**不可能漂移**；
- 不用改用户的 HA 配置。

## 4. 实现

### 4.1 `Utils/TimeNormalizer.{h,cpp}`（新增）

纯 `Qt6Core` 依赖的静态函数：

```cpp
static QString TimeNormalizer::normalize(const QString &raw,
                                        const QDateTime &now = QDateTime::currentDateTime());
```

`now` 显式可注入，这样单元测试能锁死时钟。  
解析顺序：**区间 → 时长 → 绝对日期/时刻**。  
（区间必须排在时长前面，否则 `今后三天` 会被 `^\d+天$` 当成「三天」时长。）

覆盖：中文数字（`三十`/`两`/`一个半`）、尾缀（`后`/`之后`/`以后`/`以内`）、  
前缀（`等`/`再`/`过`）、时段词（`早上`→07:00、`晚上`→19:00 …）、  
星期、`周末` 区间（含「今天就是周六/周日」的边界）、年月日。

### 4.2 接入点：两个后端的 `entity2Slot()`

`Conversation/NLU/rasanlu.cpp` 与 `Conversation/NLU/llmnlu.cpp`  
的 else 分支里，对名为 `time` 的槽位做归一化；**归一化失败就丢弃该槽位**，  
让 `{time|--}` 回退到 `--`：

```cpp
if (slot.name == "time") {
    const QString normalized = TimeNormalizer::normalize(slot.value);
    if (!normalized.isEmpty()) {
        slotIntent.appendSlot(IntentSlot("time", normalized, slot.conf));
    }
    continue;
}
```

### 4.3 语料与 schema

`Data/rasa/data/nlu/nlu.yml` 新增 14 条延时样本（OPEN_FURNITURE 7 + CLOSE_FURNITURE 7）：

```yaml
- [30分钟](time)后打开[热水器](furniture)
- [半小时](time)后打开[空调](furniture)
- 等[十分钟](time)后打开[卧室]{...}的[灯](furniture)
- [明天早上8点](time)打开[热水器](furniture)
```

**⚠ 实体跨度刻意不含尾部的「后」**：jieba 会把「后开」「后关」粘成一个 token  
（实测 `20分钟后开一下空调` → `20|分钟|后开|一下|空调`），  
跨度只要跨过这个粘连边界，rasa 就会 WARNING 然后**静默丢弃**整条标注。  
把「后」留在实体外，边界永远落在干净切分点上；  
归一化器对含「后」和不含「后」两种写法都能处理。

`tools/nlu_llm_finetune/build_dataset.py`（schema 的**真正源头**）：

- `INTENT_ENTITY_CODES` 给 `OPEN_FURNITURE` / `CLOSE_FURNITURE` 补上 `"d"`；
- `HINTS` 改成 `"房间用 l=,延时用 d=（照抄时长原话，如 30分钟）,"`；
- 新增 `EXTRA_SHOT_TEXTS`，把 `30分钟后打开热水器` / `30分钟后关掉热水器`  
  两条 few-shot 钉进 prompt（2B 模型只靠 hint 容易整条漏掉 `d`）。

> 注意：`Data/nlu_schema.json` 是 `build_dataset.py` **生成**的，  
> 直接手改会被下次运行覆盖 —— 必须改 `HINTS` / `INTENT_ENTITY_CODES`。

### 4.4 单元测试

`tools/timenormalizer_test/`（独立 CMake 工程，只链 `Qt6Core`）：

```bash
cmake -S tools/timenormalizer_test -B build-tn -G Ninja \
      -DCMAKE_PREFIX_PATH=<Qt6 msvc 目录>
cmake --build build-tn
build-tn/bin/timenormalizer_test.exe
```

**77 条断言全部通过**，含一段「语料/留出集/probe 里出现过的全部 23 个  
time 跨度都必须归一化出非空值」的回归 —— 这条是专门用来防  
「某个跨度悄悄归一化成空串 → 槽位被丢弃 → 白掉一分」的。

## 5. 评测

### 5.1 延时能力专项 A/B —— 这轮改动的直接收益

新增 `tools/nlu_llm_finetune/data/probe_delay.jsonl`（11 条人工撰写、  
与两侧训练集**零逐字重叠**、含 2 条「无延时」对照组抓幻觉）。

**A. rasa：这一组是干净的「只补语料」对照**（rasa 不吃 prompt，只有 nlu.yml 变了）

| 模型                               | 意图准确率   | exact               | 槽位 P / R / F1             | P50 / P90  |
| -------------------------------- | ------- | ------------------- | ------------------------- | ---------- |
| 旧（`nlu-s42.tar.gz`，**无**延时语料）    | 100.00% | **18.18%** (2/11)   | 0.955 / **0.621** / 0.753 | 44 / 45 ms |
| 新（`nlu-s42-v2.tar.gz`，+14 条延时语料） | 100.00% | **100.00%** (11/11) | 1.000 / 1.000 / 1.000     | 51 / 54 ms |

旧模型的失败形态很整齐 —— 9 条带延时的句子里 **8 条完全没抽出 `d`**，  
剩下 1 条 `明天早上7点打开热水器` 被截成 `d=明天早上`（丢了「7点」）；  
11 条里仅有的 2 个 exact 正是那 2 条**无延时对照组**（本来就该没有 `d`）。  
这正是「语料里不存在的能力，模型不可能凭空学会」的直接证据。

**B. LLM：这一组**不是**干净的只补语料对照**，要注意两个混淆项：  
v2 同时吃到 ① 新增延时语料，② `parse_nlu` 覆盖 bug 修好后多出来的 ~216 条  
（训练集 656 → 925 条）。

| 模型               | 意图准确率   | exact              | 槽位 P / R / F1         | P50 / P90    |
| ---------------- | ------- | ------------------ | --------------------- | ------------ |
| v1 `nlu-lora`    | 100.00% | **45.45%** (5/11)  | 0.818 / 0.758 / 0.787 | 485 / 591 ms |
| v2 `nlu-lora-v2` | 100.00% | **90.91%** (10/11) | 0.955 / 0.955 / 0.955 | 534 / 624 ms |

v1 已经能靠 few-shot 的暗示抽出一部分 `d`（45.45%），但形态不稳：  
`十分钟`/`十五分钟` 被改写成阿拉伯数字 `10分钟`/`15分钟`（值不匹配）、  
`两个小时后` 把尾部的「后」也吃进了跨度、两条干脆整条漏掉 `d`。  
v2 只剩 1 条错：`十五分钟后把灯打开 → d=15分钟`（中文数字改成了阿拉伯数字）。

> 这类「同义写法」如果继续追求 exact，就该轮到 `canon_delay` 去收敛  
> 中文/阿拉伯数字 —— 但**刻意不做**，理由见 §5.3。

### 5.2 主留出集与泛化探针（回归检查）

⚠ 先说两个**必须一起看**的口径问题，否则结论会反：

1. **计时口径**：v1 的 `output/hf_metrics.json` 记的是 `timing=e2e`，  
   v2 记的是 `timing=gen`，**跨口径不可比**。已补跑 v1 的 `gen` 口径。
2. **prompt 口径**：`sft_holdout.jsonl` 是 21:26 重生成的（新 prompt），  
   但 `probe_unseen.jsonl` 停留在 20:30（**旧 prompt**，见 §5.5）。  
   已重生成探针集，并校验四份文件（train/eval/holdout/两个 probe）  
   的 system prompt **逐字一致**（610 字符）。

| 数据集    | 模型                       | 意图      | exact       | P / R / F1             | P50 / P90        |
| ------ | ------------------------ | ------- | ----------- | ---------------------- | ---------------- |
| 留出集 65 | rasa v1 `nlu-s42`        | 100.00% | 90.77%      | .880 / .820 / .849     | 34 / 37 ms       |
| 留出集 65 | **rasa v2** `nlu-s42-v2` | 100.00% | **92.31%**  | .880 / .840 / .860     | 42 / 45 ms       |
| 留出集 65 | LLM v1 `nlu-lora`        | 96.92%  | 92.31%      | **.893 / .875 / .884** | **212 / 420 ms** |
| 留出集 65 | **LLM v2** `nlu-lora-v2` | 98.46%  | 90.77%      | .852 / .833 / .842     | 225 / 451 ms     |
| 探针 19  | rasa v1                  | 100.00% | 94.74%      | .947 / .947 / .947     | 40 / 44 ms       |
| 探针 19  | **rasa v2**              | 100.00% | **100.00%** | 1.000 / 1.000 / 1.000  | 40 / 45 ms       |
| 探针 19  | LLM v1                   | 100.00% | **100.00%** | 1.000 / 1.000 / 1.000  | 409 / 492 ms     |
| 探针 19  | **LLM v2**               | 100.00% | **100.00%** | 1.000 / 1.000 / 1.000  | 401 / 481 ms     |

**怎么读**（重要）：

- 留出集上 rasa **+1 条样本**（92.31% vs 90.77%），LLM v2 与 v1 **差 1 条样本**  
  （90.77% vs 92.31%）。65 条集上 1 条 = 1.54 pt，  
  **≤1 条的差异不作结论**。补语料没有把别的东西挤坏。
- 实测过噪声底：同一 adapter、同一口径**复跑 2~3 次结果完全一致**；  
  换口径/换 prompt 才会动 1 条。所以上面这些 1 条级别的差异属于  
  「口径 + 样本量」的噪声，不是能力差异。
- LLM 的 `应带/实际槽位` 在 v2 是 25/27（仍然多报 2 个幻觉槽位：  
  `有人在家吗 → 8|l=家`、`外面下雨了吗 → 15|c=外面`），precision 因此被拉低 ——  
  这是 LLM 侧**唯一稳定存在的实质短板**。
- 延迟只在**同一评测集内**横向可比：跨集不可比，因为生成 token 数不同  
  （留出集均值 4.4 token → P50 225 ms；探针 7.7 token → P50 455 ms，  
  同一 adapter 同一口径实测）。同集内复跑抖动约 ±10 ms。
- LLM 延迟 v1 212 → v2 225 ms（+6%），因为 prompt 从 4 条 few-shot 加到 6 条。  
  rasa 42–45 ms，**绝对优势约 5×**。

### 5.3 尺子上补掉的一个写法差异

补完延时语料后，rasa 在探针上答的是 `d=半个**小时**`，而 gold 标的是  
`d=半小时` —— 归一化后**都是 1800**，但 exact 按字符串比会白扣两分  
（81.82% 而不是 100%）。

这和当年 `trend` 的 `调大` / `大` 是同一类问题，所以按同一层处理：  
在 `build_dataset.canon_delay()` 里收敛**虚词**「个」，  
由 `eval_lora.score()` 对**预测与 gold 两侧同时**施加  
（`_canon_slots()`，与 `canon_trend` 并列）。

> 刻意**不做**的事：不在 Python 里做「时间 → 秒」换算（那是  
> `Utils/TimeNormalizer` 的唯一职责，两份实现必然漂移）；  
> 也不收敛中文数字与阿拉伯数字（`十分钟` vs `10分钟`）——  
> 两者在语料里都是独立标注，属模型该学的内容，不该由尺子抹平。

**验证过这个改动没有动历史数字**：留出集在新旧尺子下都是 `92.31%`（rasa v2）。

顺带发现一个**尺子的真缺陷**（本次未采纳，仅存证）：  
`把声音调大点` 的 DIET 会同时吐 `trend=调`（裸跨度）与 `trend=大`  
（经 `EntitySynonymMapper` 映射），而 `rasa_to_dsl` 的「同码取第一个」  
规则取到了 `调`。这是**转换缺陷而非模型缺陷**。  
`eval_rasa.py --prefer-synonym` 可以量化它：

```bash
python eval_rasa.py --model models_v2/nlu-s42-v2.tar.gz \
       --eval data/sft_holdout.jsonl --prefer-synonym
# -> exact 93.85% (61/65), 槽位 0.920 / 0.880 / 0.900
```

**默认仍是关闭**：历史对照表全部在「取第一个」口径下量出，  
改默认会让新旧数字不可比。要采纳得把历史那批模型全部重跑一遍。

### 5.4 ⚠ 探针集上的「假失败」：prompt 版本对不上

写 §5.2 时发现 LLM 在探针集上只有 63.16%（v1）/ 73.68%（v2），  
而 rasa 是 94.74% / 100%。追下去发现**是评测脚本的 bug，不是模型的问题**：

`tools/nlu_llm_finetune/make_probe.py` 把 system prompt **硬编码**在文件里  
（一份手写的旧版本），而训练语料的 prompt 是  
`build_dataset.build_prompt()` 按 schema **动态生成**的。两者不是同一份：

- 旧的缺少 `延时用 d=` 这条 hint；
- 意图码表的书写格式也不同（`11=OPEN_FURNITURE[f,l]` vs 生成版的排布）。

于是 LLM 被拿一份**从没见过的提示词**考，输出直接退化成了  
`把书房的灯打开 → 11|f=book,l=shrine`、`音量调到七十 → 3|n=70`  
这种典型的 off-distribution 产物 —— 而 rasa 是 JSONL 里只读  
`messages[-2].content`（用户文本），**完全不受 prompt 影响**，  
所以这个 bug 只打 LLM 一边，看起来就像"rasa 靠标注、LLM 靠常识但常识不灵"。

**修法**：让 `make_probe.py` 也走 `bd.build_prompt()`，与 `make_delay_probe.py` 一致。  
改完重新生成，并加了一条**自检**（顺手做的）——校验四个文件的 system prompt 逐字相同：

```bash
python -c "
import json
sp=lambda p: json.loads(open(p,encoding='utf-8').readline())['messages'][0]['content']
a=sp('data/sft_holdout.jsonl')
for p in ['data/probe_unseen.jsonl','data/probe_delay.jsonl','data/sft_train.jsonl']:
    print(p, a==sp(p))
"
# holdout == probe_unseen : True
# holdout == probe_delay  : True
# sft_train == holdout    : True
# prompt 长度(字符): 610
```

修好后两边都满分：

| 探针 19 条（prompt 已对齐）  | 意图      | exact       | 槽位 P/R/F1             |
| -------------------- | ------- | ----------- | --------------------- |
| rasa `nlu-s42-v2`    | 100.00% | **100.00%** | 1.000 / 1.000 / 1.000 |
| LLM v1 `nlu-lora`    | 100.00% | **100.00%** | 1.000 / 1.000 / 1.000 |
| LLM v2 `nlu-lora-v2` | 100.00% | **100.00%** | 1.000 / 1.000 / 1.000 |


**连带作废的结论**：`doc/rasa_vs_llm_nlu.md` §6「探针集上 rasa 反超 LLM 31.58 pt」  
以及由它推出的「LLM 缺先验知识」。LLM 的预训练先验**完全够用** ——  
王菲 / 张国荣 / 七里香 / 李宗盛 全都标对了。  
已在该文件 §0.1 与 §6 顶部加了作废标注。

> 教训：**凡是"给模型的输入"，都必须和训练时的输入逐字一致，并且在评测集生成脚本里  
> 用同一份生成逻辑产出**。任何一处手抄的提示词，都会在若干轮之后变成幽灵问题。

### 5.5 复现命令

```bash
# 0) 先重生成评测集（保证 prompt 与训练一致，见 §5.4）
cd tools/nlu_llm_finetune
python make_probe.py          # 19 条未见实体探针
python make_delay_probe.py    # 11 条延时探针

# 1) 单元测试（77 条断言 + 语料跨度回归）
cd ../..
cmake -S tools/timenormalizer_test -B build-tn -G Ninja -DCMAKE_PREFIX_PATH=<Qt6>
cmake --build build-tn && build-tn/bin/timenormalizer_test.exe

# 2) 端到端单发（用真实系统时间，把模型输出的跨度直接串通）
build-tn/bin/timenormalizer_test.exe --normalize "30分钟" "明天早上7点" "今"
#   now = 2026-10-10 21:32:36 Saturday
#   30分钟 -> 1800
#   明天早上7点 -> 34043
#   今 -> <空>

# 3) rasa 侧（CWD 必须是 Data/rasa：model_weights 是相对路径）
cd Data/rasa
for f in sft_holdout probe_unseen probe_delay; do
  D:/libs/python310/python.exe ../../tools/nlu_llm_finetune/eval_rasa.py \
      --model models_v2/nlu-s42-v2.tar.gz \
      --eval ../../tools/nlu_llm_finetune/data/$f.jsonl \
      --dump-json  ../../tools/nlu_llm_finetune/output/rasa_v2_$f.json
done
# 尺子缺陷量化（默认关，见 §5.3）
D:/libs/python310/python.exe ../../tools/nlu_llm_finetune/eval_rasa.py \
    --model models_v2/nlu-s42-v2.tar.gz --prefer-synonym \
    --eval ../../tools/nlu_llm_finetune/data/sft_holdout.jsonl \
    --dump-json ../../tools/nlu_llm_finetune/output/rasa_v2_ps_metrics.json

# 4) LLM 侧（Python 3.12 + ROCm）。★ 必须带 --timing gen，
#    否则与 v2 的数字不同口径
cd ../..
P=D:/libs/python312/python.exe
for f in sft_holdout probe_unseen probe_delay; do
  $P tools/nlu_llm_finetune/eval_lora.py --backend hf \
     --base ./models/qwen3.5-2b --adapter tools/nlu_llm_finetune/output/nlu-lora-v2 \
     --timing gen --eval tools/nlu_llm_finetune/data/$f.jsonl \
     --dump-json tools/nlu_llm_finetune/output/hf_v2_$f.json
done

# 5) 汇总三张表
$P tools/nlu_llm_finetune/summarize.py            # 留出集
$P tools/nlu_llm_finetune/summarize.py --probe    # 泛化探针
$P tools/nlu_llm_finetune/summarize.py --delay    # 延时探针
```

> 训练侧命令见 `doc/nlu_llm_results.md`；rasa 的 `--seed` 与 `random_seed` 陷阱  
> 见 `doc/rasa_vs_llm_nlu.md` §4。

## 6. 配置与发布侧

> 📌 **2026-10 更新（配置迁移为 YAML + NLU 后端换成 ONNX）**
> 本节记录的是**当时**（2026-10-10）的状态，其中提到的 `Data/default_config.json`
> / `config.json` 现已迁移为 `Data/default_config.yaml` / `config.yaml`；
> 第 6.1c 节「运行期 NLU 后端已钉到 rasa」也已被**取代** —— 运行时默认后端改为
> **自研 ONNX NLU**（`nlu.backend = "onnx"`），rasa 退居兜底。
> 本文其余结论（尤其 6.1 的 `useAssistant` 分支行为、超时归一化算法）仍然有效，
> 只是文件扩展名要按 YAML 理解。配置格式细节见 [config.md](config.md)。

### 6.1 ✅ `OPEN_FURNITURE` 的 `useAssistant`（已修）

`Data/default_config.yaml` 里热水器那条原本是：

```json
{ "intent": "OPEN_FURNITURE",
  "useAssistant": true,
  "path": "/api/services/script/turn_on",
  "params": { "entity_id": "script.unknown_3",
              "variables": { "duration": "{time|--}" } } }
```

> 上面保留 JSON 形态便于对照当时的排查过程；迁移到 YAML 后同一段内容为
> ```yaml
> intent: OPEN_FURNITURE
> useAssistant: false
> path: "/api/services/script/turn_on"
> params:
>   entity_id: "script.unknown_3"
>   variables:
>     duration: "{time|--}"
> ```

`params` 写对了，但 `Plugins/hass/hass.cpp:243` 是**二选一**分支：

```cpp
if (service.useAssistant) {
    message["type"] = "assist_pipeline/run";   // ← 走这条，params 被整包丢弃
    message["input"]["text"] = text;            //   把原始文本丢给 HA 自己的 NLU
} else if (pathParts[2] == "services") {
    message["type"] = "call_service";
    message["service_data"] = params;           // ← 只有这条才会带上 duration
}
```

`Hass::handle()` 第 18 行确实调用了 `parseParams()`，算出来的 `duration`  
也确实是 `1800`（`parseValue()` 把 `{time|--}` 换成槽位值），  
但在 `useAssistant` 分支里**没人用它**。

**已改为 `"useAssistant": false`**（全文件仅此一处 `useAssistant`，改动无歧义；  
改完 `json.load` 校验通过）。改完之后发出的报文是：

```json
{ "type": "call_service", "domain": "script", "service": "turn_on",
  "service_data": { "entity_id": "script.unknown_3",
                    "variables": { "duration": "1800" } } }
```

> ⚠ `duration` 是**字符串** `"1800"`（`Hass::parseValue()` 返回 `QString`，  
> `QJsonObject::insert` 按字符串落盘）。HA 侧脚本模板里要写成  
> `{{ duration | int(0) }}` 才能当秒数用，直接 `delay: {seconds: duration}` 会报类型错。

> ⚠ 这是 `Data/default_config.yaml`（模板）。运行期实际读的是  
> `~/.config/QSmartAssistant/Data/config.yaml`，**若该文件已存在，需要把这行同步过去，  
> 或者备份后重新拷贝模板**，否则改动不会生效。

### 6.1b ✅ 发布模型已换成 v2

`Data/default_config.yaml` 的 `rasa.model` 指向 `rasa/models/default.tar.gz`。  
原先那份是 21:02 的 v1（语料里**没有**延时样本 → 抽不出 `d` 槽）。  
现已替换：

| 文件                                               | sha256 前 16 位      | 大小         | 说明                             |
| ------------------------------------------------ | ------------------ | ---------- | ------------------------------ |
| `Data/rasa/models/default.tar.gz`                | `e669d975242fd36d` | 29 640 952 | **= `nlu-s42-v2.tar.gz`，当前生效** |
| `Data/rasa/models/default_v1_nlu-s42.tar.gz`     | `62bf375cb13b1bd5` | 29 548 760 | 回滚用                            |
| `Data/rasa/models/_orig_default_pretrain.tar.gz` | —                  | 28 049 521 | 更早的原始预训练备份                     |

### 6.1c ✅ 运行期 NLU 后端已钉到 **rasa**

`Conversation/conversation.cpp:48` 起是二选一：

```cpp
const QString nluBackend = Config::instance()->getConfig("nlu").value("backend").toString("llm");
if (nluBackend == "llm") {
    auto *llmNlu = new LLMNLU(this);
    if (llmNlu->isValid()) { delete nlu; nlu = llmNlu; }   // 用 LLM
    // isValid() 失败（后端没配/服务不通）则保留 RasaNLU
}
```

原先 `nlu.backend = "llm"` 且 `llm.default = "qwen3.5:2b"` 指向 **ollama**，
于是「跑的是哪个模型」取决于 ollama 起没起 —— 不可控。
**已改成 `"backend": "rasa"`**，直接构造 `RasaNLU`，不再有这层不确定性。

#### 连带修掉的一个必踩点：`rasa.python_venv`

`RasaNLU::startRasa()` 需要 `python_venv` 指向一个**含 `Scripts\rasa.exe` 的目录**
（或直接指向 `rasa.exe`），否则 `qCritical "rasa executable not found"` 后**直接 return**，
服务永远起不来、NLU 全哑。原配置里是占位符 `"absolute path to activate"` ——
以前 backend 是 `llm` 时这条路径很少被走到，钉到 rasa 之后就变成**硬依赖**了。

已填 `"D:/libs/python310"`（该目录下 `Scripts/rasa.exe` 存在，实测可执行）。

#### ⚠ 运行期目录是**独立副本**，不是软链

> 📌 2026-10 起配置为 YAML：`default_config.yaml` → `config.yaml`；
> 「只在不存在时才生成」的规则不变，且**只有老 json 时也会自动迁移**出 yaml。

CMake 构建时会把 `Data/` **拷贝**到 `~/.config/QSmartAssistant/Data/`
（`scripts/build_windows.ps1` 只在 `config.yaml` **不存在**（且没有老 `config.json`）
时才从 `default_config.yaml` 生成它）。
所以：

| 文件 | 谁生效 |
|---|---|
| `~/.config/QSmartAssistant/Data/config.yaml` | ✅ **应用真正读的** |
| 仓库 `Data/default_config.yaml` | 仅作模板 / 下次构建的拷贝源 |
| 仓库 `Data/rasa/…` | 构建时才同步过去 |

本轮（原 2026-10-10 那次）已把三处**都**同步（改前先备份 `config.json.bak-20261010`）：
运行期 `config.json`（外科式改 3 个值，其余保持原样）、
`rasa/models/default.tar.gz` ← v2、`default_config.json` ← 模板。
（当时仍为 JSON；同日之后的 YAML 迁移中 `default_config.yaml` / `config.yaml`
继承了同一套内容与同一套「三处同步」原则。）
（同日之后的 YAML 迁移里，同样的三处同步原则再次适用。）

#### ✅ 端到端实测（真的把服务拉起来了）

```bash
cd ~/.config/QSmartAssistant/Data/rasa
D:/libs/python310/Scripts/rasa.exe run --enable-api -m models/default.tar.gz
# 35 s 后 /status 返回 model_file = default.tar.gz
```

| 输入 | 意图 | 抽出实体 |
|---|---|---|
| 30分钟后开启热水器 | OPEN_FURNITURE | `time=30分钟`、`furniture=热水器` |
| 半小时后把空调打开 | OPEN_FURNITURE | `time=半小时`、`furniture=空调` |
| 明天早上7点打开热水器 | OPEN_FURNITURE | `time=明天早上7点`（**完整跨度，不再被截断**） |
| 两个小时后关掉电风扇 | CLOSE_FURNITURE | `time=两个小时`、`furniture=电风扇` |
| 帮我把灯开一下（对照） | OPEN_FURNITURE | `furniture=灯`，**无 time**（不幻觉） |
| 明后天天气如何 | Weather | `time=明后天` |

把这条和 §4.1 的归一化连起来，链路就是完整的：

```
rasa: time=30分钟  →  entity2Slot()+TimeNormalizer: "1800"
     →  hass parseValue("{time|--}"): "1800"
     →  service_data.variables.duration = "1800"
```

#### 若以后还想用 LLM 后端

得先把 v2 adapter 导成 GGUF 并注册进 ollama，
**不能**只指望 ollama 里那个未微调的 `qwen3.5:2b`：

```bash
cd tools/nlu_llm_finetune
D:/libs/python312/python.exe export_gguf.py \
    --base ./models/qwen3.5-2b --adapter output/nlu-lora-v2 \
    --quant Q4_K_M --model-name qsa-nlu-v2
# 会用 llama.cpp 的 convert_hf_to_gguf.py 合并+转换，并生成带正确 chat template 的
# ollama Modelfile（这一步极易出错：模板不对会让准确率直接崩）
# 然后把 nlu.backend 改回 "llm_nlu" 侧的名字 —— 见 conversation.cpp 的分支
```

**这一步本轮没做**（需要 llama.cpp 源码树 + 起 ollama）。

### 6.2 `CLOSE_FURNITURE` 的 `{number|--}`

`Data/default_config.yaml` 里 `CLOSE_FURNITURE` 用的是 `{number|--}` 而  
非 `{time|--}`，且指向 `switch.turn_off`（该服务不接受延时参数）。  
语义上应统一成 `{time|--}` 并指向一个 HA 脚本；**这次刻意没改**，  
因为它等价于改动 HA 接线，属于用户侧决定。

### 6.3 `Weather` 插件仍按 `yyyy-MM-dd` 解析

拿到秒偏移后行为与改前一致（静默当今天）。要真正修好它，  
只需在 `Weather::handle()` 里加一个「纯整数即秒偏移」的分支，  
用 `currentDateTime().addSecs(n)` 反推绝对时间，约 25 行。
