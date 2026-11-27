#!/usr/bin/env python3
"""Allocations per frame by call site, macOS only (malloc_stacks.c interposer, symbols from atos).

Usage: scripts/kog/dev/allocs_by_site.py --client build/KoG-Client [--seconds 16] [--fixture 64] [--top 25] [--depth 4] [--env KOG_FIXTURE=64] [--config "cl_foo 1"]

Runs the client with `benchmark_quit`, takes the table written at second 4 and the last one, and prints the allocations per frame
that came between them, by call stack (innermost frames first, frames of libc and libc++ skipped). The executable needs its symbols.
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile
from collections import defaultdict

BASE = "gfx_fullscreen 0;gfx_screen_width 1280;gfx_screen_height 720;gfx_backgroundrender 1;gfx_vsync 0;snd_enable 0;cl_save_settings 0;cl_show_welcome 0;cl_refresh_rate 0;cl_refresh_rate_inactive 0;cl_menu_map \"auto\""


def read(path):
    images = []
    frames = 0
    table = {}
    symbols = {}
    for line in open(path).read().split("\n"):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "F":
            frames = int(parts[1])
        elif parts[0] == "I":
            images.append((int(parts[1], 16), line.split(" ", 2)[2]))
        else:
            table[tuple(f.split("=", 1)[0] for f in parts[1:])] = int(parts[0])
            for f in parts[1:]:
                address, _, name = f.partition("=")
                symbols[address] = name
    return sorted(images), table, frames, symbols


def resolve(client, images, addresses):
    names = {}
    per_image = defaultdict(list)
    for a in set(addresses):
        value = int(a, 16)
        owner = max((i for i in images if i[0] <= value), default=None, key=lambda i: i[0])
        if owner and os.path.realpath(owner[1]) == os.path.realpath(client):
            per_image[owner].append(a)
        else:
            names[a] = None
    for (base, path), addrs in per_image.items():
        out = subprocess.run(["atos", "-o", path, "-l", hex(base)] + addrs, capture_output=True, text=True).stdout.split("\n")
        names.update(zip(addrs, out))
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client", required=True)
    parser.add_argument("--seconds", type=int, default=16)
    parser.add_argument("--top", type=int, default=25)
    parser.add_argument("--depth", type=int, default=4)
    parser.add_argument("--fixture", type=int, default=0)
    parser.add_argument("--env", action="append", default=[])
    parser.add_argument("--config", default="")
    args = parser.parse_args()
    client = os.path.abspath(args.client)
    here = os.path.dirname(os.path.abspath(__file__))
    totals = subprocess.run([os.path.join(here, "allocs_per_frame.py"), "--client", client, "--fixture", str(args.fixture), "--seconds", "14"], capture_output=True, text=True).stdout
    match = re.search(r"([0-9.]+) per frame", totals)
    per_frame = float(match.group(1)) if match else None
    source = os.path.join(os.path.dirname(os.path.abspath(__file__)), "malloc_stacks.c")
    with tempfile.TemporaryDirectory() as work:
        library = os.path.join(work, "libmalloc_stacks.dylib")
        subprocess.run(["cc", "-dynamiclib", "-O1", "-undefined", "dynamic_lookup", "-o", library, source], check=True)
        frames = os.path.join(work, "frames.txt")
        env = dict(os.environ, HOME=work, DYLD_INSERT_LIBRARIES=library, MALLOC_STACKS_FILE=os.path.join(work, "stacks"))
        if args.fixture:
            env.update(KOG_HUD="1", KOG_FIXTURE=str(args.fixture))
        env.update(e.split("=", 1) for e in args.env)
        config = BASE + (";" + args.config if args.config else "")
        subprocess.run([client, config, f"benchmark_quit {args.seconds} {frames}"], env=env, cwd=work, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        frame_count = sum(1 for line in open(frames) if line.startswith("Frametime")) if os.path.exists(frames) else 0
        snapshots = sorted(glob.glob(os.path.join(work, "stacks.*")), key=lambda p: int(p.rsplit(".", 1)[1]))
        if len(snapshots) < 3:
            print("too few snapshots", file=sys.stderr)
            return 1
        images, first, first_frames, _ = read(snapshots[1])
        _, last, last_frames, symbols = read(snapshots[-1])
    delta = {k: v - first.get(k, 0) for k, v in last.items() if v > first.get(k, 0)}
    total = sum(delta.values())
    names = resolve(client, images, [a for stack in delta for a in stack])
    skip = re.compile(r"^(operator new|std::__1::|_malloc|malloc|calloc|realloc|_os_|__|CountedM|CountedC|CountedR|\+\[|-\[|start)")
    def short(a):
        name = names.get(a)
        if name is None:
            name = symbols.get(a, "-")
            return "" if name == "-" or re.match(r"^(_?malloc|_?calloc|_?realloc|_os_|_?free|operator new|_Znw|_Zna|_ZNSt|objc_|_objc|_?pthread|_?dispatch|_?CF|_NS|CFRunLoop|__)", name) else name
        if not name:
            return ""
        name = re.sub(r" \(in [^)]*\)( \(.*\))?", "", name)
        return name
    by_site = defaultdict(int)
    for stack, count in delta.items():
        kept = [short(a) for a in stack if short(a) and not skip.match(short(a))][: args.depth]
        by_site[" < ".join(kept) if kept else "(system only)"] += count
    frames_in_window = max(last_frames - first_frames, 1)
    print(f"{total / frames_in_window:.1f} allocations per frame in {frames_in_window} frames (call-site run, slowed by the stack walks); malloc_count run: {per_frame}")
    scale = 1.0 / frames_in_window
    for site, count in sorted(by_site.items(), key=lambda kv: -kv[1])[: args.top]:
        print(f"{count * scale:8.1f}  {100.0 * count / total:5.1f}%  {site}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
