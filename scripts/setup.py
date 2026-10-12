#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""setup.py —— QSmartAssistant 统一安装入口（跨平台，单文件）。

把所有原本分散的安装脚本收敛成一处，带依赖图、断点续装与安装后自检。

设计要点
--------
1. **组件化 + 依赖图**：每个组件声明 `requires`。`--profile minimal`
   只装「唤醒 + ASR + TTS + NLU」这条最小链路，不碰 OpenSSL / llama /
   网易云 node 服务等可选项。
2. **复用而非重写**：onnxruntime / sherpa-onnx / rasa / llama 这几项仍调用
   scripts/install_*.{sh,ps1}（那些脚本里有大量踩过坑的处理，重复实现
   等于把坑再踩一遍）。组件层只负责：判断要不要装 → 调脚本 → 验证结果。
3. **模型与库分离**：`lib/` 里的库（编译产物）与 `~/.config/.../Data/`
   里的模型（下载产物）是两回事。组件显式区分，避免"库装好了但跑不起来"。
4. **断点续装**：每个组件有 `is_installed()` 探测，已装则跳过；
   `--force` 可强制重装。

最小安装（profile=minimal）包含什么
------------------------------------
    livekit-wakeup   唤醒   mel/emb 从 openWakeWord 官方 release 下载，
                            分类头需自备（见 --livekit-src / --livekit-head）
    funasr-asr       ASR    FunASR 官方 ONNX 导出包 = Paraformer-q8（~230 MB，
                            **默认 ASR**：-DASR_FUN=ON）
    sherpa-tts       TTS    vits-zh-aishell3（~140 MB）
    onnx-nlu         NLU    进程内 ONNX 双头模型（vocab/rules 随仓库，
                            57MB int8 权重从 GitHub Release 下载）
    onnxruntime      依赖   funasr / livekit / silero VAD / onnx-nlu 都要（编译产物）
    sherpa-onnx      依赖   TTS 的 C API 共享库（编译产物；ASR 默认已不用它）
    yaml-cpp         依赖   config.yaml 的读写（编译产物；lib/ 不入库，
                            **新 clone 必须装**，否则 cmake configure
                            直接报 Could NOT find yaml_cpp）

    sherpa-asr 自 2026-10 起**不再是默认 ASR**（默认换成 funasr，见
    doc/funasr_cpu_edge_bench.md §10），不在任何 profile 里；只有用
    `-DASR_SHERPA=ON -DASR_FUN=OFF` 编译时才需要 --only sherpa-asr。

    rasa-nlu 现在是**兜底后端**，不在任何默认 profile 里；只有把
    config.json 改成 "nlu": {"backend": "rasa"} 时才需要 --only rasa-nlu。

用法
----
    # Linux 最小安装（部署推荐）
    python3 scripts/setup.py --profile minimal

    # Windows 最小安装
    python scripts/setup.py --profile minimal

    # 看看会做什么但不真装
    python3 scripts/setup.py --profile minimal --dry-run

    # 只装某几项
    python3 scripts/setup.py --only onnx-nlu funasr-asr

    # 切回 sherpa ASR 时（配合 -DASR_SHERPA=ON -DASR_FUN=OFF）
    python3 scripts/setup.py --only sherpa-asr

    # 需要 rasa 兜底后端时
    python3 scripts/setup.py --only rasa-nlu

    # 装完自检
    python3 scripts/setup.py --verify-only
