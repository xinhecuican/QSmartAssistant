#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fetch_llm_model.py —— 下载本地推理用的 GGUF 模型。

用于「直接加载模型」这条链路（LlamaCppBackend）：
    ollama 那条路需要额外装 ollama 并常驻一个进程；直接加载只需要
    一个 .gguf 文件 + lib/llama/bin 里的 DLL，插电就能跑，树莓派也扛得住。

默认下载 qwen3.5 2b（Q4_K_M 约 1.5 GB），来源 huggingface 镜像
（国内可达；直连 huggingface.co 在受限网络下通常不通）。

用法：
    python scripts/fetch_llm_model.py                # 默认 Q4_K_M
    python scripts/fetch_llm_model.py --quant Q8_0
    python scripts/fetch_llm_model.py --repo unsloth/Qwen3.5-2B-GGUF --quant Q5_K_M
    python scripts/fetch_llm_model.py --dest ~/.config/QSmartAssistant/Data/llm
    python scripts/fetch_llm_model.py --list          # 只看有哪些量化版本

下载完把返回的文件名填进 config.json：
    "llm": {"backends": {"local-qwen": {
        "type": "llamacpp",
        "model": "Qwen3.5-2B-Q4_K_M.gguf",
        "modelDir": "llm"
    }}}
"""

import argparse
import io
import json
import os
import shutil
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))

DEFAULT_REPO = "unsloth/Qwen3.5-2B-GGUF"
DEFAULT_QUANT = "Q4_K_M"
# 国内镜像优先；HF_ENDPOINT 环境变量可覆盖；直连放最后兜底
ENDPOINTS = [
    os.environ.get("HF_ENDPOINT", "https://hf-mirror.com"),
    "https://hf-mirror.com",
    "https://huggingface.co",
]


def make_opener():
    op = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    op.addheaders = [("User-Agent", "QSmartAssistant/1.0")]
    return op


def fetch_json(opener, url, timeout=30):
    return json.loads(opener.open(url, timeout=timeout).read().decode("utf-8"))


def list_repo(opener, repo):
    """列出仓库里可下载的 gguf 文件。"""
    last = None
    for base in ENDPOINTS:
        try:
            data = fetch_json(opener, "%s/api/models/%s" % (base, repo))
            return base, [s["rfilename"] for s in data.get("siblings", [])
                          if s["rfilename"].endswith(".gguf")]
        except Exception as exc:
            last = exc
    raise SystemExit("cannot list repo %s: %s" % (repo, last))


def resolve_file(opener, repo, quant):
    """把 Q4_K_M 这样的量化名解析成仓库里的实际文件名。"""
    base, files = list_repo(opener, repo)
    # mmproj-* 是多模态投影，语音助手用不上，直接排除
    models = [f for f in files if not f.startswith("mmproj-")]
    if not models:
        raise SystemExit("no gguf model files found in %s" % repo)

    for f in models:
        stem = os.path.basename(f)
        if stem.upper().endswith(quant.upper() + ".GGUF"):
            return base, f
    # 退而求其次：包含该量化关键字
    for f in models:
        if quant.upper() in os.path.basename(f).upper():
            return base, f
    raise SystemExit(
        "quant %r not found in %s. available:\n  %s"
        % (quant, repo, "\n  ".join(os.path.basename(m) for m in models)))


def download(opener, url, dest_path, expect_size=None):
    """带断点续传与进度显示的下载。"""
    tmp = dest_path + ".part"
    existing = os.path.getsize(tmp) if os.path.exists(tmp) else 0

    headers = {"User-Agent": "QSmartAssistant/1.0"}
    if existing:
        headers["Range"] = "bytes=%d-" % existing
        print("  resuming from %.1f MB" % (existing / 1e6))

    req = urllib.request.Request(url, headers=headers)
    resp = opener.open(req, timeout=60)
    mode = "ab" if existing and resp.code == 206 else "wb"
    if mode == "wb":
        existing = 0

    total = expect_size or 0
    if not total:
        length = resp.headers.get("Content-Length")
        if length:
            total = int(length) + existing

    done = existing
    last_report = 0
    with open(tmp, mode) as f:
        while True:
            chunk = resp.read(1024 * 256)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if total and done - last_report > total * 0.02:
                last_report = done
                print("  %.1f / %.1f MB (%.0f%%)"
                      % (done / 1e6, total / 1e6, 100.0 * done / total),
                      flush=True)

    if total and done < total:
        os.remove(tmp)
        raise SystemExit("incomplete download: %d / %d bytes" % (done, total))
    os.replace(tmp, dest_path)
    return done


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--quant", default=DEFAULT_QUANT)
    ap.add_argument("--dest", default=None,
                    help="存放目录，默认 ~/.config/QSmartAssistant/Data/llm")
    ap.add_argument("--file", default=None,
                    help="直接指定仓库内的文件名，跳过量化名解析")
    ap.add_argument("--list", action="store_true",
                    help="只列出可用的量化版本后退出")
    args = ap.parse_args()

    opener = make_opener()

    if args.list:
        _, files = list_repo(opener, args.repo)
        print("available gguf files in %s:" % args.repo)
        for f in files:
            print("  %s" % f)
        return 0

    if args.file:
        base, remote = ENDPOINTS[0], args.file
        for candidate in ENDPOINTS:
            try:
                fetch_json(opener, "%s/api/models/%s" % (candidate, args.repo))
                base = candidate
                break
            except Exception:
                continue
    else:
        base, remote = resolve_file(opener, args.repo, args.quant)

    dest_dir = args.dest
    if dest_dir is None:
        dest_dir = os.path.join(
            os.path.expanduser("~"), ".config", "QSmartAssistant", "Data", "llm")
    os.makedirs(dest_dir, exist_ok=True)

    dest = os.path.join(dest_dir, os.path.basename(remote))
    if os.path.exists(dest):
        print("already exists: %s (%.1f MB)"
              % (dest, os.path.getsize(dest) / 1e6))
    else:
        url = "%s/%s/resolve/main/%s" % (base, args.repo, remote)
        print("downloading %s" % remote)
        print("  from %s" % url)
        size = download(opener, url, dest)
        print("done: %.1f MB" % (size / 1e6))

    print()
    print("model file: %s" % dest)
    print()
    print("在 config.json 里加一个本地后端：")
    print(json.dumps({
        "llm": {"backends": {"local-qwen": {
            "type": "llamacpp",
            "model": os.path.basename(dest),
            "modelDir": os.path.relpath(
                dest_dir,
                os.path.join(os.path.expanduser("~"), ".config",
                             "QSmartAssistant", "Data")),
            "nCtx": 2048,
            "nThreads": 4,
            "gpuLayers": 0,
            "maxTokens": 512,
        }}},
        "llm_nlu": {"llm": "local-qwen"},
    }, ensure_ascii=False, indent=2))
    print()
    print("别忘了把 lib/llama/bin 下的 DLL 一起拷到 %s/llama"
          % os.path.expanduser("~"))
    return 0


if __name__ == "__main__":
    sys.exit(main())