#!/usr/bin/env python3
"""Checks a Windows package: every DLL that an .exe or .dll in the zip imports must be in the zip or be a Windows system DLL,
and the Visual C++ runtime must be shipped if it is imported.

usage: windows_package_check.py ZIP

Reads the PE import tables itself, so it runs on any system.
"""
import re
import struct
import sys
import zipfile

# DLLs that are part of every supported Windows (10 and newer); anything else has to be in the package
SYSTEM = {
    "advapi32.dll", "bcrypt.dll", "cfgmgr32.dll", "combase.dll", "comctl32.dll", "comdlg32.dll", "crypt32.dll",
    "dbghelp.dll", "dwmapi.dll", "dxgi.dll", "gdi32.dll", "imm32.dll", "iphlpapi.dll", "kernel32.dll", "msvcrt.dll",
    "ntdll.dll", "ole32.dll", "oleaut32.dll", "opengl32.dll", "rpcrt4.dll", "setupapi.dll", "shell32.dll",
    "shlwapi.dll", "ucrtbase.dll", "user32.dll", "userenv.dll", "uxtheme.dll", "version.dll", "winmm.dll",
    "ws2_32.dll", "wldap32.dll", "wininet.dll", "cabinet.dll", "lz32.dll", "normaliz.dll", "secur32.dll", "psapi.dll", "mswsock.dll", "winhttp.dll",
}
SYSTEM_PREFIXES = ("api-ms-win-", "ext-ms-win-")
# not part of Windows: the Visual C++ Redistributable has to come with the package
VC_RUNTIME = re.compile(r"^(vcruntime|msvcp|msvcr|vcomp|concrt|vccorlib)\d+", re.I)


def imports(data):
    if data[:2] != b"MZ":
        return None
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        return None
    nsections, = struct.unpack_from("<H", data, pe + 6)
    optsize, = struct.unpack_from("<H", data, pe + 20)
    opt = pe + 24
    magic, = struct.unpack_from("<H", data, opt)
    dirs = opt + (112 if magic == 0x20B else 96)
    sections = []
    for i in range(nsections):
        base = opt + optsize + 40 * i
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, base + 8)
        sections.append((va, max(vsize, rawsize), raw))

    def offset(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError(f"bad RVA {rva:#x}")

    def cstring(off):
        return data[off:data.index(b"\0", off)].decode("ascii", "replace")

    names = []
    # regular imports (directory 1) and delay-load imports (directory 13)
    for index, size, name_at, step in ((1, 20, 12, 20), (13, 32, 4, 32)):
        rva, length = struct.unpack_from("<II", data, dirs + 8 * index) if dirs + 8 * index + 8 <= len(data) else (0, 0)
        if not rva:
            continue
        off = offset(rva)
        while True:
            entry = data[off:off + size]
            if len(entry) < size or not any(entry):
                break
            name_rva, = struct.unpack_from("<I", entry, name_at)
            names.append((cstring(offset(name_rva)), index == 13))
            off += step
    return names


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    z = zipfile.ZipFile(sys.argv[1])
    shipped = {n.rsplit("/", 1)[-1].lower() for n in z.namelist()}
    failures = []
    count = 0
    for name in sorted(z.namelist()):
        if not name.lower().endswith((".exe", ".dll")):
            continue
        found = imports(z.read(name))
        if found is None:
            failures.append(f"{name}: not a PE file")
            continue
        count += 1
        base = name.rsplit("/", 1)[-1]
        print(f"{base}: " + ", ".join(sorted({d + ('*' if delayed else '') for d, delayed in found}, key=str.lower)))
        for dll, delayed in found:
            low = dll.lower()
            if low in shipped:
                continue
            if VC_RUNTIME.match(low):
                failures.append(f"{base}: needs {dll}, the Visual C++ Redistributable is not in the package")
            elif low in SYSTEM or low.startswith(SYSTEM_PREFIXES):
                continue
            else:
                failures.append(f"{base}: imports {dll}, which is neither in the package nor a known Windows DLL" + (" (delay-loaded)" if delayed else ""))
    if not count:
        failures.append("no exe or dll found")
    print("(* = delay-loaded)")
    for f in sorted(set(failures)):
        print(f"unexpected: {f}", file=sys.stderr)
    print("windows package check: " + ("FAILED" if failures else "ok"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