"""

from __future__ import annotations

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SCRIPTS = HERE

# 运行期数据目录。与 CMake 的 CONFIG_PATH（$HOME/.config/QSmartAssistant）一致，
# 见顶层 CMakeLists.txt。
RUNTIME_DIR = Path.home() / ".config" / "QSmartAssistant"
RUNTIME_DATA = RUNTIME_DIR / "Data"
REPO_DATA = ROOT / "Data"

IS_WIN = sys.platform == "win32"


# ===========================================================================
# 输出helpers
# ===========================================================================
class C:
    """ANSI 颜色。非 TTY 时自动降级为纯文本（避免日志文件里出现转义序列）。"""

    def __init__(self, enabled: Optional[bool] = None):
        self.on = sys.stdout.isatty() if enabled is None else enabled

    def _w(self, code: str, s: str) -> str:
        return f"\033[{code}m{s}\033[0m" if self.on else s

    def bold(self, s):   return self._w("1", s)
    def green(self, s):  return self._w("32", s)
    def yellow(self, s): return self._w("33", s)
    def red(self, s):    return self._w("31", s)
    def cyan(self, s):   return self._w("36", s)
    def dim(self, s):    return self._w("2", s)


C_ = C()


def info(msg):    print(f"  {msg}", flush=True)
def step(msg):    print(f"\n{C_.cyan('==>')} {C_.bold(msg)}", flush=True)
def ok(msg):      print(f"  {C_.green('OK')} {msg}", flush=True)
def skip(msg):    print(f"  {C_.yellow('--')} {msg} (已安装，跳过)", flush=True)
def warn(msg):    print(f"  {C_.yellow('!!')} {msg}", flush=True)
def fail(msg):    print(f"  {C_.red('XX')} {msg}", flush=True)


# ===========================================================================
# 通用工具
# ===========================================================================
def sha256_of(path: Path, limit: Optional[int] = None) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def human(n: float) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024:
            return f"{n:.1f}{unit}"
        n /= 1024.0
    return f"{n:.1f}TB"


def run(cmd: Sequence[str], cwd: Optional[Path] = None,
        env: Optional[Dict[str, str]] = None, check: bool = True,
        dry: bool = False) -> int:
    """执行子进程。dry 模式只打印不执行。"""
    printable = " ".join(str(c) for c in cmd)
    if dry:
        print(f"  {C_.yellow('$')} {printable}")
        return 0
    proc = subprocess.run([str(c) for c in cmd], cwd=str(cwd) if cwd else None,
                          env=env)
    if check and proc.returncode != 0:
        raise RuntimeError(f"command failed ({proc.returncode}): {printable}")
    return proc.returncode


def download(url: str, dest: Path, dry: bool = False,
             label: str = "") -> Path:
    """下载到 dest。先下到 .part 再改名，避免中断留下"看起来完整"的半个文件。

    GitHub 在无网络/被墙时会返回一个几十字节的 HTML 页面并返回 200，
    不校验大小的话会得到一个能"下载成功"但完全不能用的文件。
    """
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dry:
        print(f"  [dry] download {label or dest.name} <- {url}")
        return dest
    tmp = dest.with_suffix(dest.suffix + ".part")
    req = urllib.request.Request(url, headers={"User-Agent": "QSmartAssistant/setup"})
    t0 = time.time()
    print(f"  downloading {label or dest.name} ...", end=" ", flush=True)
    try:
        with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
            total = int(r.headers.get("Content-Length") or 0)
            done = 0
            while True:
                chunk = r.read(1 << 18)
                if not chunk:
                    break
                f.write(chunk)
                done += len(chunk)
                if total:
                    pct = done * 100 // total
                    print(f"\r  downloading {label or dest.name} {pct:3d}% "
                          f"({human(done)}/{human(total)})   ", end="", flush=True)
        print(f"\r  downloaded {label or dest.name} {human(done)} "
              f"in {time.time()-t0:.1f}s" + " " * 20)
    except Exception as exc:
        tmp.unlink(missing_ok=True)
        raise RuntimeError(f"download failed: {url}\n    {exc}") from exc
    # 最小体积门槛：<10KB 的 onnx / tar 一定是错误页
    if tmp.stat().st_size < 10_240:
        size = tmp.stat().st_size
        head = tmp.read_bytes()[:200]
        tmp.unlink(missing_ok=True)
        raise RuntimeError(
            f"{url}\n  -> only {size} bytes, this is an error page not the file "
            f"(GitHub returns HTTP 200 with HTML when the asset is missing)\n"
            f"  first bytes: {head[:120]!r}")
    tmp.replace(dest)
    return dest


def extract_archive(archive: Path, dest_dir: Path, dry: bool = False) -> Path:
    """解压 tar.bz2 / tar.gz / zip。返回内容根目录（自动剥掉一层单目录）。"""
    if dry:
        print(f"  [dry] extract {archive.name} -> {dest_dir}")
        return dest_dir
    dest_dir.mkdir(parents=True, exist_ok=True)
    name = archive.name.lower()
    if name.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            z.extractall(dest_dir)
    elif name.endswith((".tar.bz2", ".tbz2", ".tar.gz", ".tgz", ".tar")):
        with tarfile.open(archive) as t:
            t.extractall(dest_dir)
    else:
        raise RuntimeError(f"unsupported archive: {archive}")
    entries = [e for e in dest_dir.iterdir() if not e.name.startswith(".")]
    if len(entries) == 1 and entries[0].is_dir():
        return entries[0]
    return dest_dir


def ensure_runtime_data() -> Path:
    """把仓库 Data/ 的脚本与配置准备到运行期目录。

    只在缺失时拷贝，不覆盖 —— 用户可能已在运行期 config.yaml 里改过密钥。
    配置文件以 yaml 为准；程序自身也支持读取老 json 并自动迁移。
    """
    RUNTIME_DATA.mkdir(parents=True, exist_ok=True)
    cfg = RUNTIME_DATA / "config.yaml"
    if not cfg.exists():
        shutil.copy2(REPO_DATA / "default_config.yaml", cfg)
        ok(f"生成运行期 config.yaml（来自 default_config.yaml）")
    order = RUNTIME_DATA / "plugin_order.txt"
    if not order.exists():
        shutil.copy2(REPO_DATA / "plugin_order_default.txt", order)
        ok(f"生成运行期 plugin_order.txt")
    # start/stop-rasa.sh 必须同步：rasanlu.cpp 的 Linux 分支按名字调它们
    for script in ("start-rasa.sh", "stop-rasa.sh"):
        src, dst = REPO_DATA / script, RUNTIME_DATA / script
        if src.exists() and (not dst.exists() or sha256_of(src) != sha256_of(dst)):
            shutil.copy2(src, dst)
            dst.chmod(0o755)
            ok(f"同步 {script}")
    return RUNTIME_DATA


def call_script(name_stem: str, args: Sequence[str], dry: bool) -> None:
    """调用 scripts/install_<stem>.{sh,ps1}。优先当前平台的那个。"""
    cands = []
    if IS_WIN:
        cands = [SCRIPTS / f"install_{name_stem}.ps1", SCRIPTS / f"install_{name_stem}.sh"]
    else:
        cands = [SCRIPTS / f"install_{name_stem}.sh", SCRIPTS / f"install_{name_stem}.ps1"]
    for script in cands:
        if not script.exists():
            continue
        if script.suffix == ".ps1":
            cmd = ["powershell", "-ExecutionPolicy", "Bypass", "-File", str(script)]
        else:
            cmd = ["bash", str(script)]
        cmd += list(args)
        run(cmd, cwd=ROOT, dry=dry)
        return
    raise RuntimeError(f"找不到 install_{name_stem} 的脚本（试过 {cands}）")


# ===========================================================================
# 组件
# ===========================================================================
@dataclass
class Component:
    name: str
    title: str
    requires: List[str] = field(default_factory=list)
    size: str = "?"
    minutes: str = "?"
    platforms: str = "all"          # all / linux / win
    optional: bool = False

    # 以下由 register 阶段填入
    probe: Callable[[], bool] = lambda: False
    action: Optional[Callable[["Ctx"], None]] = None
    verify: Optional[Callable[[], List[str]]] = None

    def supported(self) -> bool:
        return (self.platforms == "all"
                or (self.platforms == "linux" and not IS_WIN)
                or (self.platforms == "win" and IS_WIN))


@dataclass
class Ctx:
    dry: bool
    force: bool
    hf_base: str
    livekit_src: Optional[str]
    livekit_head: Optional[str]
    nlu_url: str
    jobs: int

    def should_install(self, comp: Component) -> bool:
        if self.force:
            return True
        try:
            return not comp.probe()
        except Exception:
            return True


REGISTRY: Dict[str, Component] = {}


def register(c: Component) -> Component:
    """登记一个组件。真正的调用点在本文件末尾（所有 builder 定义之后）。"""
    REGISTRY[c.name] = c
    return c


# openWakeWord 官方 release 里这两个模型是所有 wakeword 方案的公共前置。
# sha256 已与本机那份逐一核对一致（2026-10-11）：worktree 原始文件与仓库那份
# 只差第 1 字节（IR version 10 → 9），mel/emb 则完全一致。
OPENWAKEWORD = "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1"
OWW_SHA = {
    "melspectrogram.onnx":
        "ba2b0e0f8b7b875369a2c89cb13360ff53bac436f2895cced9f479fa65eb176f",
    "embedding_model.onnx":
        "70d164290c1d095d1d4ee149bc5e00543250a7316b59f31d056cff7bd3075c1f",
}


# ---- 组件探测函数 --------------------------------------------------------
def have(path: Path) -> bool:
    return path.exists()


def ort_installed() -> bool:
    """ONNX Runtime 是否已装好。

    探测必须**按平台看对应形态的库**：Windows 的包只有 onnxruntime.lib
    （链接期用）+ bin/*.dll（运行期用），POSIX 是 lib/libonnxruntime.so。
    早先只看 include 目录，两个平台都会误判为已装 —— 在没真正下载过的
    机器上会把"要装的东西"当成"装好了"，直接跳过。
    """
    base = ROOT / "lib" / "onnxruntime"
    if IS_WIN:
        has_hdr = (base / "include" / "onnxruntime_c_api.h").exists()
        has_runtime = any((base / "bin").glob("onnxruntime*.dll"))
        has_import = (base / "lib" / "onnxruntime.lib").exists()
        return has_hdr and (has_runtime or has_import)
    return (base / "lib" / "libonnxruntime.so").exists()


def sherpa_installed() -> bool:
    return (ROOT / "lib" / "sherpa_onnx" / "include").is_dir()


def rasa_venv_python() -> Optional[Path]:
    for rel in ("bin/python", "Scripts/python.exe"):
        p = ROOT / "lib" / "rasa" / rel
        if p.exists():
            return p
    return None


def rasa_installed() -> bool:
    return rasa_venv_python() is not None


def model_dir(name: str) -> Path:
    return RUNTIME_DATA / name


def asr_installed() -> bool:
    d = model_dir("sherpa-onnx-paraformer-zh-small-2024-03-09")
    return d.is_dir() and any(d.glob("*.onnx")) and (d / "tokens.txt").exists()


def tts_installed() -> bool:
    d = model_dir("vits-zh-aishell3")
    return d.is_dir() and any(d.glob("*.onnx")) and (d / "tokens.txt").exists()


def livekit_installed() -> bool:
    d = RUNTIME_DATA / "livekit"
    return all((d / f).exists() for f in
               ("melspectrogram.onnx", "embedding_model.onnx"))


def livekit_head_installed() -> bool:
    """分类头 = livekit 目录里除 mel/emb 之外的任意 .onnx。"""
    d = RUNTIME_DATA / "livekit"
    if not d.is_dir():
        return False
    reserved = set(OWW_SHA)
    return any(p.suffix == ".onnx" and p.name not in reserved for p in d.iterdir())


def bert_installed() -> bool:
    d = REPO_DATA / "rasa" / "data" / "chinese_rbtl3"
    return (d / "pytorch_model.bin").exists()


def rasa_model_installed() -> bool:
    return (REPO_DATA / "rasa" / "models" / "default.tar.gz").exists()


def onnx_nlu_installed() -> bool:
    """ONNX NLU 三件套随仓库提供，不需下载；只需确认运行期那份在位。"""
    d = RUNTIME_DATA / "onnx_nlu"
    return ((d / "nlu_int8.onnx").exists()
            and (d / "vocab.txt").exists()
            and (d / "rules.json").exists()
            and (RUNTIME_DATA / "nlu_schema.json").exists())


def openssl_installed() -> bool:
    if IS_WIN:
        return True          # Windows 走系统 CNG，不需要 OpenSSL
    return True              # Linux 上缺失也只是降级，不阻断


def yaml_cpp_installed() -> bool:
    """config.yaml 的读写依赖（lib/yaml-cpp/{include,lib}）是否就位。

    `lib/` 整个目录都在 .gitignore 里，所以**新 clone 必然没有**；而它是
    **configure 阶段**的硬依赖（顶层 CMakeLists.txt 的
    `find_package(yaml_cpp REQUIRED)`），缺了就只会在 cmake 阶段报
    "Could NOT find yaml_cpp (missing: yaml_cpp_LIBRARY)"。放进这里是为了
    让"装依赖"和"能不能 configure"这两件事在同一个入口对齐。
    """
    base = ROOT / "lib" / "yaml-cpp"
    has_header = (base / "include" / "yaml-cpp" / "yaml.h").exists()
    has_library = ((base / "lib" / "yaml-cpp.lib").exists()
                   or (base / "lib" / "libyaml-cpp.a").exists())
    return has_header and has_library


# ---- 组件实现 ------------------------------------------------------------
def _build_yaml_cpp() -> Component:
    c = Component("yaml-cpp", "yaml-cpp 静态库（config.yaml 读写，编译期依赖）",
                  size="<1MB", minutes="<1",
                  platforms="all",
                  probe=yaml_cpp_installed)
    c.action = lambda ctx: call_script("yaml_cpp", [], ctx.dry)
    c.verify = lambda: ([] if yaml_cpp_installed() else
                        ["lib/yaml-cpp 缺失 —— cmake configure 会直接失败"
                         "（Could NOT find yaml_cpp）"])
    return c


def _build_onnxruntime() -> Component:
    c = Component("onnxruntime", "ONNX Runtime（推理引擎）",
                  size="~15MB", minutes="1-3",
                  platforms="all",
                  probe=ort_installed)
    c.action = lambda ctx: call_script("onnxruntime", [], ctx.dry)
    c.verify = lambda: [] if ort_installed() else [
        "lib/onnxruntime 缺失 —— livekit 唤醒与 silero VAD 都依赖它"]
    return c


def _build_sherpa_onnx() -> Component:
    c = Component("sherpa-onnx", "sherpa-onnx C API（ASR/TTS 原生库）",
                  size="~50MB", minutes="10-60",
                  platforms="all",
                  requires=["onnxruntime"],
                  probe=sherpa_installed)
    c.action = lambda ctx: call_script("sherpa_onnx", ["--skip-model"], ctx.dry)
    c.verify = lambda: [] if sherpa_installed() else ["lib/sherpa_onnx 缺失"]
    return c


# ---- ONNX NLU ------------------------------------------------------------
# 三件套里 vocab.txt / rules.json 很小，随仓库 `Data/onnx_nlu/` 走；int8 权重
# 约 57MB，不适合进 git（仓库体积 + GitHub 100MB 单文件上限），挂在 GitHub
# Release 上（tag: onnx_nlu，资产名 nlu_int8.onnx）。
# ⚠ 换 Release tag 或改资产名时必须同步改这里，否则下载会拿到 404 的 HTML。
ONNX_NLU_RELEASE_TAG = "onnx_nlu"
ONNX_NLU_ASSET = "nlu_int8.onnx"
ONNX_NLU_URL = (f"https://github.com/xinhecuican/QSmartAssistant"
                f"/releases/download/{ONNX_NLU_RELEASE_TAG}/{ONNX_NLU_ASSET}")


def _build_onnx_nlu() -> Component:
    c = Component("onnx-nlu", "ONNX NLU（进程内意图+槽位，替代 rasa）",
                  size="~58MB", minutes="<1",
                  platforms="all",
                  requires=["onnxruntime"],
                  probe=onnx_nlu_installed)
    c.action = install_onnx_nlu
    c.verify = onnx_nlu_verify
    return c


def install_onnx_nlu(ctx: "Ctx") -> None:
    """补齐运行期 `Data/` 里的 ONNX NLU 四件套。

    分工：
      * `vocab.txt` / `rules.json` / `nlu_schema.json` 很小，随仓库 `Data/` 走，
        CMake 在 configure 阶段也会整目录拷到 ~/.config/QSmartAssistant/Data/；
      * `nlu_int8.onnx`（~57MB）**不随仓库提交**，挂在 GitHub Release 上，这里
        负责下载。仓库里若恰好有（开发机常见）则直接复用，不重复下载。

    之所以还要单独处理运行期目录：老机器的 ~/.config 里可能还没有 onnx_nlu/
    （在 ONNX NLU 上线前装的）。"""
    dst = RUNTIME_DATA / "onnx_nlu"

    # ---- 小文件：随仓库提供 ----
    small = [("onnx_nlu/vocab.txt", "vocab.txt"),
             ("onnx_nlu/rules.json", "rules.json"),
             ("nlu_schema.json", "nlu_schema.json")]
    if ctx.dry:
        for rel, name in small:
            target = RUNTIME_DATA / name
            print(f"  [dry] ensure {rel} -> {target}")
        print(f"  [dry] ensure onnx_nlu/nlu_int8.onnx "
              f"（仓库有则复用，否则下载 {ctx.nlu_url}）")
        return
    dst.mkdir(parents=True, exist_ok=True)
    for rel, name in small:
        src = REPO_DATA / rel
        if not src.exists():
            raise RuntimeError(
                f"仓库里缺 {rel} —— 它随仓库提供，若这里是空说明 checkout 不完整。")
        target = RUNTIME_DATA / name
        if target.exists() and sha256_of(target) == sha256_of(src):
            skip(f"{name}（已存在，校验一致）")
            continue
        shutil.copy2(src, target)
        ok(f"{name}  ({human(target.stat().st_size)})")

    # ---- 权重：优先复用仓库那份（开发机），否则从 Release 下载 ----
    weight = dst / "nlu_int8.onnx"
    repo_weight = REPO_DATA / "onnx_nlu" / "nlu_int8.onnx"
    if weight.exists() and (not repo_weight.exists()
                            or sha256_of(weight) == sha256_of(repo_weight)):
        skip("nlu_int8.onnx（已存在）")
        return
    if repo_weight.exists():
        shutil.copy2(repo_weight, weight)
        ok(f"nlu_int8.onnx  ({human(weight.stat().st_size)}，来自仓库 Data/)")
        return
    try:
        download(ctx.nlu_url, weight, ctx.dry, "nlu_int8.onnx")
    except RuntimeError as exc:
        raise RuntimeError(
            f"ONNX NLU 权重下载失败：{ctx.nlu_url}\n"
            f"    {exc}\n"
            f"  它是 ~57MB 的 int8 权重，不随仓库提交，挂在 GitHub Release 上。\n"
            f"  可手动下载后放到 {weight}；或用 --nlu-url 指定镜像 / 本地文件。"
        ) from exc
    ok(f"nlu_int8.onnx  ({human(weight.stat().st_size)})")


def onnx_nlu_verify() -> List[str]:
    d = RUNTIME_DATA / "onnx_nlu"
    problems: List[str] = []
    for rel in ("nlu_int8.onnx", "vocab.txt", "rules.json"):
        if not (d / rel).exists():
            problems.append(f"Data/onnx_nlu/{rel} 缺失（NLU 会回落 rasa 或全哑）")
    if not (RUNTIME_DATA / "nlu_schema.json").exists():
        problems.append("Data/nlu_schema.json 缺失（槽位 schema，NLU 需要）")
    return problems


def _build_rasa() -> Component:
    c = Component("rasa-nlu", "rasa NLU（兜底后端，默认不装）",
                  size="~1.2GB", minutes="10-25",
                  platforms="all",
                  probe=rasa_installed)
    c.action = lambda ctx: call_script("rasa", [], ctx.dry)
    c.verify = rasa_verify
    return c


def rasa_verify() -> List[str]:
    """rasa 的验证必须真 import —— '装了但 import 不了' 是最常见的失败。"""
    problems: List[str] = []
    py = rasa_venv_python()
    if py is None:
        return ["lib/rasa 下的 python 不存在"]
    if not rasa_model_installed():
        problems.append("Data/rasa/models/default.tar.gz 缺失（NLU 会全哑）")
    if not bert_installed():
        problems.append("Data/rasa/data/chinese_rbtl3/pytorch_model.bin 缺失"
                        "（LanguageModelFeaturizer 需要它）")
    # 真跑一次 import：这正是 transformers 5.x 会炸的地方
    if not IS_WIN:
        code = ("try:\n"
                "    from transformers import TFPreTrainedModel\n"
                "    import transformers, rasa\n"
                "    print('OK', transformers.__version__)\n"
                "except Exception as e:\n"
                "    print('FAIL', e)\n")
        try:
            out = subprocess.run([str(py), "-c", code], capture_output=True,
                                 text=True, timeout=120)
            tail = (out.stdout + out.stderr).strip().splitlines()
            last = tail[-1] if tail else "(无输出)"
            if "OK" not in last:
                problems.append(
                    f"rasa import 失败：{last}\n"
                    f"      → transformers 必须钉 4.36.2（5.x 删了 TF 支持）")
        except subprocess.TimeoutExpired:
            problems.append("rasa import 超时（120s），venv 可能损坏")
        except FileNotFoundError:
            problems.append(f"无法执行 {py}")
    return problems


def _build_livekit() -> Component:
    c = Component("livekit-wakeup", "livekit-wakeup（唤醒词，onnxruntime）",
                  size="~2.6MB", minutes="<1",
                  platforms="all",
                  requires=["onnxruntime"],
                  probe=livekit_installed)
    c.action = install_livekit
    c.verify = livekit_verify
    return c


# openWakeWord 官方 release 里这两个模型是所有 wakeword 方案的公共前置。
# sha256 见文件顶部 OWW_SHA。
def install_livekit(ctx: Ctx) -> None:
    """livekit 三件套：mel + emb（可下载）+ 分类头（随仓库提交）。

    分类头 size_small.onnx 是**本项目自训练**的产物，openWakeWord 官方 release
    里没有，因此**已经连 mel/emb 一起提交进仓库** Data/livekit/，正常 clone 就
    有了，不需要任何参数。仍保留另外两种来源以便替换成别的自训练唤醒词，
    按优先级依次尝试：
      1. --livekit-head 直接给文件路径
      2. --livekit-src 给 livekit-wakeword 仓库根目录（有 output/<name>/*.onnx）
      3. 仓库 Data/livekit/ 里已有的分类头
    都拿不到时**不静默跳过** —— 唤醒会完全失效，必须报出来。
    """
    dst = RUNTIME_DATA / "livekit"
    dst.mkdir(parents=True, exist_ok=True)
    tmpd = Path(tempfile.mkdtemp(prefix="qsa_livekit_"))

    try:
        # ---- 先校验显式参数给的路径 ----
        # 必须在"已存在就跳过"之前校验：否则用户传了个错路径却因为本地
        # 恰好有旧的分类头而静默用了旧模型，看不出参数没生效。
        if ctx.livekit_head and not Path(ctx.livekit_head).exists():
            raise RuntimeError(f"--livekit-head 指向的文件不存在: {ctx.livekit_head}")
        if ctx.livekit_src and not Path(ctx.livekit_src).is_dir():
            raise RuntimeError(f"--livekit-src 指向的目录不存在: {ctx.livekit_src}")

        for fname, sha in OWW_SHA.items():
            target = dst / fname
            if target.exists() and sha256_of(target) == sha:
                skip(f"{fname}（校验一致）")
                continue
            url = f"{OPENWAKEWORD}/{fname}"
            got = download(url, tmpd / fname, ctx.dry, fname)
            if not ctx.dry:
                real = sha256_of(got)
                if real != sha:
                    raise RuntimeError(
                        f"{fname} 校验不符：\n"
                        f"  期望 {sha}\n  实际 {real}\n"
                        f"  多半是代理返回了缓存/劫持页面。重试或设 --hf-base 走镜像。")
                shutil.copy2(got, target)
            ok(f"{fname} ({human(target.stat().st_size) if not ctx.dry else '?'})")

        # ---- 分类头 ----
        if livekit_head_installed():
            skip("分类头（已存在）")
            return

        head: Optional[Path] = None
        if ctx.livekit_head:
            cand = Path(ctx.livekit_head)
            if not cand.exists():
                raise RuntimeError(f"--livekit-head 指向的文件不存在: {cand}")
            head = cand
            info(f"使用 --livekit-head: {cand}")
        elif ctx.livekit_src:
            src = Path(ctx.livekit_src)
            # 与 scripts/prepare_livekit_models.py 相同的查找顺序
            for rel in ("output/size_small/size_small.onnx",
                        "output/nihao_xiaozhi/nihao_xiaozhi.onnx",
                        "output/nihao_xiaozhi_clean/nihao_xiaozhi_clean.onnx"):
                cand = src / rel
                if cand.exists():
                    head = cand
                    info(f"从 --livekit-src 找到分类头: {cand}")
                    break
            if head is None:
                matches = sorted(src.glob("output/*/*.onnx"))
                if matches:
                    head = matches[0]
                    info(f"从 --livekit-src 找到分类头: {head}")
        else:
            # 仓库里可能同时有多个自训练分类头（size_small / nihao_xiaozhi …）。
            # 优先选 config.yaml 默认的那个（size_small.onnx），否则按名字排序取
            # 第一个 —— 不能直接用 sorted()[0]，否则装了 nihao_xiaozhi 却指望
            # 默认唤醒词是 size_small 的人会踩空。
            cands = [p for p in sorted((REPO_DATA / "livekit").glob("*.onnx"))
                     if p.name not in OWW_SHA]
            cands.sort(key=lambda p: (p.name != "size_small.onnx", p.name))
            if cands:
                head = cands[0]
                info(f"复用仓库 Data/livekit/{head.name}")

        if head is None:
            raise RuntimeError(
                "缺少 wakeword 分类头（size_small.onnx）。\n"
                "  它随仓库 Data/livekit/ 一起提交；这里是空说明 checkout 不完整，\n"
                "  或者你清理过该目录。三种提供方式任选其一：\n"
                "    a) --livekit-head /path/to/size_small.onnx\n"
                "    b) --livekit-src /path/to/livekit-wakeword   （含 output/<name>/）\n"
                "    c) 把文件放回仓库 Data/livekit/ 下\n"
                "  没有它唤醒不会工作，但其余组件不受影响，可先 --only 跳过本项。")

        if ctx.dry:
            print(f"  [dry] copy livekit head {head} -> {dst}")
            return

        # 必须降 IR version：新版 PyTorch 导出的是 IR 10，而 lib/onnxruntime
        # 是 1.17.1（只支持 IR ≤ 9），直接加载会报
        # "Unsupported model IR version: 10"。分类头只用 opset-18 算子，
        # 改声明版本即可，无需重写图。逻辑复用 prepare_livekit_models.py。
        sys.path.insert(0, str(SCRIPTS))
        from prepare_livekit_models import patch_ir_version, read_ir_version
        target = dst / head.name
        shutil.copy2(head, target)
        ir = read_ir_version(target)
        if ir > 9:
            old = patch_ir_version(target, 9)
            ok(f"{head.name}  ir_version {old} -> 9（onnxruntime 1.17 只支持 ≤9）")
        else:
            ok(f"{head.name}  ({human(target.stat().st_size)})")
    finally:
        shutil.rmtree(tmpd, ignore_errors=True)


