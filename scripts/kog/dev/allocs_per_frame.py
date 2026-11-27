#!/usr/bin/env python3
"""Allocations per frame in steady state, macOS only (an interposer counts malloc, calloc and realloc).

Usage: scripts/kog/dev/allocs_per_frame.py --client build/KoG-Client [--theme 1] [--fixture 64] [--seconds 14]

Builds libmalloc_count.dylib from malloc_count.c in a temporary directory, runs the client with `benchmark_quit` and the
interposer (the client must not be started through a system tool such as /usr/bin/timeout: SIP drops DYLD_INSERT_LIBRARIES there),
and prints the allocations per frame between second 4 and the end. A fixture needs the dev hooks of README.md; without
--fixture the client draws the start page. Counts include the engine (the command buffer of the graphics code allocates), so
compare a scene with its classic twin (--theme 0) and with an empty scene, do not expect zero.
"""
import argparse
import os
import subprocess
import sys
import tempfile

BASE = "gfx_fullscreen 0;gfx_screen_width 1280;gfx_screen_height 720;gfx_background_render 1;gfx_vsync 0;snd_enable 0;cl_save_settings 0;cl_show_welcome 0;cl_refresh_rate 0;cl_refresh_rate_inactive 0;cl_menu_map \"auto\""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client", required=True)
    parser.add_argument("--theme", type=int, default=1)
    parser.add_argument("--fixture", type=int, default=0)
    parser.add_argument("--seconds", type=int, default=14)
    args = parser.parse_args()
    client = os.path.abspath(args.client)
    source = os.path.join(os.path.dirname(os.path.abspath(__file__)), "malloc_count.c")
    with tempfile.TemporaryDirectory() as work:
        library = os.path.join(work, "libmalloc_count.dylib")
        subprocess.run(["cc", "-dynamiclib", "-o", library, source], check=True)
        counts = os.path.join(work, "counts.txt")
        frames = os.path.join(work, "frames.txt")
        env = dict(os.environ, HOME=work, DYLD_INSERT_LIBRARIES=library, MALLOC_COUNT_FILE=counts)
        if args.fixture:
            env.update(KOG_HUD="1", KOG_FIXTURE=str(args.fixture))
        subprocess.run([client, f"{BASE};cl_kog_theme {args.theme}", f"benchmark_quit {args.seconds} {frames}"], env=env, cwd=work, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        samples = [[float(x) for x in line.split()] for line in open(counts) if line.strip()]
        frame_count = sum(1 for line in open(frames) if line.startswith("Frametime"))
    start = next(s for s in samples if s[0] >= 4.0)
    end = samples[-1]
    seconds = end[0] - start[0]
    rate = (sum(end[1:]) - sum(start[1:])) / seconds
    fps = frame_count / args.seconds
    print(f"theme {args.theme}, fixture {args.fixture}: {frame_count} frames, {fps:.0f} fps, {rate:.0f} allocations per second, {rate / fps:.1f} per frame")
    return 0


if __name__ == "__main__":
    sys.exit(main())
