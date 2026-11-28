#!/usr/bin/env python3
"""Checks a macOS app bundle: every Mach-O file may load only system libraries or libraries inside the bundle,
may have no absolute RPATH, and must not need a macOS newer than --max-minos.

usage: macos_package_check.py APP [--max-minos 12.0]

Reads the Mach-O load commands itself, so it also runs on Linux (for a DMG unpacked with 7z).
"""
import argparse
import os
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE
FAT_MAGIC_64 = 0xCAFEBABF
LC_REQ_DYLD = 0x80000000
LC_LOAD_DYLIB = 0x0C
LC_ID_DYLIB = 0x0D
LC_LOAD_WEAK_DYLIB = 0x18 | LC_REQ_DYLD
LC_REEXPORT_DYLIB = 0x1F | LC_REQ_DYLD
LC_RPATH = 0x1C | LC_REQ_DYLD
LC_VERSION_MIN_MACOSX = 0x24
LC_BUILD_VERSION = 0x32
PLATFORM_MACOS = 1
CPU = {0x01000007: "x86_64", 0x0100000C: "arm64"}
SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/")


def version(v):
    return (v >> 16, (v >> 8) & 0xFF)


def slices(data):
    magic = struct.unpack_from(">I", data, 0)[0]
    if magic in (FAT_MAGIC, FAT_MAGIC_64):
        count = struct.unpack_from(">I", data, 4)[0]
        for i in range(count):
            if magic == FAT_MAGIC:
                _, _, offset, size, _ = struct.unpack_from(">iiIII", data, 8 + 20 * i)
            else:
                _, _, offset, size, _, _ = struct.unpack_from(">iiQQII", data, 8 + 32 * i)
            yield data[offset:offset + size]
    elif struct.unpack_from("<I", data, 0)[0] == MH_MAGIC_64:
        yield data


def parse(sl):
    cputype, _sub, _ftype, ncmds, _size, _flags, _res = struct.unpack_from("<iiIIIII", sl, 4)
    info = {"cpu": CPU.get(cputype, hex(cputype)), "libs": [], "rpaths": [], "id": None, "minos": None}
    offset = 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", sl, offset)
        body = sl[offset:offset + size]
        if cmd in (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_ID_DYLIB):
            name = body[struct.unpack_from("<I", body, 8)[0]:].split(b"\0")[0].decode()
            if cmd == LC_ID_DYLIB:
                info["id"] = name
            else:
                info["libs"].append(name)
        elif cmd == LC_RPATH:
            info["rpaths"].append(body[struct.unpack_from("<I", body, 8)[0]:].split(b"\0")[0].decode())
        elif cmd == LC_BUILD_VERSION:
            platform, minos = struct.unpack_from("<II", body, 8)
            if platform == PLATFORM_MACOS:
                info["minos"] = version(minos)
        elif cmd == LC_VERSION_MIN_MACOSX:
            info["minos"] = version(struct.unpack_from("<I", body, 8)[0])
        offset += size
    return info


def expand(path, binary, app):
    if path.startswith("@loader_path/"):
        return os.path.normpath(os.path.join(os.path.dirname(binary), path[len("@loader_path/"):]))
    if path.startswith("@executable_path/"):
        return os.path.normpath(os.path.join(app, "Contents/MacOS", path[len("@executable_path/"):]))
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app")
    ap.add_argument("--max-minos", default="12.0")
    args = ap.parse_args()
    app = os.path.abspath(args.app)
    limit = tuple(int(x) for x in args.max_minos.split("."))
    failures = []
    seen = 0

    machos = []
    for root, _dirs, files in os.walk(app):
        for name in sorted(files):
            path = os.path.join(root, name)
            if os.path.islink(path) or not os.path.isfile(path):
                continue
            with open(path, "rb") as f:
                head = f.read(4)
                if head not in (b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf", b"\xcf\xfa\xed\xfe"):
                    continue
                data = head + f.read()
            machos.append((path, [parse(sl) for sl in slices(data)]))

    # dyld also searches the RPATHs of the executable that loaded a library
    inherited = []
    for path, infos in machos:
        if os.path.dirname(path) == os.path.join(app, "Contents/MacOS"):
            for info in infos:
                inherited += [expand(r, path, app) for r in info["rpaths"]]

    for path, infos in machos:
        rel = os.path.relpath(path, app)
        if True:
            for info in infos:
                seen += 1
                minos = ".".join(map(str, info["minos"])) if info["minos"] else "?"
                print(f"{rel} [{info['cpu']}] minos {minos}: {len(info['libs'])} libraries")
                if info["minos"] is None or info["minos"] > limit:
                    failures.append(f"{rel} [{info['cpu']}]: minimum macOS {minos}, the limit is {args.max_minos}")
                for r in info["rpaths"]:
                    if not r.startswith(("@loader_path/", "@executable_path/")):
                        failures.append(f"{rel} [{info['cpu']}]: RPATH {r}")
                for lib in info["libs"]:
                    if lib.startswith(SYSTEM_PREFIXES):
                        continue
                    if lib.startswith("@rpath/"):
                        dirs = [expand(r, path, app) for r in info["rpaths"]] + inherited
                        if any(d and os.path.exists(os.path.join(d, lib[len("@rpath/"):])) for d in dirs):
                            continue
                        failures.append(f"{rel} [{info['cpu']}]: {lib} is not found through its RPATHs")
                        continue
                    target = expand(lib, path, app)
                    if target and os.path.exists(target):
                        continue
                    failures.append(f"{rel} [{info['cpu']}]: {lib}")

    if not seen:
        failures.append("no Mach-O file found")
    for f in sorted(set(failures)):
        print(f"unexpected: {f}", file=sys.stderr)
    print("macos package check: " + ("FAILED" if failures else "ok"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