def livekit_verify() -> List[str]:
    problems: List[str] = []
    d = RUNTIME_DATA / "livekit"
    if not d.is_dir():
        return ["Data/livekit/ 缺失"]
    for fname, sha in OWW_SHA.items():
        p = d / fname
        if not p.exists():
            problems.append(f"缺 {fname}")
        elif sha256_of(p) != sha:
            problems.append(f"{fname} 校验不符（文件损坏或被改过）")
    if not livekit_head_installed():
        problems.append("缺分类头 *.onnx（唤醒无法工作）")
    return problems


# sherpa-onnx 模型包
SHERPA_GH = "https://github.com/k2-fsa/sherpa-onnx/releases/download"
ASR_PKG = "sherpa-onnx-paraformer-zh-small-2024-03-09"
TTS_PKG = "vits-zh-aishell3"


# ⚠ 自 2026-10 起 sherpa-asr **不是默认 ASR** 了（默认是下面 funasr-asr 的
#   Paraformer-q8），所以它不在任何 profile 里 —— 只有显式用
#   `-DASR_SHERPA=ON -DASR_FUN=OFF` 编译时才需要 `--only sherpa-asr`。
#   组件本身保留（且 probe/verify 照旧），切回去不用改脚本。
def _build_asr() -> Component:
    c = Component("sherpa-asr", "sherpa ASR（paraformer-zh-small，非默认后端）",
                  size="~74MB", minutes="2-6",
                  platforms="all",
                  probe=asr_installed)
    c.action = lambda ctx: install_sherpa_model(
        ctx, f"{SHERPA_GH}/asr-models/{ASR_PKG}.tar.bz2", ASR_PKG, flatten=False)
    c.verify = lambda: [] if asr_installed() else [f"缺 {ASR_PKG}"]
    return c


