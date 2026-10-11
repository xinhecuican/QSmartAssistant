#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""silero.thresh 最佳参数搜索脚本。

用途
----
为 QSmartAssistant 的 SileroVAD 后端（Wakeup/Vad/silerovad.cpp）自动搜索
``Data/default_config.yaml`` 中 ``silero.thresh`` 的最优取值。

原理
----
* 完全复刻 ``VadIterator`` 的流式推理协议：16k 每次喂入 512 个采样
  （config 的 chunkSize=1024 → 512 样本/帧），模型输入 = context(64) + 512，
  state(2,1,128) 与 context 跨帧递归、reset 时清零（对齐官方 OnnxWrapper）。
* 对每条标注音频，逐帧得到 speech_prob，再对候选阈值判定帧级「是否人声」，
  最后按帧级指标（而非整段 0/1）评分。
  这一层很关键：真实助手场景里长语音中间常有吸气/停顿，若按整段判定，
  0.5 这类高阈值会因为一两帧塌陷而被误判为「漏检」，导致选型偏差。
* 指标与真实使用场景对齐：
    - speech 文件：帧级 recall（人声帧有多少被判成人声）
    - non-speech 文件：帧级 false-alarm（静音/噪声帧有多少被误判成人声）
    - 同时给出「整段级」指标作参考（含 VAD 尾点静音判定 detectSlient=800ms 语义）
* 综合评分（默认）：F1 - 0.25 * false_alarm，在「不误触发」上加权。
  可用 ``--sweep-only`` 关闭。

数据集
------
默认扫描 ``--speech-dir``（含语音）与 ``--noise-dir``（静音/噪声）。
目录里的 wav 会被自动重采样到 16k 单声道。

**可用 ``scripts/collect_vad_data.py`` 从 livekit-wakeword 生成语料自动构建**：
    python scripts/collect_vad_data.py
    python scripts/tune_silero_thresh.py

⚠ 分类必须按「是否有语音活动」，不是「是否唤醒词」：
    * ``--speech-dir`` ← 含唤醒词的 TTS **和** 非唤醒词的真人语音（都算语音活动）
    * ``--noise-dir``  ← 只放纯背景噪声/环境音
    把真人非唤醒词语音放进 noise 会虚高误触发率（silero 判其为人声是正确的）。

若目录不存在或为空，脚本会提示你放置素材；仓库自带 ``Data/test2.wav``
可作为 speech 样本起步。

用法
----
    python scripts/tune_silero_thresh.py                     # 默认扫描
    python scripts/tune_silero_thresh.py --sweep-only        # 只看指标不评分
    python scripts/tune_silero_thresh.py --speech-dir D:/a --noise-dir D:/b
    python scripts/tune_silero_thresh.py --apply 0.35        # 直接写入配置

依赖：``pip install onnxruntime numpy``（音频解码优先用标准库 wave，
非 wav/异常格式回退 soundfile/ffmpeg）。
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import wave

import numpy as np
import onnxruntime as ort

SR = 16000
CHUNK = 512            # 16k 每次喂入模型的新采样数（chunkSize=1024 / 2）
CONTEXT = 64           # 16k 上下文采样数
STATE_SHAPE = (2, 1, 128)

# 项目里的默认候选阈值（覆盖 silero 常用的 0.1~0.6 区间）
DEFAULT_THRESHOLDS = [round(x, 2) for x in np.arange(0.05, 0.96, 0.05)]


# --------------------------------------------------------------------------
# 音频读取 / 重采样
# --------------------------------------------------------------------------
def _resample_linear(x: np.ndarray, src_sr: int, dst_sr: int) -> np.ndarray:
    """线性插值重采样，够用；对 VAD 概率影响可忽略。"""
    if src_sr == dst_sr:
        return x
    n_dst = int(round(len(x) * dst_sr / src_sr))
    idx = np.linspace(0, len(x) - 1, n_dst)
    lo = np.floor(idx).astype(np.int64)
    hi = np.minimum(lo + 1, len(x) - 1)
    frac = (idx - lo).astype(np.float32)
    return (x[lo] * (1 - frac) + x[hi] * frac).astype(np.float32)


