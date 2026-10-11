#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""silero-vad 不同版本性能对比基准。

对比 v4.0 / v5.0 / v5.1.2 / v6.2 在当前 VAD 测试集上的表现。

各版本推理协议（关键差异）
-------------------------
  v4.0    input(512) + sr + h(2,1,64) + c(2,1,64)  -> output, hn, cn
          * 无 context；state 拆成 h/c 两个 64 维张量
  v5.0    input(512) + state(2,1,128) + sr          -> output, stateN
          * 需要 context(64) 前置拼接（v5.0 起引入）
  v5.1.2  同上（权重更新）
  v6.2    同上（权重更新）
          ⚠ v6.2 **强烈依赖 context**：不拼 context 时概率几乎恒为 0（完全失效）；
            v5.0 不拼 context 仍可工作（概率略降），属「碰巧能用」。

用法
----
    python scripts/bench_silero_versions.py                 # 用 scripts/vad_data
    python scripts/bench_silero_versions.py --speech-dir A --noise-dir B
    python scripts/bench_silero_versions.py --thresholds 0.05,0.1,0.2,0.5
    python scripts/bench_silero_versions.py --models-dir /tmp --out report.md

说明：v4.0 的模型不在 v5+ 的仓库路径下（在 `files/` 目录），需单独下载：
    https://raw.githubusercontent.com/snakers4/silero-vad/v4.0/files/silero_vad.onnx