# ---------------------------------------------------------------------------
# FunASR 官方 ONNX 导出包（Paraformer-q8）
#
# 这是 `-DASR_FUN=ON`（Conversation/ASR/funasr.cpp）用的模型，**自 2026-10 起
# 是默认 ASR**（`Conversation/CMakeLists.txt` 里 option(ASR_FUN) 默认 ON），
# 所以 minimal / standard / full 三个 profile 都装它。
#
# 相比 sherpa 那份 paraformer-zh-small，这个是 **large** 的 int8 量化版：
# AISHELL-1 抽样 999 条上 micro-CER 1.98%（small 版 2.90%），
# 见 doc/funasr_cpu_edge_bench.md §10。代价是模型大 3 倍（230MB vs 74MB）。
#
# ⚠ 这里**只要三件套**（model_quant.onnx / am.mvn / tokens.json）：
#   前端（80 维 mel-fbank → LFR(7,6) → CMVN）是 funasr.cpp 自己算的，
#   官方包里的 config.yaml 我们只当文档看，不下载也不用。
# ---------------------------------------------------------------------------
FUNASR_MS = "https://www.modelscope.cn/models"
FUNASR_MODEL = ("iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-onnx")
FUNASR_DIR = "paraformer-zh-q8"
FUNASR_FILES = ["model_quant.onnx", "am.mvn", "tokens.json"]


