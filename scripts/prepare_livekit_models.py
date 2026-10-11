#!/usr/bin/env python3
"""Prepare the livekit-wakeword model bundle for QSmartAssistant.

Collects the three ONNX models used by the wake-word pipeline into a single
directory so the C++ engine (and the offline eval tool) can load them:

    melspectrogram.onnx    raw audio (1, n)        -> mel frames (1, 1, F, 32)
    embedding_model.onnx   (1, 76, 32, 1)          -> embedding (1, 1, 1, 96)
    <wakeword>.onnx        (N, 16, 96)             -> score (N, 1)

Note: the classifier exported by recent PyTorch declares ONNX IR version 10,
while onnxruntime <= 1.17 (the version bundled in lib/onnxruntime) only
supports IR <= 9.  The model itself only uses opset-18 ops, so we simply
downgrade the declared ir_version in place -- no graph rewriting needed.

Usage:
    python scripts/prepare_livekit_models.py \
        --src  C:/Users/26864/WorkBuddy/Worktrees/livekit-wakeword/main-d751d36b \
        --dst  Data/livekit \
        --wakeword nihao_xiaozhi
"""

from __future__ import annotations

import argparse
import shutil
import struct
import sys
from pathlib import Path


def read_ir_version(path: Path) -> int:
    """Parse ModelProto.ir_version (field 1, varint) from the front of the file."""
    with open(path, "rb") as f:
        data = f.read(16)
    if not data or data[0] != 0x08:  # key = (1 << 3) | 0
        raise ValueError(f"{path}: unexpected ModelProto layout")
    value = 0
    shift = 0
    for i in range(1, len(data)):
        b = data[i]
        value |= (b & 0x7F) << shift
        if not (b & 0x80):
            return value
        shift += 7
    raise ValueError(f"{path}: truncated varint")


def patch_ir_version(path: Path, target: int = 9) -> int:
    """Rewrite the declared ir_version in place; return the old value."""
    with open(path, "rb") as f:
        data = bytearray(f.read())
    if data[0] != 0x08:
        raise ValueError(f"{path}: unexpected ModelProto layout")
    old = 0
    shift = 0
    i = 1
    while True:
        b = data[i]
        old |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
        i += 1
    if old <= target:
        return old
    if old > 127:
        raise ValueError(f"{path}: ir_version {old} needs a multi-byte varint rewrite")
    data[1] = target
    with open(path, "wb") as f:
        f.write(bytes(data))
    return old


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="livekit-wakeword repo/worktree root")
    ap.add_argument("--dst", required=True, help="output directory (e.g. Data/livekit)")
    ap.add_argument("--wakeword", default="nihao_xiaozhi", help="wake word model name")
    ap.add_argument("--ir-version", type=int, default=9, help="max supported IR version")
    ap.add_argument(
        "--stock-mel-emb",
        action="store_true",
        help="use the repo's own Data/livekit copies for mel/emb instead of --src",
    )
    args = ap.parse_args()

    src = Path(args.src)
    dst = Path(args.dst)
    dst.mkdir(parents=True, exist_ok=True)

    resources = src / "src" / "livekit" / "wakeword" / "resources"
    out = src / "output" / args.wakeword
    clf_src = out / f"{args.wakeword}.onnx"
    if not clf_src.exists():
        # exported models may live directly under output/
        clf_src = src / "output" / f"{args.wakeword}.onnx"
    if not clf_src.exists():
        print(f"error: wake word model not found for '{args.wakeword}'", file=sys.stderr)
        return 1

    plan = [
        (resources / "melspectrogram.onnx", dst / "melspectrogram.onnx", False),
        (resources / "embedding_model.onnx", dst / "embedding_model.onnx", False),
        (clf_src, dst / f"{args.wakeword}.onnx", True),
    ]

    for s, d, patch in plan:
        if not s.exists():
            print(f"error: missing {s}", file=sys.stderr)
            return 1
        shutil.copyfile(s, d)
        ir = read_ir_version(d)
        if patch and ir > args.ir_version:
            old = patch_ir_version(d, args.ir_version)
            print(f"{d.name:28s} ir_version {old} -> {args.ir_version} (patched)")
        else:
            print(f"{d.name:28s} ir_version {ir} (ok, {d.stat().st_size} bytes)")

    print(f"\nbundle ready at {dst.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