"""

import argparse
import json
import os
import sys
import time
import wave

import numpy as np
import onnxruntime as ort

SR = 16000


# --------------------------------------------------------------------------
# 音频
# --------------------------------------------------------------------------
def read_wav(path):
    with wave.open(path, "rb") as w:
        sr, n_ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(w.getnframes())
    if width == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 4:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float32) / 2147483648.0
    else:
        raise ValueError("unsupported width %d" % width)
    if n_ch > 1:
        x = x.reshape(-1, n_ch).mean(axis=1)
    if sr != SR:
        n_dst = int(round(len(x) * SR / sr))
        idx = np.linspace(0, len(x) - 1, n_dst)
        lo = np.floor(idx).astype(np.int64)
        hi = np.minimum(lo + 1, len(x) - 1)
        f = (idx - lo).astype(np.float32)
        x = x[lo] * (1 - f) + x[hi] * f
    return x.astype(np.float32)


# --------------------------------------------------------------------------
# 各版本推理
# --------------------------------------------------------------------------
def _sess(path):
    so = ort.SessionOptions()
    so.intra_op_num_threads = 1
    so.inter_op_num_threads = 1
    so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    return ort.InferenceSession(path, so, providers=["CPUExecutionProvider"])


def probs_v4(sess, x, chunk=512):
    """v4: h/c 各 (2,1,64)，无 context。"""
    h = np.zeros((2, 1, 64), dtype=np.float32)
    c = np.zeros((2, 1, 64), dtype=np.float32)
    sr = np.array(SR, dtype=np.int64)
    out = []
    for i in range(0, len(x) - chunk + 1, chunk):
        inp = x[i:i + chunk][None, :].astype(np.float32)
        o, h, c = sess.run(None, {"input": inp, "h": h, "c": c, "sr": sr})
        out.append(float(o[0, 0]))
    return np.asarray(out, dtype=np.float32)


def probs_v5(sess, x, chunk=512, context_size=64, use_context=True):
    """v5.0/v5.1.2/v6.2: state (2,1,128) + 可选 context 拼接。"""
    st = np.zeros((2, 1, 128), dtype=np.float32)
    ctx = np.zeros((1, context_size), dtype=np.float32)
    sr = np.array(SR, dtype=np.int64)
    out = []
    for i in range(0, len(x) - chunk + 1, chunk):
        c = x[i:i + chunk]
        if use_context:
            inp = np.concatenate([ctx, c[None, :]], axis=1)
            ctx = inp[:, -context_size:]
        else:
            inp = c[None, :].astype(np.float32)
        o, st = sess.run(None, {"input": inp, "state": st, "sr": sr})
        out.append(float(o[0, 0]))
    return np.asarray(out, dtype=np.float32)


VERSIONS = {
    "v4.0":   {"fn": probs_v4, "ctx": False, "desc": "input+h/c(64); 无context"},
    "v5.0":   {"fn": probs_v5, "ctx": True,  "desc": "input+state(128); context64"},
    "v5.1.2": {"fn": probs_v5, "ctx": True,  "desc": "input+state(128); context64"},
    "v6.2":   {"fn": probs_v5, "ctx": True,  "desc": "input+state(128); context64"},
}


# --------------------------------------------------------------------------
# 评估
# --------------------------------------------------------------------------
def infer_all(model_path, ver, files, use_context=None):
    fn = VERSIONS[ver]["fn"]
    use_ctx = VERSIONS[ver]["ctx"] if use_context is None else use_context
    sess = _sess(model_path)
    res = {}
    t0 = time.perf_counter()
    total_frames = 0
    for f in files:
        x = read_wav(f)
        if ver == "v4.0":
            p = fn(sess, x)
        else:
            p = fn(sess, x, use_context=use_ctx)
        res[os.path.basename(f)] = p
        total_frames += len(p)
    dt = time.perf_counter() - t0
    return res, dt, total_frames


def metrics(probs, th):
    """返回 (recall, false_alarm, seg_ok, seg_fail, hit, sp_tot, fa_hit, fa_tot)"""
    sp_hit = sp_tot = fa_hit = fa_tot = 0
    seg_ok = seg_fail = 0
    for pr, is_sp in probs:
        hit = int((pr > th).sum())
        # 整段判定：连续 3 帧超阈值即认为有语音（近似 abandonNum=5 / 96ms）
        run = 0
        seg = False
        for v in pr:
            if v > th:
                run += 1
                if run >= 3:
                    seg = True
                    break
            else:
                run = 0
        if is_sp:
            sp_hit += hit
            sp_tot += len(pr)
            seg_ok += int(seg)
        else:
            fa_hit += hit
            fa_tot += len(pr)
            seg_fail += int(seg)
    rec = sp_hit / sp_tot if sp_tot else float("nan")
    fa = fa_hit / fa_tot if fa_tot else float("nan")
    return rec, fa, seg_ok, seg_fail, sp_hit, sp_tot, fa_hit, fa_tot


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser(description="silero-vad 版本性能对比")
    ap.add_argument("--models-dir", default=here,
                    help="存放 silero_<ver>.onnx 的目录")
    ap.add_argument("--speech-dir", default=os.path.join(here, "vad_data", "speech"))
    ap.add_argument("--noise-dir", default=os.path.join(here, "vad_data", "noise"))
    ap.add_argument("--thresholds", default="0.1,0.2,0.3,0.4,0.5")
    ap.add_argument("--versions", default="v4.0,v5.0,v5.1.2,v6.2")
    ap.add_argument("--compare-no-context", action="store_true",
                    help="额外测 v5+/v6.2 在『不拼 context』下的表现")
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    ths = [float(t) for t in args.thresholds.split(",") if t.strip()]
    vers = [v.strip() for v in args.versions.split(",") if v.strip()]

    def collect(d, is_sp):
        if not os.path.isdir(d):
            return []
        return [(os.path.join(d, f), is_sp) for f in sorted(os.listdir(d))
                if f.lower().endswith(".wav")]

    files = collect(args.speech_dir, True) + collect(args.noise_dir, False)
    if not files:
        sys.exit("[err] 未找到测试音频，请先跑 scripts/collect_vad_data.py")

    print("测试集: speech=%d noise=%d, 共 %d 条" % (
        sum(1 for _f, s in files if s), sum(1 for _f, s in files if not s), len(files)))
    print("阈值: %s\n" % ths)

    lines = []
    def emit(s=""):
        print(s)
        lines.append(s)

    report = {"thresholds": ths, "versions": []}

    for ver in vers:
        mp = os.path.join(args.models_dir, "silero_%s.onnx" % ver)
        if not os.path.isfile(mp):
            print("[skip] 缺模型 %s" % mp)
            continue
        pr, dt, nf = infer_all(mp, ver, [f for f, _s in files])
        # 按文件标注整理
        labeled = []
        for f, is_sp in files:
            key = os.path.basename(f)
            if key in pr:
                labeled.append((pr[key], is_sp))
        msize = os.path.getsize(mp) / 1e6
        rtf = dt / (nf * 512 / SR) if nf else 0
        emit("\n===== %s  (%s) =====" % (ver, VERSIONS[ver]["desc"]))
        emit("模型 %.2f MB | 推理 %.1f ms/%d帧 | RTF %.4f" %
             (msize, dt * 1000, nf, rtf))
        emit("%-8s %-10s %-12s %-14s %s" %
             ("thresh", "recall", "false_alarm", "seg_speech_ok", "seg_noise_fail"))
        entry = {"version": ver, "model_mb": round(msize, 2),
                 "rtf": round(rtf, 5), "rows": []}
        for th in ths:
            rec, fa, sok, sfail, *_ = metrics(labeled, th)
            emit("%-8.2f %-10s %-12s %-14s %s" % (
                th, "%7.2f%%" % (rec * 100), "%7.2f%%" % (fa * 100),
                "%d/%d" % (sok, sum(1 for _p, s in labeled if s)),
                "%d/%d" % (sfail, sum(1 for _p, s in labeled if not s))))
            entry["rows"].append({"thresh": th, "recall": round(rec, 4),
                                  "false_alarm": round(fa, 4),
                                  "seg_speech_ok": sok, "seg_noise_fail": sfail})
        report["versions"].append(entry)

        if args.compare_no_context and ver != "v4.0":
            pr2, dt2, nf2 = infer_all(mp, ver, [f for f, _s in files],
                                      use_context=False)
            lab2 = [(pr2[os.path.basename(f)], s) for f, s in files
                    if os.path.basename(f) in pr2]
            emit("  -- 不拼 context（对照）--")
            for th in [0.1, 0.5]:
                rec, fa, sok, sfail, *_ = metrics(lab2, th)
                emit("  thresh %.2f  recall %7.2f%%  false_alarm %7.2f%%" %
                     (th, rec * 100, fa * 100))
            entry["no_context"] = {"thresh": 0.1, "recall": round(
                metrics(lab2, 0.1)[0], 4)}

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print("\n报告已写入 %s" % args.out)


if __name__ == "__main__":
    main()