def funasr_installed() -> bool:
    d = model_dir(FUNASR_DIR)
    return all((d / f).exists() for f in FUNASR_FILES)


def install_funasr_model(ctx: Ctx) -> None:
    """从 ModelScope 拉官方 ONNX 导出包（4 个文件，只有 3 个是必需的）。"""
    dst = model_dir(FUNASR_DIR)
    dst.mkdir(parents=True, exist_ok=True)
    for name in FUNASR_FILES:
        url = f"{FUNASR_MS}/{FUNASR_MODEL}/resolve/master/{name}"
        download(url, dst / name, ctx.dry, f"{FUNASR_DIR}/{name}")
    if ctx.dry:
        print(f"  [dry] install {FUNASR_DIR} -> {dst}")
        return
    size = sum(f.stat().st_size for f in dst.rglob("*") if f.is_file())
    ok(f"{FUNASR_DIR} -> {dst} ({human(size)})")


def _build_funasr_asr() -> Component:
    c = Component("funasr-asr", "FunASR ASR（Paraformer-q8 / ONNX，默认后端 -DASR_FUN=ON）",
                  size="~230MB", minutes="3-10",
                  platforms="all",
                  probe=funasr_installed)
    c.action = lambda ctx: install_funasr_model(ctx)
    c.verify = lambda: ([] if funasr_installed()
                        else [f"缺 {FUNASR_DIR}/model_quant.onnx"])
    return c


