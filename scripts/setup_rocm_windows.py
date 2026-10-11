#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
setup_rocm_windows.py —— 在 Windows 上准备 AMD ROCm + PyTorch 环境。

背景：Radeon RX 9070 XT 是 RDNA 4 架构，gfx target 为 **gfx1201**。
PyTorch 官方只发布 cuXXX（NVIDIA）构建，AMD 的 ROCm 构建要单独装。
2026 年起 AMD 已提供 Windows 原生 wheel（无需 WSL、无需编译 HIP SDK），
所以在 Windows 上微调是可行的。

用法：
    python scripts/setup_rocm_windows.py            # 打印安装命令（不执行）
    python scripts/setup_rocm_windows.py --install # 真正执行 pip 安装
    python scripts/setup_rocm_windows.py --check   # 只体检当前环境

GPU 矩阵（gfx target 决定了 wheel 后缀，写错装不上或运行时报
"no kernel image is available"）：
    RDNA 4  gfx1201  RX 9070 XT / RX 9070 / Radeon AI PRO R9700
    RDNA 4  gfx1200  RX 9060 XT / RX 9060
    RDNA 3.5 gfx115x Ryzen AI Max (Strix Halo)
    RDNA 3  gfx110x  RX 7900 XTX / 7800 XT / 7700 XT

前置条件（AMD 官方要求，缺一个都可能装不上）：
  * Windows 11 + 最新 AMD Adrenalin 驱动（26.x）
  * Python 3.12（ROCm wheel 只为 3.12 提供了 numpy 构建）
  * 关闭 Microsoft Defender Application Guard (WDAG) 与 Smart App Control
