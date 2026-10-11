#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 livekit-wakeword 生成语料中挑选样本，构建 VAD 测试集。

背景
----
`livekit-wakeword.lnk` 指向的工程生成「你好小智」唤醒词语料，每类下：
  clip_NNNNNN.wav      原始干净 TTS 合成语音（未增强，信噪比最好）
  clip_NNNNNN_rN.wav  第 N 轮增强（叠加噪声/混响/失真，越叠越糊）

对 VAD 调参而言，分类必须按「**是否有语音活动**」而不是「是否唤醒词」：
  * speech 正样本 → 「原始干净 TTS 正样本(含唤醒词)」+「非唤醒词语音(真人说话，
    同样是语音活动)」。二者都该被判为人声，合并才有代表性。
  * noise 负样本  → 只放「background_*」（纯背景噪声/音乐/环境音），
    这才是 VAD 应当拒绝的对象。
  * ⚠ 不要把 negative_* 当噪声：那是**真人语音**，silero 判为人声是正确行为，
    混入噪声会虚高误触发率。本脚本默认把 negative_* 归入 speech 侧。

输出结构（默认 QSmartAssistant/scripts/vad_data/）：
  speech/  ← 含唤醒词 + 非唤醒词语音
  noise/   ← 纯背景噪声
  manifest.json 记录来源、类别与统计。

由于 livekit-wakeword 的样本为窗口对齐做过零填充（正样本末尾对齐、负样本居中），
默认按能量裁剪首尾静音（--no-trim 可关闭）。

用法
----
    python scripts/collect_vad_data.py                    # 默认挑选
    python scripts/collect_vad_data.py --out D:/vad_data
    python scripts/collect_vad_data.py --speech 120 --noise-bg 60
    python scripts/collect_vad_data.py --rounds r0        # 用 r0 增强语音作正样本
    python scripts/collect_vad_data.py --dry-run          # 只统计不复制