def _build_tts() -> Component:
    c = Component("sherpa-tts", "sherpa TTS（vits-zh-aishell3）",
                  size="~140MB", minutes="2-8",
                  platforms="all",
                  probe=tts_installed)
    c.action = lambda ctx: install_sherpa_model(
        ctx, f"{SHERPA_GH}/tts-models/{TTS_PKG}.tar.bz2", TTS_PKG, flatten=False)
    c.verify = lambda: [] if tts_installed() else [f"缺 {TTS_PKG}"]
    return c


def install_sherpa_model(ctx: Ctx, url: str, pkg: str, flatten: bool) -> None:
    """下载并安装一个 sherpa-onnx 模型包到运行期 Data 目录。"""
    dst = model_dir(pkg)
    if dst.is_dir():
        shutil.rmtree(dst)
    tmpd = Path(tempfile.mkdtemp(prefix="qsa_pkg_"))
    try:
        arc = download(url, tmpd / f"{pkg}.tar.bz2", ctx.dry, pkg)
        root = extract_archive(arc, tmpd / pkg, ctx.dry)
        if ctx.dry:
            print(f"  [dry] install {pkg} -> {dst}")
            return
        root = Path(root)
        dst.mkdir(parents=True, exist_ok=True)
        for item in root.iterdir():
            target = dst / item.name
            if item.is_dir():
                shutil.copytree(item, target, dirs_exist_ok=True)
            else:
                shutil.copy2(item, target)
        ok(f"{pkg} -> {dst} ({human(sum(f.stat().st_size for f in dst.rglob('*') if f.is_file()))})")
    finally:
        shutil.rmtree(tmpd, ignore_errors=True)


def _build_llama() -> Component:
    p = ROOT / "lib" / "llama" / "bin"
    return _make_llama(p)


