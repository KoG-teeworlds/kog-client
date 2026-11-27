#!/usr/bin/env python3
"""What the engine costs per scene: frame time, allocations per frame, and CPU and wakeups while nothing happens. macOS only.

Usage:
  scripts/kog/bench_engine.py --client build/KoG-Client [--scenes menu,hud64] [--runs 3] [--seconds 14] [--check] [--markdown]

Scenes:
  menu    the start page, no hooks needed
  hud64   the scoreboard with 64 fake players, chat and kill feed; needs a client built with the dev hooks of
          scripts/kog/dev/README.md (the hooks fake a game state offline); hud128 is the same with 128 players

Per scene and run, three measurements, each in its own run of the client with a temporary HOME:
  frame time  `benchmark_quit` with vsync off and no frame limit: median and p95 frame time in microseconds (the first fifth
              of the frames is dropped). The loop of the client draws every frame under `benchmark_quit`, so this is the cost
              of drawing and not of waiting.
  allocations malloc, calloc and realloc per frame in steady state (scripts/kog/dev/malloc_count.c), same setup.
  idle        the window as the player has it: default frame limit and vsync, no benchmark. CPU percent of one core and
              context switches (the wakeups) per second over `--seconds`, and the frames per second drawn. Only for `menu`.

With --check the medians over the runs are compared with scripts/kog/bench_engine_limits.json and the script exits 1 when one is
above its limit. docs/CLIENT-ENGINE.md says how the limits are chosen.
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = "gfx_fullscreen 0;gfx_screen_width 1280;gfx_screen_height 720;snd_enable 0;cl_save_settings 0;cl_show_welcome 0;cl_menu_map \"auto\""
UNCAPPED = BASE + ";gfx_vsync 0;cl_refresh_rate 0;cl_refresh_rate_inactive 0"

SCENES = {
    "menu": {},
    "hud64": {"KOG_HUD": "1", "KOG_FIXTURE": "64"},
    "hud128": {"KOG_HUD": "1", "KOG_FIXTURE": "128"},
}


def build(source, name, work):
    library = os.path.join(work, name + ".dylib")
    subprocess.run(["cc", "-dynamiclib", "-undefined", "dynamic_lookup", "-o", library, os.path.join(HERE, "dev", source)], check=True)
    return library


def frame_time(client, env_extra, seconds):
    with tempfile.TemporaryDirectory() as home:
        result = os.path.join(home, "frames.txt")
        env = dict(os.environ, HOME=home, **env_extra)
        subprocess.run([client, UNCAPPED, f"benchmark_quit {seconds} {result}"], env=env, cwd=home, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=seconds + 120, check=False)
        frames = sorted(int(line.split()[1]) for line in open(result) if line.startswith("Frametime"))
    frames = frames[len(frames) // 5:]
    if len(frames) < 100:
        raise RuntimeError("too few frames: does the client start, and does it have the hooks for this scene?")
    return {"frame_median_us": frames[len(frames) // 2], "frame_p95_us": frames[int(len(frames) * 0.95)]}


def allocations(client, env_extra, seconds):
    with tempfile.TemporaryDirectory() as home:
        library = build("malloc_count.c", "libmalloc_count", home)
        counts = os.path.join(home, "counts.txt")
        frames = os.path.join(home, "frames.txt")
        env = dict(os.environ, HOME=home, DYLD_INSERT_LIBRARIES=library, MALLOC_COUNT_FILE=counts, **env_extra)
        subprocess.run([client, UNCAPPED, f"benchmark_quit {seconds} {frames}"], env=env, cwd=home, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=seconds + 120, check=False)
        samples = [[float(x) for x in line.split()] for line in open(counts) if line.strip()]
        frame_count = sum(1 for line in open(frames) if line.startswith("Frametime"))
    start = next(s for s in samples if s[0] >= 4.0)
    end = samples[-1]
    rate = (sum(end[1:]) - sum(start[1:])) / (end[0] - start[0])
    return {"allocations_per_frame": rate / (frame_count / seconds)}


def top_sample(pid, seconds):
    out = subprocess.run(["top", "-l", "2", "-s", str(seconds), "-pid", str(pid), "-stats", "pid,cpu,csw"], capture_output=True, text=True).stdout
    rows = [line.split() for line in out.split("\n") if re.match(r"^\d+\s", line)]
    if len(rows) < 2:
        raise RuntimeError("top gave no samples")
    first = int(rows[0][2].rstrip("+"))
    second = int(rows[1][2].rstrip("+"))
    return float(rows[1][1]), (second - first) / seconds


def idle(client, env_extra, seconds):
    with tempfile.TemporaryDirectory() as home:
        library = build("frame_count.c", "libframe_count", home)
        counter = os.path.join(home, "frames")
        env = dict(os.environ, HOME=home, DYLD_INSERT_LIBRARIES=library, FRAME_COUNT_FILE=counter, **env_extra)
        process = subprocess.Popen([client, BASE], env=env, cwd=home, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(10)
            before = int(open(counter).read())
            cpu, csw = top_sample(process.pid, seconds)
            after = int(open(counter).read())
        finally:
            process.terminate()
            process.wait(timeout=30)
    return {"idle_cpu_percent": cpu, "idle_wakeups_per_s": csw, "idle_fps": (after - before) / (seconds + 1)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client", required=True)
    parser.add_argument("--scenes", default="menu")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--seconds", type=int, default=14)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--markdown", action="store_true")
    args = parser.parse_args()
    client = os.path.abspath(args.client)
    limits = json.load(open(os.path.join(HERE, "bench_engine_limits.json"))) if args.check else {}

    failures = []
    table = []
    for scene in args.scenes.split(","):
        env = SCENES[scene]
        runs = []
        for _ in range(args.runs):
            result = {}
            result.update(frame_time(client, env, args.seconds))
            result.update(allocations(client, env, args.seconds))
            if scene == "menu":
                result.update(idle(client, env, args.seconds))
            runs.append(result)
        medians = {key: statistics.median(run[key] for run in runs) for key in runs[0]}
        table.append((scene, medians))
        for key, limit in limits.get(scene, {}).items():
            if medians.get(key, 0) > limit:
                failures.append(f"{scene}: {key} {medians[key]:.1f} is above the limit {limit}")
    keys = sorted({key for _scene, medians in table for key in medians})
    if args.markdown:
        print("| scene | " + " | ".join(keys) + " |")
        print("| --- | " + " | ".join(["---"] * len(keys)) + " |")
        for scene, medians in table:
            print(f"| {scene} | " + " | ".join(f"{medians[k]:.1f}" if k in medians else "" for k in keys) + " |")
    else:
        for scene, medians in table:
            print(scene + ": " + ", ".join(f"{k} {v:.1f}" for k, v in sorted(medians.items())))
    for failure in failures:
        print("FAIL " + failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
