# LLM NLU 微调与评测结果

本文件记录 `NLU_LLM` 后端（Qwen3.5-2B + LoRA）在**手写留出集**上的准确率与延迟，
以及数据管线的坑。流程与命令见 `doc/llm.md`。

## 评测口径

- 留出集 `tools/nlu_llm_finetune/data/sft_holdout.jsonl`：**65 条手写未见句**，
  覆盖 16 个意图。由 `make_holdout.py` 生成，带自动泄漏检查（与 train/eval 逐字比对）。
- **不要用 `sft_eval_aug.jsonl` 当成绩**：它由模板从同源句子扩展而来，属于数据泄漏。
- 硬件：AMD Radeon RX 9070 XT（gfx1201，16 GB），torch 2.13.0+rocm10.0.0，Python 3.12。
- 推理：`--backend hf`，贪心解码，`max_new_tokens=48`。
- 延迟统计前**先预热 3 条**（首次 `generate` 会触发内核/显存池惰性初始化，
  实测首条 1.5 s+），否则 P50 直接被带偏。`eval_lora.run_hf` 的 `--warmup` 默认 3。

### ⚠ 评测延迟必须显式传 eos

Qwen3.5 的两个 eos **不是同一个 token**：

| 来源 | id | token |
|---|---|---|
| `tokenizer.eos_token_id`（chat template 用） | **248046** | `<|im_end|>` |
| `generation_config.eos_token_id`（`generate()` 默认读） | **248044** | 另一个 |

`model.generate()` 不显式传 `eos_token_id` 就会等一个模型永远产不出的 token，
于是**每次都硬生成满 `max_new_tokens`**：

| | 平均新 token | EOS 命中 | P50 |
|---|---|---|---|
| 不传 eos | 48（跑满） | 0/12 | 428 – 1752 ms |
| 传 `eos_token_id=tok.eos_token_id` | 2.2 | 12/12 | **147 ms** |

跑满时模型还会接着"续写对话"（`11|f=灯\nuser\n客听的灯开一下\n<think>…`），
**连准确率一起污染**。`eval_lora.py` 的 `run_hf` 已修。

## 结果（65 条手写留出集，三版统一用修好的评测器）

| # | 训练数据 / 适配器 | 意图准确率 | 完全一致率 | 槽位 P / R / F1 | 延迟 P50 / P90 |
|---|---|---|---|---|---|
| ① | 修复前 `output/_stale_prefix_run` | **100.00%** (65/65) | 81.54% (53/65) | 0.667 / 0.722 / 0.693 | 192 / 445 ms |
| ② | 中途版（4 个隐藏 bug 仍在）`output/_regression_run` | 92.31% (60/65) | 72.31% (47/65) | 0.607 / 0.679 / 0.641 | 200 / 443 ms |
| ③ | **当前 `output/nlu-lora`** | 98.46% (64/65) | **93.85%** (61/65) | **0.893 / 0.875 / 0.884** | **200 / 395 ms** |

> ③ 的延迟是**加预热后**重测值。三次独立复测 P50 = 200 / 205 / 208 ms、
> P90 = 393 / 404 / 438 ms；准确率三次完全一致（无采样随机性）。
> 早先记的 177/361 ms 是未预热时的单次值，已被本轮取代。
> 另：`--timing e2e`（把 chat template + 分词也算进去）只比纯 `generate` 多 1 ms，
> 说明预处理开销可忽略，两边延迟口径没有争议空间。

**③ 相对 ①**：exact **+12.3 pt**，槽位 F1 **0.693 → 0.884**（+27.6% 相对），
P90 延迟 **−19%**。唯一退步的是意图准确率少 1 条（`西红柿炒鸡蛋` 判成 `11|f=`）。

> ① 的意图满分带着水分：修复前数据里 `location`/`singer` 实体被整批丢弃，
> 模型**根本不会输出 `l=` / `g=`**，所以不可能标错槽位——代价是 ① 的槽位 F1
> 只有 0.693，凡是需要房间或歌手的句子全错。③ 是"真的会标注义槽位"的成绩。

**② 说明了数据 bug 的代价**：同样是"位置实体已恢复"的数据，因为模板改说法却
照抄原句标签（14% 样本文本与标签矛盾）、`g` 实体名写错导致多槽位模板全被跳过、
同音块不管意图都生成「打开/关掉」、`t` 有 6 种写法，exact 只有 72.31%，
**全面低于修复前基线**。同样的模型、同样的超参，只修数据就换来 +21.5 pt。