def _make_llama(llama_bin: Path) -> Component:
    def probe() -> bool:
        if IS_WIN:
            return any(llama_bin.glob("*.dll"))
        return any(llama_bin.glob("libllama.so*"))

    c = Component("llama", "llama.cpp（本地 GGUF 推理，可选）",
                  size="~40MB", minutes="2-5",
                  platforms="all", optional=True, probe=probe)
    c.action = lambda ctx: call_script("llama", ["--backend", "cpu"], ctx.dry)
    c.verify = lambda: ([] if probe() else
                         ["lib/llama/bin 里没有 llama 动态库（chat / llm_nlu 的本地后端不可用）"])
    return c


def _build_netease() -> Component:
    def probe() -> bool:
        # node 服务是运行期依赖： Plugins/neteasemusic/NeteaseCloudMusicApi/app.js
        base = ROOT / "Plugins" / "neteasemusic" / "NeteaseCloudMusicApi"
        return (base / "app.js").exists()
    c = Component("netease-js", "网易云 node 服务（音乐插件，可选）",
                  size="~80MB", minutes="3-8",
                  platforms="all", optional=True, probe=probe)
    c.action = lambda ctx: call_script("neteasejs", [], ctx.dry)
    c.verify = lambda: ([] if probe() else
                         ["app.js 缺失；不装不影响其它插件，音乐插件不可用"])
    return c


# ---- profiles ------------------------------------------------------------
PROFILES: Dict[str, Dict[str, object]] = {
    # 最小全流程：唤醒 → ASR → NLU → TTS。这是语音助手的完整链路。
    # NLU 用进程内 ONNX（无 Python/TF/torch/2GB 常驻），产物随仓库提供。
    # ASR 自 2026-10 起默认 funasr（Paraformer-q8，230MB），与
    # Conversation/CMakeLists.txt 的 option(ASR_FUN) 默认一致；
    # 想省体积切回 sherpa：--skip funasr-asr --only sherpa-asr 并加
    # -DASR_SHERPA=ON -DASR_FUN=OFF。
    "minimal": {
        "components": ["yaml-cpp", "onnxruntime", "sherpa-onnx", "livekit-wakeup",
                      "funasr-asr", "sherpa-tts", "onnx-nlu"],
        "desc": "最小全流程：livekit 唤醒 + silero VAD + FunASR(Paraformer-q8) ASR + onnx NLU + sherpa TTS",
    },
    "standard": {
        "components": ["yaml-cpp", "onnxruntime", "sherpa-onnx", "livekit-wakeup",
                      "funasr-asr", "sherpa-tts", "onnx-nlu", "llama"],
        "desc": "standard：minimal + llama.cpp 本地大模型（chat / llm_nlu）",
    },
    "full": {
        "components": ["yaml-cpp", "onnxruntime", "sherpa-onnx", "livekit-wakeup",
                      "funasr-asr", "sherpa-tts", "onnx-nlu", "llama", "netease-js"],
        "desc": "full：standard + 网易云音乐 node 服务",
    },
}


def resolve_components(profile: str, only: List[str],
                       skip: List[str]) -> List[Component]:
    """按 profile + only + skip 展开成有序组件列表（拓扑排序）。"""
    if only:
        wanted = list(only)
    else:
        wanted = list(PROFILES[profile]["components"])  # type: ignore[index]

    for name in wanted:
        if name not in REGISTRY:
            raise SystemExit(f"未知组件: {name}\n可选: {', '.join(sorted(REGISTRY))}")
    for name in skip:
        if name not in REGISTRY:
            raise SystemExit(f"--skip 里有未知组件: {name}")

    selected: List[Component] = []
    seen = set()

    def visit(name: str) -> None:
        if name in seen:
            return
        seen.add(name)
        c = REGISTRY[name]
        for dep in c.requires:
            visit(dep)
        selected.append(c)

    for name in wanted:
        visit(name)

    result = [c for c in selected if c.name not in skip]

    # 跳过了某个组件的依赖，必须明确说出来 —— 否则会出现"ASR 装好了但其实
    # 缺 onnxruntime、编译能过、运行期 dlopen 才炸"这种最难查的情况。
    kept = {c.name for c in result}
    for c in result:
        for dep in c.requires:
            if dep not in kept:
                warn(f"{c.name} 依赖 {dep}，但它被 --skip 排除了 —— "
                     f"{c.name} 可能无法工作")

    return result