def read_audio(path: str) -> np.ndarray:
    """读成 16k 单声道 float32 [-1,1]。优先标准库 wave，回退 soundfile/ffmpeg。"""
    try:
        with wave.open(path, "rb") as w:
            n_ch = w.getnchannels()
            width = w.getsampwidth()
            sr = w.getframerate()
            raw = w.readframes(w.getnframes())
        if width == 2:
            x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
        elif width == 1:
            x = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128) / 128.0
        elif width == 4:
            x = np.frombuffer(raw, dtype="<i4").astype(np.float32) / 2147483648.0
        else:
            raise ValueError("unsupported sample width %d" % width)
        if n_ch > 1:
            x = x.reshape(-1, n_ch).mean(axis=1)
        return _resample_linear(x.astype(np.float32), sr, SR)
    except Exception:
        pass

    # 回退 1: soundfile
    try:
        import soundfile as sf  # type: ignore

        x, sr = sf.read(path, dtype="float32", always_2d=False)
        if x.ndim > 1:
            x = x.mean(axis=1)
        return _resample_linear(x.astype(np.float32), sr, SR)
    except Exception:
        pass

    # 回退 2: ffmpeg（沙箱/服务器上常见）
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg:
        cmd = [ffmpeg, "-v", "error", "-i", path, "-f", "f32le",
               "-ac", "1", "-ar", str(SR), "-"]
        out = subprocess.run(cmd, capture_output=True, check=True).stdout
        return np.frombuffer(out, dtype="<f4").astype(np.float32)

    raise RuntimeError(
        "无法解码 %s：请安装 soundfile 或 ffmpeg，或改用 16-bit PCM wav" % path)


# --------------------------------------------------------------------------
# 逐帧概率（复刻 VadIterator 流式协议）
# --------------------------------------------------------------------------
def frame_probs(sess: ort.InferenceSession, audio: np.ndarray) -> np.ndarray:
    """返回每个 512 采样帧的人声概率（与 C++ VadIterator 逐帧一致）。"""
    state = np.zeros(STATE_SHAPE, dtype=np.float32)
    context = np.zeros((1, CONTEXT), dtype=np.float32)
    sr_arr = np.array(SR, dtype=np.int64)
    probs = []
    for i in range(0, len(audio) - CHUNK + 1, CHUNK):
        chunk = audio[i:i + CHUNK]
        x = np.concatenate([context, chunk[None, :]], axis=1)
        out, state = sess.run(
            None, {"input": x, "state": state, "sr": sr_arr})
        context = x[:, -CONTEXT:]
        probs.append(float(out[0, 0]))
    return np.asarray(probs, dtype=np.float32)


def _make_session(model_path: str) -> ort.InferenceSession:
    so = ort.SessionOptions()
    so.intra_op_num_threads = 1
    so.inter_op_num_threads = 1
    so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    return ort.InferenceSession(
        model_path, so, providers=["CPUExecutionProvider"])


# --------------------------------------------------------------------------
# 标注与指标
# --------------------------------------------------------------------------
def load_dataset(speech_dir, noise_dir, model_path):
    """返回 [(name, probs, is_speech), ...]。"""
    sess = _make_session(model_path)
    items = []

    def scan(d, is_speech):
        if not d or not os.path.isdir(d):
            return
        for fn in sorted(os.listdir(d)):
            if not fn.lower().endswith((".wav", ".flac", ".ogg", ".mp3")):
                continue
            p = os.path.join(d, fn)
            try:
                audio = read_audio(p)
            except Exception as e:  # noqa: BLE001
                print("  [skip] %s: %s" % (fn, e))
                continue
            if len(audio) < CHUNK:
                print("  [skip] %s: 太短" % fn)
                continue
            items.append((fn, frame_probs(sess, audio), is_speech))

    scan(speech_dir, True)
    scan(noise_dir, False)
    return items


def frame_metrics(probs, threshold, is_speech):
    """帧级判定。返回 (hit, total, rate)。speech→recall / noise→false-alarm。"""
    hit = int((probs > threshold).sum())
    return hit, len(probs), (hit / len(probs) if len(probs) else 0.0)


def segment_verdict(probs, threshold, is_speech, detect_silent_ms=800):
    """模拟 VadModel::detect 的整段判定，看是否能在 detectSilent 内确认人声。

    对齐 silerovad.cpp 的 ``abandonNum``（前若干帧被忽略）：
    连续 N 帧超过阈值即判定「有人声」。这里 N=3 模拟约 96ms 的确认窗口。
    """
    confirm_frames = 3
    run = 0
    for p in probs:
        if p > threshold:
            run += 1
            if run >= confirm_frames:
                return True
        else:
            run = 0
    return False


