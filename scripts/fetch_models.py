#!/usr/bin/env python3
"""下载并安装 QSmartAssistant 运行所需的 sherpa-onnx 模型。

在 Windows 上跑起来至少需要三类模型（VAD 模型仓库自带，无需下载）：

  * ASR  : sherpa-onnx-paraformer-zh-small-2024-03-09  (~74 MB)
  * 唤醒 : sherpa-onnx-kws-zipformer-wenetspeech-3.3M   (~16 MB)
  * TTS  : vits-zh-aishell3                             (~140 MB)

脚本会把模型解压到 %USERPROFILE%\\.config\\QSmartAssistant\\Data 下，
并自动改写同目录 config.json 里的模型路径。

用法：
    python scripts/fetch_models.py

注意：本脚本**只负责 sherpa-onnx 那一侧**的模型。项目默认 ASR 自 2026-10 起是
FunASR 的 Paraformer-q8（`-DASR_FUN=ON`），模型不在 GitHub Release 上而是在
ModelScope，请用 `python scripts/setup.py --only funasr-asr`
（或直接 `--profile minimal`，一并装齐）。
这里下 ASR 选的是体积较小的 "paraformer-zh-small" 而不是默认配置里写的
"sherpa-onnx-paraformer-zh-2024-03-09"（后者整包接近 1 GB）；而且这份 ASR 模型
现在只有用 `-DASR_SHERPA=ON -DASR_FUN=OFF` 编译时才用得上。
"""
import json
import os
import shutil
import tarfile
import tempfile
import urllib.request

DATA = os.path.join(os.path.expanduser("~"), ".config", "QSmartAssistant", "Data")
TMP = tempfile.mkdtemp(prefix="qsa_models_")

GH = "https://github.com/k2-fsa/sherpa-onnx/releases/download"
ASR_DIR = "sherpa-onnx-paraformer-zh-small-2024-03-09"
TTS_DIR = "vits-zh-aishell3"
KWS_DIR = "sherpa-kws"

JOBS = [
    # (下载地址, 目标子目录名, 是否"平铺"到目标目录)
    (GH + "/asr-models/" + ASR_DIR + ".tar.bz2", ASR_DIR, False),
    (GH + "/tts-models/" + TTS_DIR + ".tar.bz2", TTS_DIR, False),
    ("https://github.com/pkufool/keyword-spotting-models/releases/download/v0.1/"
     "sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01.tar.bz",
     KWS_DIR, True),
]


def download(url, dest):
    print("downloading", url, flush=True)
    with urllib.request.urlopen(url, timeout=180) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f, 1024 * 1024)
    print("   -> %.1f MB" % (os.path.getsize(dest) / 1048576.0), flush=True)


def install(url, target_name, flatten):
    tar_path = os.path.join(TMP, os.path.basename(url))
    download(url, tar_path)
    stage = os.path.join(TMP, target_name)
    os.makedirs(stage, exist_ok=True)
    with tarfile.open(tar_path) as t:
        t.extractall(stage)
    # 定位解包后的内容根目录
    entries = [e for e in os.listdir(stage) if not e.startswith(".")]
    if len(entries) == 1 and os.path.isdir(os.path.join(stage, entries[0])):
        src = os.path.join(stage, entries[0])
    else:
        src = stage
    dst = os.path.join(DATA, target_name)
    if os.path.exists(dst):
        shutil.rmtree(dst)
    if flatten:
        os.makedirs(dst, exist_ok=True)
        for fn in os.listdir(src):
            shutil.move(os.path.join(src, fn), os.path.join(dst, fn))
    else:
        shutil.move(src, dst)
    print("   installed ->", dst, flush=True)
    return dst


def main():
    os.makedirs(DATA, exist_ok=True)
    for url, name, flatten in JOBS:
        install(url, name, flatten)

    # 重要：config 里的 `%1` 是应用运行时占位符，会被替换为
    # encoder / decoder / joiner（见 Wakeup/Wakeup/sherpawakeup.cpp）。
    # 因此磁盘上的模型文件必须保留 encoder-.../decoder-.../joiner-... 原名，
    # 不要重命名成 `%1-...`。

    repo_data = os.path.join(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))), "Data")
    yaml_path = os.path.join(DATA, "config.yaml")
    json_path = os.path.join(DATA, "config.json")

    def _load_cfg():
        """yaml 优先，其次老 json，最后回退仓库模板。返回 (cfg, use_yaml)。"""
        if os.path.exists(yaml_path):
            try:
                import yaml
                with open(yaml_path, encoding="utf-8") as f:
                    return yaml.safe_load(f), True
            except ImportError:
                print("提示：未安装 PyYAML，将回退到 config.json；"
                      "pip install pyyaml 后可写 yaml。", flush=True)
        if os.path.exists(json_path):
            with open(json_path, encoding="utf-8") as f:
                return json.load(f), False
        src_yaml = os.path.join(repo_data, "default_config.yaml")
        src_json = os.path.join(repo_data, "default_config.json")
        if os.path.exists(src_yaml):
            try:
                import yaml
                with open(src_yaml, encoding="utf-8") as f:
                    return yaml.safe_load(f), True
            except ImportError:
                pass
        if os.path.exists(src_json):
            with open(src_json, encoding="utf-8") as f:
                return json.load(f), False
        raise SystemExit("找不到任何配置模板（Data/default_config.yaml|json）")

    cfg, use_yaml = _load_cfg()

    asr_files = os.listdir(os.path.join(DATA, ASR_DIR))
    asr_model = next(f for f in asr_files if f.endswith(".onnx"))
    cfg["sherpa"]["model"] = "%s/%s" % (ASR_DIR, asr_model)
    cfg["sherpa"]["tokens"] = "%s/tokens.txt" % ASR_DIR

    tts_files = os.listdir(os.path.join(DATA, TTS_DIR))
    tts_model = next(f for f in tts_files if f.endswith(".onnx"))
    cfg["sherpa_tts"]["model"] = "%s/%s" % (TTS_DIR, tts_model)
    # vits-zh-aishell3 包内没有 rule.fst，使用包内实际存在的规则文件
    cfg["sherpa_tts"]["rules"] = ",".join(
        "%s/%s" % (TTS_DIR, f) for f in
        ("date.fst", "number.fst", "phone.fst", "new_heteronym.fst")
        if f in tts_files)

    cfg["sherpa_wakeup"]["model"] = (
        KWS_DIR + "/%1-epoch-12-avg-2-chunk-16-left-64.int8.onnx")
    cfg["sherpa_wakeup"]["tokens"] = KWS_DIR + "/tokens.txt"
    cfg["sherpa_wakeup"]["hotword"] = KWS_DIR + "/keywords.txt"

    if use_yaml:
        import yaml
        with open(yaml_path, "w", encoding="utf-8") as f:
            yaml.safe_dump(cfg, f, allow_unicode=True, sort_keys=True,
                           default_flow_style=False)
        print("config.yaml 已更新:", flush=True)
    else:
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(cfg, f, ensure_ascii=False, indent=2)
        print("config.json 已更新:", flush=True)
    print(json.dumps({k: cfg[k] for k in ("sherpa", "sherpa_tts", "sherpa_wakeup")},
                     ensure_ascii=False, indent=2), flush=True)

    shutil.rmtree(TMP, ignore_errors=True)
    print("done")


if __name__ == "__main__":
    main()