# ===========================================================================
# 主流程
# ===========================================================================
def main() -> int:
    ap = argparse.ArgumentParser(
        description="QSmartAssistant 统一安装入口（跨平台）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument("--profile", choices=sorted(PROFILES), default="minimal",
                    help="安装组合，默认 minimal（最小全流程）")
    ap.add_argument("--only", nargs="+", metavar="COMP",
                    help="只装指定组件（覆盖 --profile）")
    ap.add_argument("--skip", nargs="+", metavar="COMP", default=[],
                    help="跳过指定组件")
    ap.add_argument("--force", action="store_true", help="已安装也重装")
    ap.add_argument("--dry-run", action="store_true", help="只打印计划，不实际执行")
    ap.add_argument("--verify-only", action="store_true", help="只做安装后自检")
    ap.add_argument("--no-verify", action="store_true", help="跳过安装后自检")
    ap.add_argument("--livekit-src", metavar="DIR",
                    help="livekit-wakeword 仓库根目录，用于取 wakeword 分类头")
    ap.add_argument("--livekit-head", metavar="FILE",
                    help="直接指定 wakeword 分类头 .onnx 文件")
    ap.add_argument("--nlu-url", default=os.environ.get("QSA_NLU_URL", ONNX_NLU_URL),
                    help="ONNX NLU int8 权重下载地址（默认取 GitHub Release）")
    ap.add_argument("--hf-base", default=os.environ.get("HF_ENDPOINT", ""),
                    help="镜像前缀（HF_ENDPOINT 环境变量亦可），如 https://hf-mirror.com")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4,
                    help="并行度（编译类组件用）")
    args = ap.parse_args()

    ctx = Ctx(dry=args.dry_run, force=args.force, hf_base=args.hf_base,
              livekit_src=args.livekit_src, livekit_head=args.livekit_head,
              nlu_url=args.nlu_url, jobs=args.jobs)

    comps = resolve_components(args.profile, args.only or [], args.skip)

    # 只装不受支持的平台组件时要明确告知，而不是静默跳过
    usable = [c for c in comps if c.supported()]
    skipped_plat = [c for c in comps if not c.supported()]
    if not usable:
        fail("所选组件在当前平台均不支持")
        return 2

    print()
    print(C_.bold(f"QSmartAssistant setup") +
          f"  platform={sys.platform} arch={platform.machine()}")
    print(f"profile: {C_.cyan(args.profile)} — {PROFILES[args.profile]['desc']}"
          if not args.only else "profile: (--only 覆盖)")
    print(f"target  : {RUNTIME_DATA}")
    if args.dry_run:
        print(C_.yellow("DRY RUN — 不会真的下载或安装"))
    for c in skipped_plat:
        warn(f"{c.name} 不支持当前平台（仅 {c.platforms}），已排除")

    if not args.verify_only:
        step("安装计划")
        for i, c in enumerate(usable, 1):
            mark = C_.green("[skip]") if (not ctx.dry and not ctx.should_install(c)) \
                else C_.cyan("[ ok ]")
            dep = f" (needs {', '.join(c.requires)})" if c.requires else ""
            opt = " [可选]" if c.optional else ""
            print(f"  {i}. {mark} {c.title}{opt}"
                  f"{C_.dim('  ' + dep) if dep else ''}"
                  f"  {C_.dim(f'{c.size}, ~{c.minutes}min')}")

    if args.verify_only:
        print()
        return run_verify(usable)

    if not args.dry_run:
        ensure_runtime_data()

    step("开始安装")
    failures: List[tuple] = []
    t0 = time.time()
    for i, c in enumerate(usable, 1):
        print()
        info(f"[{i}/{len(usable)}] {C_.bold(c.title)}")
        if c not in usable:  # pragma: no cover
            continue
        try:
            if not ctx.should_install(c):
                skip(c.name)
                continue
            if c.action is None:
                raise RuntimeError(f"组件 {c.name} 没有实现 action")
            c.action(ctx)
        except Exception as exc:
            fail(f"{c.name}: {exc}")
            failures.append((c.name, str(exc)))

    dt = time.time() - t0
    print()
    print(C_.bold("=" * 60))
    if args.dry_run:
        # dry 模式下 failures 必然为空（action 里不真执行、不抛错），
        # 报"全部完成"是误导 —— 实际跑一遍可能失败。
        info(f"DRY RUN 结束，未做任何实际改动（{len(usable)} 个组件的计划已打印）")
        rc = 0
    elif failures:
        fail(f"{len(failures)}/{len(usable)} 个组件失败（耗时 {dt:.0f}s）")
        for name, msg in failures:
            print(f"    - {name}: {msg.splitlines()[0]}")
        print()
        print("可单独重试失败的组件：")
        print(f"    python3 scripts/setup.py --only {' '.join(n for n, _ in failures)}")
        rc = 1
    else:
        ok(f"全部组件安装完成（耗时 {dt:.0f}s）")
        rc = 0

    if not args.no_verify and not args.dry_run:
        # 自检失败的优先级高于"全部装完"：有组件没过自检就不能算这次安装成功
        rc = run_verify(usable) or rc

    return rc


def run_verify(comps: List[Component]) -> int:
    step("安装后自检")
    bad = 0
    for c in comps:
        try:
            problems = c.verify() if c.verify else []
        except Exception as exc:
            problems = [f"自检时出错: {exc}"]
        if problems:
            bad += 1
            fail(c.name)
            for p in problems:
                print(f"      - {p}")
        else:
            ok(c.name)

    print()
    if bad:
        fail(f"{bad} 个组件未通过自检")
        # 不在这里 return：配置一致性检查与"下一步"指引对排障同样有用，
        # 提前返回会让用户只看到一行失败、不知道接下来该干什么。
    else:
        ok("自检全部通过")

    # 顺带核对配置与模型是否对得上 —— 这一步能抓出"装了但配置指向别处"
    step("配置一致性")
    cfg_path = RUNTIME_DATA / "config.yaml"
    if not cfg_path.exists():
        cfg_path = RUNTIME_DATA / "config.json"  # 老版本兜底
    print(f"  config      : {cfg_path}")
    try:
        import json
        text = cfg_path.read_text(encoding="utf-8")
        if cfg_path.suffix in (".yaml", ".yml"):
            try:
                import yaml  # PyYAML 可能没装，装了就用它
                cfg = yaml.safe_load(text) or {}
            except ImportError:
                # 退化：yaml 只取顶层键的粗略值，够看 backend / python_venv
                cfg = {}
                for line in text.splitlines():
                    s = line.strip()
                    if s.startswith("backend:"):
                        cfg.setdefault("nlu", {})["backend"] = s.split(":", 1)[1].strip()
                    elif s.startswith("python_venv:"):
                        cfg.setdefault("rasa", {})["python_venv"] = (
                            s.split(":", 1)[1].strip().strip('"'))
        else:
            cfg = json.loads(text)
        print(f"  nlu.backend : {cfg.get('nlu', {}).get('backend')}")
        print(f"  rasa venv   : {cfg.get('rasa', {}).get('python_venv') or '(空=自动探测)'}")
        if (RUNTIME_DATA / "rasa" / "models" / "default.tar.gz").exists():
            ok("NLU 模型已就位")
        else:
            warn("NLU 模型缺失，NLU 会全哑")
    except Exception as exc:
        warn(f"读取配置文件失败: {exc}")

    print()
    print(C_.bold("下一步"))
    if IS_WIN:
        print(r"  .\scripts\build_windows.ps1        # 编译")
    else:
        print("  bash scripts/build_linux.sh         # 编译")
        print("  bash scripts/check_runtime_linux.sh # 依赖自检（插件是 dlopen 的）")
    print("  见 doc/linux.md / doc/windows.md")
    # 自检失败必须返回非 0：CI / 部署脚本会看这个退出码决定是否继续。
    return 1 if bad else 0


if __name__ == "__main__":
    for _builder in (_build_yaml_cpp, _build_onnxruntime, _build_sherpa_onnx,
                     _build_livekit, _build_asr, _build_tts, _build_onnx_nlu,
                     _build_rasa, _build_llama, _build_netease, _build_funasr_asr):
        register(_builder())
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\n已中断")
        raise SystemExit(130)