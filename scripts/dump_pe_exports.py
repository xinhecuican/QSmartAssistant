#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
dump_pe_exports.py —— 列出 PE 文件的导出符号。

用途：llama.cpp 官方只发二进制包（不含头文件和 .lib 导入库），
要直接加载 llama.dll 就得先知道它导出了哪些 C API 符号。
本脚本只依赖标准库，解析 PE 导出目录。

用法：
    python dump_pe_exports.py <path/to/llama.dll> [过滤子串]
"""
import struct
import sys


def exports(path):
    d = open(path, 'rb').read()
    e = struct.unpack_from('<I', d, 0x3c)[0]
    if d[e:e + 4] != b'PE\0\0':
        raise SystemExit('not a PE file')
    coff = e + 4
    nsec = struct.unpack_from('<H', d, coff + 2)[0]
    optsz = struct.unpack_from('<H', d, coff + 16)[0]
    opt = coff + 20
    pe32p = struct.unpack_from('<H', d, opt)[0] == 0x20b
    ddoff = opt + (112 if pe32p else 96)
    edir_rva = struct.unpack_from('<I', d, ddoff)[0]

    so = opt + optsz
    secs = []
    for i in range(nsec):
        b = so + 40 * i
        vsz, va, rsz, ra = struct.unpack_from('<IIII', d, b + 8)
        secs.append((va, vsz, ra, rsz))

    def r2o(rva):
        for va, vsz, ra, rsz in secs:
            if va <= rva < va + max(vsz, rsz):
                return ra + (rva - va)
        return None

    eo = r2o(edir_rva)
    if eo is None:
        return []
    # IMAGE_EXPORT_DIRECTORY 字段偏移：
    #   +0 Characteristics, +4 TimeDateStamp, +8/+10 版本,
    #   +12 Name(DLL 名), +16 Base, +20 NumberOfFunctions,
    #   +24 NumberOfNames, +28 AddressOfFunctions,
    #   +32 AddressOfNames, +36 AddressOfNameOrdinals
    # 注意名字指针表在 +32，+12 是 DLL 自身的名字 —— 弄错会读到字符串字面量。
    num_names = struct.unpack_from('<I', d, eo + 24)[0]
    names_rva = struct.unpack_from('<I', d, eo + 32)[0]
    names_off = r2o(names_rva)
    if names_off is None:
        return []

    out = []
    for i in range(num_names):
        rva = struct.unpack_from('<I', d, names_off + 4 * i)[0]
        off = r2o(rva)
        if off is None:
            continue
        out.append(d[off:d.index(b'\0', off)].decode('ascii', 'replace'))
    return sorted(out)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    flt = sys.argv[2] if len(sys.argv) > 2 else ''
    names = exports(path)
    if flt:
        names = [n for n in names if flt in n]
    for n in names:
        print(n)
    print('---- total: %d ----' % len(names))
    return 0


if __name__ == '__main__':
    sys.exit(main())