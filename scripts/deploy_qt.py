"""windeployqt 的等价替代：通过解析 PE 导入表做传递闭包，把 Qt 运行时
DLL 与必需插件拷贝到可执行文件目录。

什么时候需要它：极少数受限环境（沙箱、被安全软件拦截的 cmd.exe）里
windeployqt 无法拉起子进程，会报 `Unable to query qtpaths: ... pipe:`。
此时可用本脚本手动完成部署。

用法：
    python scripts/deploy_qt.py <输出目录(含 exe)> <Qt bin 目录> <Qt plugins 目录> [额外 DLL 搜索目录...]

示例：
    python scripts/deploy_qt.py ^
        build\\RelWithDebInfo\\bin ^
        D:\\libs\\qt\\6.12\\6.12.0\\msvc2022_64\\bin ^
        D:\\libs\\qt\\6.12\\6.12.0\\msvc2022_64\\plugins ^
        "%USERPROFILE%\\.config\\QSmartAssistant\\plugins\\lib"

说明：插件 DLL 虽然放在别处、运行时才加载，但也必须作为依赖扫描的根传进来
（放在“额外 DLL 搜索目录”里），否则会漏掉它们依赖的 Qt 模块。
"""
import os
import struct
import shutil
import sys


def imports_of(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        return []
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        return []
    coff = pe + 4
    (_mach, nsec, _ts, _psym, _nsym, opt_size, _chars) = struct.unpack_from(
        "<HHIIIHH", data, coff)
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    ddoff = opt + (112 if magic == 0x20B else 96)
    imp_rva = struct.unpack_from("<I", data, ddoff + 8)[0]
    if imp_rva == 0:
        return []
    secoff = opt + opt_size
    secs = []
    for i in range(nsec):
        o = secoff + 40 * i
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
        secs.append((vaddr, max(vsize, rsize), roff))

    def rva2off(rva):
        for vaddr, size, roff in secs:
            if vaddr <= rva < vaddr + size:
                return roff + (rva - vaddr)
        return None

    off = rva2off(imp_rva)
    if off is None:
        return []
    names = []
    while True:
        ent = data[off:off + 20]
        if len(ent) < 20:
            break
        oft, _ts, _fc, nrva, fthunk = struct.unpack("<IIIII", ent)
        if not any((oft, nrva, fthunk)):
            break
        # IMAGE_IMPORT_DESCRIPTOR: Name 字段在偏移 12，指向 DLL 名字字符串
        no = rva2off(nrva)
        if no is None:
            break
        end = data.index(b"\0", no)
        names.append(data[no:end].decode("latin1"))
        off += 20
    return names


def main():
    out_dir = os.path.abspath(sys.argv[1])
    qt_bin = os.path.abspath(sys.argv[2])
    qt_plugins = os.path.abspath(sys.argv[3])
    extra_dirs = [os.path.abspath(p) for p in sys.argv[4:] if p]

    search_dirs = [out_dir, qt_bin] + extra_dirs
    search = {}
    for d in search_dirs:
        if os.path.isdir(d):
            for fn in os.listdir(d):
                if fn.lower().endswith(".dll"):
                    search.setdefault(fn.lower(), os.path.join(d, fn))

    copied = []
    queue = [os.path.join(out_dir, fn) for fn in os.listdir(out_dir)
             if fn.lower().endswith((".exe", ".dll"))]
    # 插件 DLL 位于别处(如 ~/.config/QSmartAssistant/plugins/lib)，运行时才加载，
    # 但也必须作为依赖扫描的根，否则会漏掉它们依赖的 Qt 模块。
    for d in extra_dirs:
        if os.path.isdir(d):
            queue += [os.path.join(d, fn) for fn in os.listdir(d)
                      if fn.lower().endswith((".exe", ".dll"))]
    seen = set()
    while queue:
        cur = queue.pop()
        key = os.path.basename(cur).lower()
        if key in seen:
            continue
        seen.add(key)
        for dep in imports_of(cur):
            dkey = dep.lower()
            if dkey in seen:
                continue
            src = search.get(dkey)
            if not src:
                continue  # 系统 DLL，跳过
            if os.path.dirname(src) == out_dir:
                # 依赖已在输出目录(例如 lpcommon.dll / sherpa-onnx-*.dll)。
                # 注意：不能在这里直接标记 seen —— 否则该 DLL 自身的依赖
                # (如 lpcommon.dll -> Qt6Multimedia.dll) 永远不会被遍历。
                # 正确做法是把它放进队列，交给循环去扫描一次。
                queue.append(src)
                continue
            dst = os.path.join(out_dir, dep)
            if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(src):
                shutil.copy2(src, dst)
                copied.append(dep)
            queue.append(dst)

    print("== 拷贝的 Qt 运行时 DLL ==")
    for c in sorted(copied):
        print("  ", c)

    # Qt 插件：按需拷贝
    wanted = {
        "platforms": ["qwindows.dll"],
        "multimedia": None,          # 全部
        "tls": None,                 # 全部
        "networkinformation": None,  # 全部
        "styles": ["qmodernwindowsstyle.dll"],
        "imageformats": ["qgif.dll", "qico.dll", "qjpeg.dll"],
        "generic": None,
    }
    print("== 拷贝的 Qt 插件 ==")
    for sub, only in wanted.items():
        src_dir = os.path.join(qt_plugins, sub)
        if not os.path.isdir(src_dir):
            continue
        dst_dir = os.path.join(out_dir, sub)
        files = [fn for fn in os.listdir(src_dir) if fn.lower().endswith(".dll")]
        lower = {f.lower() for f in files}
        for fn in files:
            if only is not None and fn.lower() not in [x.lower() for x in only]:
                continue
            # 跳过 debug 版(xxxd.dll)，当同目录存在 release 版(xxx.dll)时
            if fn.lower().endswith("d.dll") and fn[:-5].lower() + ".dll" in lower:
                continue
            os.makedirs(dst_dir, exist_ok=True)
            shutil.copy2(os.path.join(src_dir, fn), os.path.join(dst_dir, fn))
            print("  ", os.path.join(sub, fn))
    print("deploy done")


if __name__ == "__main__":
    main()