"""

import argparse
import platform
import subprocess
import sys

# RX 9070 XT = RDNA 4 = gfx1201
DEFAULT_GFX = "gfx1201"

# 已知的 (torch, torchvision, torchaudio, rocm) 组合。
# 来源：repo.amd.com/rocm/whl-next 与 stable.repo.amd.com/rocm/whl-next
ROCM_STACKS = {
    "10.0.0": {
        "torch": "2.13.0+rocm10.0.0",
        "torchvision": "0.28.0+rocm10.0.0",
        "torchaudio": "2.11.0.2+rocm10.0.0",
        "index": "https://stable.repo.amd.com/rocm/whl-next/",
    },
    "7.13.0": {
        "torch": "2.11.0+rocm7.13.0",
        "torchvision": "0.26.0+rocm7.13.0",
        "torchaudio": "2.11.0+rocm7.13.0",
        "index": "https://repo.amd.com/rocm/whl-multi-arch/",
    },
    "7.2.0": {
        # AMD 官方 Windows ROCm SDK wheel（不带 device- 后缀）
        "torch": "2.9.1+rocmsdk20260116",
        "torchvision": "0.24.1+rocmsdk20260116",
        "torchaudio": "2.9.1+rocmsdk20260116",
        "index": None,  # 直接给 .whl URL，见下
        "wheel_base": "https://repo.radeon.com/rocm/windows/rocm-rel-7.2/",
        "wheel_files": {
            "torch": "torch-2.9.1%2Brocmsdk20260116-cp312-cp312-win_amd64.whl",
            "torchvision": "torchvision-0.24.1%2Brocmsdk20260116-cp312-cp312-win_amd64.whl",
            "torchaudio": "torchaudio-2.9.1%2Brocmsdk20260116-cp312-cp312-win_amd64.whl",
        },
    },
}


def banner(text):
    print()
    print("=" * 68)
    print(text)
    print("=" * 68)


def check_env():
    """体检：把「问题出在哪」一次说清，不要让用户装完才发现驱动不对。"""
    banner("环境体检")
    ok = True

    print("OS        : %s %s (%s)"
          % (platform.system(), platform.release(), platform.machine()))

    if platform.system() != "Windows":
        print("!! 非 Windows。Linux 下请直接用 AMD 官方 ppa/容器，流程更简单。")
        ok = False

    ver = sys.version_info
    if (ver.major, ver.minor) != (3, 12):
        print("!! Python %d.%d —— ROCm wheel 面向 3.12，其它版本会缺 numpy"
              % (ver.major, ver.minor))
        ok = False
    else:
        print("Python    : %d.%d.%d  OK" % ver[:3])

    try:
        import torch
        print("torch      : %s" % torch.__version__)
        if torch.cuda.is_available():
            print("device    : %s" % torch.cuda.get_device_name(0))
            print("gcnArch   : %s" % torch.cuda.get_device_properties(0).gcnArchName)
            print("HIP       : %s" % torch.version.hip)
        else:
            print("!! torch 装好了但 cuda.is_available() == False")
            print("   → 多半是驱动太旧（需要 Adrenalin 26.x）或 wheel 后缀 gfx 写错")
            ok = False
    except ImportError:
        print("torch      : 未安装")

    print()
    print("若上面报 False，请按 README 的前置条件处理后再重试。")
    return ok


def print_plan(gfx, rocm_ver, python="3.12"):
    stack = ROCM_STACKS[rocm_ver]
    banner("安装方案  gfx=%s  ROCm=%s" % (gfx, rocm_ver))

    steps = [
        "# 1) 用 Python %s 建虚拟环境（ROCm wheel 只为 3.12 提供 numpy 构建）" % python,
        "py -%s -m venv .venv" % python,
        ".venv\\Scripts\\activate",
        "python -m pip install --upgrade pip setuptools wheel",
        "",
        "# 2) 装 ROCm PyTorch",
    ]

    if stack["index"]:
        pkgs = [
            '"torch[device-%s]==%s"' % (gfx, stack["torch"]),
            '"torchvision[device-%s]==%s"' % (gfx, stack["torchvision"]),
            '"torchaudio==%s"' % stack["torchaudio"],
        ]
        steps.append("pip install %s \\" % " ".join(pkgs))
        steps.append("    --index-url %s" % stack["index"])
    else:
        base = stack["wheel_base"]
        for pkg in ("torch", "torchvision", "torchaudio"):
            steps.append("pip install %s%s" % (base, stack["wheel_files"][pkg]))

    steps += [
        "",
        "# 3) 验证（必须看到你的显卡名 + gcnArch 里含 gfx1201）",
        'python -c "import torch;print(torch.__version__, torch.cuda.is_available());'
        'print(torch.cuda.get_device_name(0));'
        "print(torch.cuda.get_device_properties(0).gcnArchName)\"",
        "",
        "# 4) 装微调依赖（transformers / peft / trl / datasets / accelerate）",
        "pip install -r tools/nlu_llm_finetune/requirements.txt",
        "",
        "# 5) 跑训练",
        "python tools/nlu_llm_finetune/train_lora.py \\",
        "    --base Qwen/Qwen3.5-2B \\",
        "    --epochs 12 \\",
        "    --batch-size 8 \\",
        "    --lora-r 32",
    ]

    print()
    print("\n".join(steps))
    banner("显存参考（RX 9070 XT = 16 GB）")
    print("  LoRA r=16/32 + Qwen2.5-2B  bf16 全参加载   : 约 5~7 GB   余量充足")
    print("  batch=8 / seq=512                          : 不会 OOM")
    print("  样本很短（system prompt ~600 字符 ≈ 400 tok），max_seq_len 设 512 足够")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gfx", default=DEFAULT_GFX,
                    help="AMD gfx target。RX 9070 XT = gfx1201")
    ap.add_argument("--rocm", default="10.0.0", choices=sorted(ROCM_STACKS.keys()),
                    help="ROCm wheel 版本系列")
    ap.add_argument("--install", action="store_true",
                    help="真正执行 pip 安装（否则只打印命令）")
    ap.add_argument("--check", action="store_true", help="只体检当前环境")
    args = ap.parse_args()

    if args.check:
        return 0 if check_env() else 1

    if args.install:
        stack = ROCM_STACKS[args.rocm]
        if stack["index"]:
            cmd = [sys.executable, "-m", "pip", "install",
                   "torch[device-%s]==%s" % (args.gfx, stack["torch"]),
                   "torchvision[device-%s]==%s" % (args.gfx, stack["torchvision"]),
                   "torchaudio==%s" % stack["torchaudio"],
                   "--index-url", stack["index"]]
        else:
            base = stack["wheel_base"]
            cmd = [sys.executable, "-m", "pip", "install"]
            for pkg in ("torch", "torchvision", "torchaudio"):
                cmd.append(base + stack["wheel_files"][pkg])
        print("$", " ".join(cmd))
        rc = subprocess.call(cmd)
        if rc != 0:
            print("\n安装失败。先跑 --check 排除驱动/版本问题。")
            return rc
        print()
        check_env()
        return 0

    print_plan(args.gfx, args.rocm)
    return 0


if __name__ == "__main__":
    sys.exit(main())