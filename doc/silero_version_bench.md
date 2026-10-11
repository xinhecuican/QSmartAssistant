
===== v4.0  (input+h/c(64); 无context) =====
模型 1.81 MB | 推理 866.7 ms/7334帧 | RTF 0.0037
thresh   recall     false_alarm  seg_speech_ok  seg_noise_fail
0.10       79.71%     35.57%     160/160        23/40
0.20       74.90%     22.52%     159/160        19/40
0.30       72.29%     15.29%     158/160        12/40
0.40       70.09%     11.41%     158/160        8/40
0.50       67.98%      9.78%     157/160        7/40

===== v5.0  (input+state(128); context64) =====
模型 2.31 MB | 推理 612.1 ms/7334帧 | RTF 0.0026
thresh   recall     false_alarm  seg_speech_ok  seg_noise_fail
0.10       81.88%      5.17%     159/160        8/40
0.20       78.93%      3.27%     159/160        3/40
0.30       76.16%      2.71%     159/160        3/40
0.40       74.16%      2.15%     159/160        3/40
0.50       72.31%      1.77%     159/160        2/40
  -- 不拼 context（对照）--
  thresh 0.10  recall   76.08%  false_alarm   31.31%
  thresh 0.50  recall   55.77%  false_alarm    5.60%

===== v5.1.2  (input+state(128); context64) =====
模型 2.33 MB | 推理 675.9 ms/7334帧 | RTF 0.0029
thresh   recall     false_alarm  seg_speech_ok  seg_noise_fail
0.10       81.88%      5.17%     159/160        8/40
0.20       78.93%      3.27%     159/160        3/40
0.30       76.16%      2.71%     159/160        3/40
0.40       74.16%      2.15%     159/160        3/40
0.50       72.31%      1.77%     159/160        2/40
  -- 不拼 context（对照）--
  thresh 0.10  recall   76.08%  false_alarm   31.31%
  thresh 0.50  recall   55.77%  false_alarm    5.60%

===== v6.2  (input+state(128); context64) =====
模型 2.33 MB | 推理 663.8 ms/7334帧 | RTF 0.0028
thresh   recall     false_alarm  seg_speech_ok  seg_noise_fail
0.10       81.11%      3.88%     159/160        9/40
0.20       76.50%      1.72%     157/160        3/40
0.30       73.68%      1.03%     157/160        1/40
0.40       71.21%      0.65%     157/160        1/40
0.50       69.55%      0.39%     157/160        1/40
  -- 不拼 context（对照）--
  thresh 0.10  recall    0.70%  false_alarm    1.16%
  thresh 0.50  recall    0.00%  false_alarm    0.00%

---

## 关键结论

### 1. v5.0 与 v5.1.2 数值上完全等价
两者模型文件 hash 不同、体积不同（2,313,101 vs 2,327,524 字节），但在
20 个语音文件上**逐帧概率最大差 = 0.000e+00**（逐位一致），各项指标逐位相同。
差异仅在 ONNX 序列化元数据，**权重完全相同**，无需区分对待。
（v6.2 与 v6.2.3 同样同 hash，是同一份模型。）

### 2. v6.2 相比 v5.x：误触发率显著更低，召回基本持平
同阈值对比（帧级）：

| 阈值 | v5.0/v5.1.2 召回 | v5.0 误触发 | v6.2 召回 | v6.2 误触发 |
|:--:|:--:|:--:|:--:|:--:|
| 0.10 | 81.88% | 5.17% | 81.11% | **3.88%** |
| 0.20 | 78.93% | 3.27% | 76.50% | **1.72%** |
| 0.30 | 76.16% | 2.71% | 73.68% | **1.03%** |
| 0.50 | 72.31% | 1.77% | 69.55% | **0.39%** |

v6.2 的整体 DET 曲线更靠左上：**同等召回下误触发更低**。代价是召回略降
1~3pt。对唤醒词助手场景（误触发代价高），v6.2 更合适。

### 3. v4.0 明显落后
误触发率高达 9.78%~35.57%（同等召回下比 v5 高约 5~7 倍），
且 state 为 h/c 双 64 维、接口不兼容，无继续使用理由。

### 4. ⚠ context 是 v6.2 的硬性要求（重要）
对照实验（不拼接 64 采样 context）：

| 版本 | 不拼 context 召回 | 拼 context 召回 |
|:--|:--:|:--:|
| v5.0 | 76.08% | 81.88% |
| **v6.2** | **0.70%** | **81.11%** |

- v5.0 不拼 context 仍可工作（召回仅降 5.8pt），属「碰巧能用」——这解释了
  为什么项目早期代码（未加 context）能跑起来。
- **v6.2 不拼 context 几乎完全失效（0.70%）**，因为 v6.2 的卷积结构把
  输入序列长度当作时间上下文，缺失 64 采样前缀会导致特征错位。
- 结论：`silerovad.cpp` 中新增的 context 逻辑**不仅为升级，更是修复了
  一个既有隐患**——旧代码配 v5.0 是侥幸，若直接换 v6.2 权重而不改代码，
  VAD 会静默失效。

### 5. 性能与体积
| 版本 | 体积 | RTF（单线程 CPU） |
|:--|:--:|:--:|
| v4.0 | 1.81 MB | 0.0037 |
| v5.0 | 2.31 MB | 0.0026 |
| v5.1.2 | 2.33 MB | 0.0029 |
| v6.2 | 2.33 MB | 0.0028 |

差距可忽略（RTF 均 < 0.004，即 1 秒音频处理 < 4ms）。v6.2 反而略快于 v4.0。

## 复现方式

```bash
# 1) 准备测试集（一次性）
python scripts/collect_vad_data.py

# 2) 下载各版本模型（v4.0 在 files/ 目录，路径与 v5+ 不同）
python - <<'PY'
import urllib.request, os
op = urllib.request.build_opener(urllib.request.ProxyHandler({}))
for tag in ['v4.0','v5.0','v5.1.2','v6.2']:
    path = 'files/silero_vad.onnx' if tag == 'v4.0' else 'src/silero_vad/data/silero_vad.onnx'
    url = f'https://raw.githubusercontent.com/snakers4/silero-vad/{tag}/{path}'
    d = op.open(urllib.request.Request(url, headers={'User-Agent':'curl/8'}), timeout=90).read()
    open(f'scripts/silero_{tag}.onnx','wb').write(d)
PY

# 3) 跑对比
python scripts/bench_silero_versions.py --compare-no-context --out doc/silero_version_bench.md
```

> 沙箱内 localhost 与直连可能被 HTTP_PROXY 劫持，需禁用代理（脚本内已处理）。