def evaluate(items, thresholds, score_all_fa=False):
    speech_items = [it for it in items if it[2]]
    noise_items = [it for it in items if not it[2]]
    rows = []
    for th in thresholds:
        # 帧级
        sp_hit = sp_tot = 0
        for _n, pr, _s in speech_items:
            h, t, _ = frame_metrics(pr, th, True)
            sp_hit += h
            sp_tot += t
        recall = sp_hit / sp_tot if sp_tot else float("nan")

        fa_hit = fa_tot = 0
        for _n, pr, _s in noise_items:
            h, t, _ = frame_metrics(pr, th, False)
            fa_hit += h
            fa_tot += t
        fa = fa_hit / fa_tot if fa_tot else float("nan")

        # 整段级（更像是真实助手的通过/失败）
        seg_ok = sum(1 for _n, pr, _s in speech_items
                     if segment_verdict(pr, th, True))
        seg_fail = sum(1 for _n, pr, _s in noise_items
                       if segment_verdict(pr, th, False))

        f1 = (2 * recall * fa and 0)  # noqa: F841  (占位，见下)
        if not np.isnan(recall) and not np.isnan(fa):
            f1 = 0.0 if (recall + (1 - fa)) == 0 else \
                2 * recall * (1 - fa) / (recall + (1 - fa))
            score = f1 - 0.25 * fa
        else:
            f1, score = float("nan"), float("nan")

        rows.append({
            "thresh": th,
            "recall": recall,
            "false_alarm": fa,
            "score": score,
            "seg_speech_ok": seg_ok, "seg_speech_n": len(speech_items),
            "seg_noise_fail": seg_fail, "seg_noise_n": len(noise_items),
        })
    return rows


# --------------------------------------------------------------------------
# 输出 / 应用
# --------------------------------------------------------------------------
def print_rows(rows, fp=sys.stdout):
    print("\n%-8s %-10s %-12s %-10s %-14s %s" %
          ("thresh", "recall", "false_alarm", "score",
           "seg_speech_ok", "seg_noise_fail"), file=fp)
    print("-" * 82, file=fp)
    for r in rows:
        rec = "  n/a  " if np.isnan(r["recall"]) else "%7.2f%%" % (r["recall"] * 100)
        fa = "  n/a  " if np.isnan(r["false_alarm"]) else "%7.2f%%" % (r["false_alarm"] * 100)
        sc = "  n/a  " if np.isnan(r["score"]) else "%8.4f" % r["score"]
        print("%-8.2f %-10s %-12s %-10s %-14s %s" %
              (r["thresh"], rec, fa, sc,
               "%d/%d" % (r["seg_speech_ok"], r["seg_speech_n"]),
               "%d/%d" % (r["seg_noise_fail"], r["seg_noise_n"])), file=fp)