训练记录（③）：641 条训练样本 / 492 步 / 23 分 21 秒 / `train_loss` 0.0180 /
`eval_loss` 0.0456 / 峰值显存 allocated **7.76 GB**（16 GB 卡）。

## 剩余 4 个错例

| 输入 | 期望 | 实际 | 原因 |
|---|---|---|---|
| 客听的灯开一下 | `11|f=灯,l=客厅` | `11|f=灯` | 训练集无「灯 × 客厅」组合：同音错字块只对 `HOMOPHONE_VARIANTS` 里有的家具生效，而「客听」只能从 l=客厅 的源句派生，那些源句的家具恰好是风扇。修法：补「家具 × 房间」笛卡尔积样本（文本与标签同时含两者，零编造） |
| 有人在家吗 | `8` | `8|l=家` | 标注口径问题——「家」算不算 `location` 需要在 `rasa/data/nlu/nlu.yml` 定下来 |
| 外面下雨了吗 | `15` | `15|c=外面` | 模板 `{city}下雨吗` 过度泛化，把「外面」当城市 |
| 西红柿炒鸡蛋 | `0` | `11|f=西红柿炒鸡蛋` | FALLBACK 仅 34 条且刻意不扩增，噪声覆盖不足 |

## 数据管线（改模板后必看自检）

`augment_dataset.py` 末尾会做**标签自检**：标签里的实体值必须能在文本里找到
（`t` / `n` 除外 —— `t` 是归一化语义值、`n` 有中文与阿拉伯两种写法）。
这条不变式能一次抓出「模板改了说法却照抄原句槽位」造成的全部脏样本。

生成顺序必须是 **`build_dataset.py` → `augment_dataset.py` → `make_holdout.py`**
（泄漏检查要看到最终的扩增集才有意义）。

已知必须遵守的三条：

1. **文本里表达不出来的实体，标签里就不能有**。模板 `"把{f}开一下"` 没有 `{l}`，
   就不能继承 `l=卧室`。此前 507 条里 69 条（14%）栽在这里，模型因此学会
   凭空编房间、编歌手。
2. **实体名按 schema 查**：`Data/nlu_schema.json` 的 `entities` 是 `{码: 名}`，
   `g` 的名字是 `singer` 不是 `human`。写错会让 MUSICINFO 多槽位模板被静默跳过。
3. **`t` 统一为 `大` / `小`**（schema `hints` 明文规定）。rasa 原文标的是原样跨度
   （增大/减小/调大/调小/大/小 六种），已在 `build_dataset.format_target` 收敛。

## 复现

```bash
cd tools/nlu_llm_finetune
D:/libs/python312/python.exe build_dataset.py
D:/libs/python312/python.exe augment_dataset.py --no-llm
D:/libs/python312/python.exe make_holdout.py

cd ../..
HF_HUB_OFFLINE=1 D:/libs/python312/python.exe tools/nlu_llm_finetune/train_lora_native.py \
    --base ./models/qwen3.5-2b \
    --train tools/nlu_llm_finetune/data/sft_train_aug.jsonl \
    --eval  tools/nlu_llm_finetune/data/sft_eval_aug.jsonl \
    --epochs 12 --batch-size 4 --grad-accum 4 \
    --lora-r 32 --lora-alpha 64 --max-seq-len 640

HF_HUB_OFFLINE=1 D:/libs/python312/python.exe tools/nlu_llm_finetune/eval_lora.py \
    --backend hf --base ./models/qwen3.5-2b \
    --adapter tools/nlu_llm_finetune/output/nlu-lora \
    --eval tools/nlu_llm_finetune/data/sft_holdout.jsonl
```

---

## 与 rasa 的横向对比

本文件只覆盖 LLM 侧。rasa 侧的对照（含改动斜坡、受控 A/B、随机种子坑、未见实体探针）
见 **`doc/rasa_vs_llm_nlu.md`**（可视化版 `doc/rasa_vs_llm_nlu.html`）。

需要同步记住的两点：

- **`eval_lora.report()` 是唯一的指标实现**，`eval_rasa.py` 也调它，两边数字一定同口径。
  改指标实现时两边一起变，不要各写一套。
- **延迟必须先预热**（`--warmup 3`，默认开）；**eos 必须显式传**。
  这两条是 LLM 侧评测的两个前提，rasa 侧对应的是「必须钉 `random_seed`」。