"""

import argparse
import json
import os
import shutil
import sys
import wave

import numpy as np

DEFAULT_SRC = (r"C:/Users/26864/WorkBuddy/Worktrees/livekit-wakeword/"
               r"main-d751d36b/output/nihao_xiaozhi")
AUDIO_EXT = (".wav", ".flac")


def _read_wav(path):
    with wave.open(path, "rb") as w:
        sr, n_ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(w.getnframes())
    if width == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 4:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float32) / 2147483648.0
    elif width == 1:
        x = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128) / 128.0
    else:
        raise ValueError("unsupported width %d" % width)
    if n_ch > 1:
        x = x.reshape(-1, n_ch).mean(axis=1)
    return x.astype(np.float32), sr


def trim_active(x, sr, frame_ms=20, rel_db=-35.0, min_active_ms=200,
                pad_ms=100):
    """按能量裁掉首尾静音，返回活跃语音段（VAD 无关，避免循环依赖）。

    livekit-wakeword 的正/负样本为窗口对齐做过零填充（正样本末尾对齐、
    负样本居中填充），直接整段喂 VAD 会把填充静音算进重建/漏检。
    这里用滑动 RMS 找到能量门限以上的活跃区段再保留。
    """
    fl = max(1, int(sr * frame_ms / 1000))
    n_fr = len(x) // fl
    if n_fr == 0:
        return x
    rms = np.sqrt(np.mean(x[:n_fr * fl].reshape(n_fr, fl) ** 2, axis=1) + 1e-12)
    peak = rms.max()
    if peak <= 0:
        return x
    thr = peak * (10 ** (rel_db / 20.0))
    active = rms > thr
    # 找最长连续活跃段
    best_s = best_len = cur_s = cur_len = 0
    for i, a in enumerate(active):
        if a:
            if cur_len == 0:
                cur_s = i
            cur_len += 1
            if cur_len > best_len:
                best_len, best_s = cur_len, cur_s
        else:
            cur_len = 0
    if best_len * fl < sr * min_active_ms / 1000:
        return x  # 活跃段太短，保守起见原样返回
    pad = int(sr * pad_ms / 1000)
    a = max(0, best_s * fl - pad)
    b = min(len(x), (best_s + best_len) * fl + pad)
    return x[a:b]


def _write_wav(path, x, sr):
    xi = np.clip(x * 32768.0, -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(xi.tobytes())


def list_by_round(d, want_round):
    """按增强轮次筛选文件。want_round: 'clean' | 'r0' | 'r1' | ... | 'all'"""
    if not os.path.isdir(d):
        return []
    out = []
    for fn in sorted(os.listdir(d)):
        if not fn.lower().endswith(AUDIO_EXT):
            continue
        is_aug = "_r" in os.path.splitext(fn)[0]
        if want_round == "all":
            out.append(fn)
        elif want_round == "clean":
            if not is_aug:
                out.append(fn)
        else:  # r0/r1/r2
            suffix = "_%s" % want_round
            if os.path.splitext(fn)[0].endswith(suffix):
                out.append(fn)
    return out


def evenly_pick(files, n):
    """跨整份列表均匀取样，避免只取前 N 个（前 N 个往往来自同一说话人/批次）。"""
    if n >= len(files):
        return list(files)
    idx = [round(i * (len(files) - 1) / (n - 1)) for i in range(n)] if n > 1 else [0]
    seen, picked = set(), []
    for i in idx:
        if i not in seen:
            seen.add(i)
            picked.append(files[i])
    return picked


def copy_all(src_dir, files, dst_dir, prefix, dry_run, trim=False):
    os.makedirs(dst_dir, exist_ok=True)
    manifest = []
    for i, fn in enumerate(files):
        src = os.path.join(src_dir, fn)
        dst = os.path.join(dst_dir, "%s_%04d_%s" % (prefix, i, fn))
        try:
            x, sr = _read_wav(src)
        except Exception:
            continue
        raw_dur = len(x) / sr
        if trim:
            x = trim_active(x, sr)
        dur = len(x) / sr
        if not dry_run:
            _write_wav(dst, x, sr)
        manifest.append({"file": os.path.basename(dst), "src": src,
                         "dur": round(dur, 3), "raw_dur": round(raw_dur, 3),
                         "sr": sr, "ch": 1, "trimmed": bool(trim)})
    return manifest


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="从 livekit-wakeword 语料挑选 VAD 测试集")
    ap.add_argument("--src", default=DEFAULT_SRC, help="livekit-wakeword output 目录")
    ap.add_argument("--out", default=os.path.join(here, "vad_data"),
                    help="输出根目录（下建 speech/ 与 noise/）")
    ap.add_argument("--speech-wake", type=int, default=100,
                    help="speech 中含唤醒词的干净 TTS 条数（positive_test）")
    ap.add_argument("--speech-nonwake", type=int, default=60,
                    help="speech 中非唤醒词语音条数（negative_test，真人说话）")
    ap.add_argument("--noise-bg", type=int, default=40,
                    help="noise 中背景噪声条数（background_test）")
    ap.add_argument("--rounds", default="clean",
                    choices=["clean", "r0", "r1", "r2", "all"],
                    help="正样本取哪一轮（默认 clean=原始干净 TTS）")
    ap.add_argument("--no-trim", action="store_true",
                    help="不做首尾静音裁剪（默认裁剪：语料为窗口对齐做过零填充）")
    ap.add_argument("--dry-run", action="store_true", help="只统计不复制")
    args = ap.parse_args()

    if not os.path.isdir(args.src):
        sys.exit("[err] 源目录不存在: %s\n      （livekit-wakeword.lnk 指向的路径）"
                 % args.src)

    print("源: %s" % args.src)
    print("正样本轮次: %s   speech(wake)=%d  speech(non-wake)=%d  noise(bg)=%d\n"
          % (args.rounds, args.speech_wake, args.speech_nonwake, args.noise_bg))

    wake_files = evenly_pick(
        list_by_round(os.path.join(args.src, "positive_test"), args.rounds),
        args.speech_wake)
    nonwake_files = evenly_pick(
        list_by_round(os.path.join(args.src, "negative_test"), args.rounds),
        args.speech_nonwake)
    bg_files = evenly_pick(
        list_by_round(os.path.join(args.src, "background_test"), args.rounds),
        args.noise_bg)

    print("可用: positive_test=%d  negative_test=%d  background_test=%d" % (
        len(list_by_round(os.path.join(args.src, "positive_test"), args.rounds)),
        len(list_by_round(os.path.join(args.src, "negative_test"), args.rounds)),
        len(list_by_round(os.path.join(args.src, "background_test"), args.rounds))))
    print("已选: speech_wake=%d  speech_nonwake=%d  noise_bg=%d\n" %
          (len(wake_files), len(nonwake_files), len(bg_files)))

    if args.dry_run:
        print("[dry-run] 不复制文件。示例 wake:", wake_files[:3])
        print("[dry-run] 示例 non-wake:", nonwake_files[:3])
        print("[dry-run] 示例 bg:", bg_files[:3])
        return

    m1 = copy_all(os.path.join(args.src, "positive_test"), wake_files,
                  os.path.join(args.out, "speech"), "wake", False,
                  trim=not args.no_trim)
    m2 = copy_all(os.path.join(args.src, "negative_test"), nonwake_files,
                  os.path.join(args.out, "speech"), "nonwake", False,
                  trim=not args.no_trim)
    m3 = copy_all(os.path.join(args.src, "background_test"), bg_files,
                  os.path.join(args.out, "noise"), "bg", False,
                  trim=not args.no_trim)

    manifest = {
        "source": args.src,
        "rounds": args.rounds,
        "trimmed": not args.no_trim,
        "note": "speech = 含唤醒词 + 非唤醒词语音(均为语音活动); "
                "noise = 纯背景噪声",
        "speech": m1 + m2,
        "noise": m3,
    }
    with open(os.path.join(args.out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)

    tot_dur = sum(x["dur"] for x in m1 + m2 + m3)
    print("完成：speech %d 条 / noise %d 条，总时长 %.1fs" %
          (len(m1) + len(m2), len(m3), tot_dur))
    print("输出: %s" % args.out)
    print("下一步: python scripts/tune_silero_thresh.py --speech-dir %s "
          "--noise-dir %s" % (os.path.join(args.out, "speech"),
                              os.path.join(args.out, "noise")))


if __name__ == "__main__":
    main()