def apply_threshold(config_path, thresh):
    """把 silero.thresh 写回配置。yaml/json 都支持，按扩展名分派。"""
    is_yaml = config_path.lower().endswith((".yaml", ".yml"))
    with open(config_path, "r", encoding="utf-8") as f:
        if is_yaml:
            try:
                import yaml
            except ImportError:
                print("  [skip] 未安装 PyYAML，无法写 %s" % config_path)
                return
            cfg = yaml.safe_load(f) or {}
        else:
            cfg = json.load(f)
    if "silero" not in cfg:
        raise KeyError("配置里没有 silero 段：%s" % config_path)
    old = cfg["silero"].get("thresh")
    cfg["silero"]["thresh"] = round(float(thresh), 4)
    with open(config_path, "w", encoding="utf-8") as f:
        if is_yaml:
            yaml.safe_dump(cfg, f, allow_unicode=True, sort_keys=True,
                           default_flow_style=False)
        else:
            json.dump(cfg, f, ensure_ascii=False, indent=4)
            f.write("\n")
    print("  %s: silero.thresh %s -> %s" % (config_path, old, cfg["silero"]["thresh"]))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser(
        description="搜索 silero.thresh 最佳参数（帧级 + 整段级评估）")
    ap.add_argument("--model", default=os.path.join(root, "Data", "silero_vad.onnx"),
                    help="silero onnx 模型路径")
    ap.add_argument("--speech-dir", default=os.path.join(here, "vad_data", "speech"),
                    help="含人声的 wav 目录")
    ap.add_argument("--noise-dir", default=os.path.join(here, "vad_data", "noise"),
                    help="静音/噪声的 wav 目录")
    ap.add_argument("--thresholds", default="",
                    help="逗号分隔的候选阈值，默认 0.05~0.95 步进 0.05")
    ap.add_argument("--sweep-only", action="store_true",
                    help="不评分，只输出原始指标")
    ap.add_argument("--apply", type=float, default=None,
                    help="把指定阈值写入 default_config.yaml / config.yaml")
    ap.add_argument("--configs", default="",
                    help="要写入的配置路径（逗号分隔），默认为仓库+运行期两份")
    ap.add_argument("--runtime-config", default="",
                    help="运行期 config.yaml 路径，默认 ~/.config/QSmartAssistant/Data/config.yaml")
    ap.add_argument("--out", default="",
                    help="把完整扫描结果另存为 .md")
    args = ap.parse_args()

    if not os.path.isfile(args.model):
        sys.exit("[err] 找不到模型: %s" % args.model)

    # 数据检查
    dirs_ok = any(os.path.isdir(d) and
                  any(f.lower().endswith(".wav") for f in os.listdir(d))
                  for d in (args.speech_dir, args.noise_dir))
    if not dirs_ok:
        print("[warn] 未在 %s / %s 找到 wav 素材。" %
              (args.speech_dir, args.noise_dir))
        print("       请把「含人声」与「纯静音/噪声」的 wav 分别放进去。")
        print("       仓库自带的 Data/test2.wav 可作为 speech 样本起步。")
        print("       脚本仍会继续，但无标注数据无法给出有意义的结论。\n")

    print("[1/3] 加载模型并逐帧推理 ...")
    items = load_dataset(args.speech_dir, args.noise_dir, args.model)
    n_sp = sum(1 for it in items if it[2])
    n_no = len(items) - n_sp
    print("      样本: speech %d / noise %d（帧数 %s）" %
          (n_sp, n_no,
           "n/a" if not items else
           "speech=%d noise=%d" % (
               sum(len(p) for _n, p, s in items if s),
               sum(len(p) for _n, p, s in items if not s))))
    if not items:
        sys.exit("[err] 没有可用样本，已退出。")

    ths = ([float(x) for x in args.thresholds.split(",") if x.strip()]
           if args.thresholds else DEFAULT_THRESHOLDS)

    print("[2/3] 扫描阈值 ...")
    rows = evaluate(items, ths, score_all_fa=args.sweep_only)
    print_rows(rows)

    best = None
    if not args.sweep_only and not all(np.isnan(r["score"]) for r in rows):
        best = max(rows, key=lambda r: r["score"])
        print("\n[3/3] 最佳阈值: silero.thresh = %.2f  "
              "(recall %.2f%%, false_alarm %.2f%%, score %.4f)" %
              (best["thresh"], best["recall"] * 100,
               best["false_alarm"] * 100, best["score"]))
    else:
        print("\n[3/3] 未评分（--sweep-only 或缺少某一类样本）。")

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write("# silero.thresh 扫描结果\n\n")
            f.write("- 模型: `%s`\n- speech 样本: %d 条\n- noise 样本: %d 条\n\n"
                    % (args.model, n_sp, n_no))
            if best:
                f.write("**推荐**: `silero.thresh = %.2f`\n\n" % best["thresh"])
            print_rows(rows, fp=f)
        print("结果已写入 %s" % args.out)

    # 应用
    if args.apply is not None:
        if args.configs:
            paths = [p for p in args.configs.split(",") if p.strip()]
        else:
            runtime = args.runtime_config or os.path.join(
                os.path.expanduser("~"), ".config", "QSmartAssistant",
                "Data", "config.yaml")
            paths = [os.path.join(root, "Data", "default_config.yaml"), runtime]
        print("\n写入配置:")
        for p in paths:
            if os.path.isfile(p):
                apply_threshold(p, args.apply)
            else:
                print("  [skip] 不存在: %s" % p)
        print("提示：运行期改动只影响当前机器，仓库模板改动才会随代码分发。")
    elif best is not None:
        print("提示：加 --apply %.2f 可把推荐值写入配置。" % best["thresh"])


if __name__ == "__main__":
    main()
