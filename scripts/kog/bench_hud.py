#!/usr/bin/env python3
"""Frame times of the KoG look against the classic look, with the client's own `benchmark_quit`.

Usage:
  scripts/kog/bench_hud.py --client build/KoG-Client [--runs 3] [--seconds 12] [--scenes menu,hud] [--markdown]

Scenes:
  menu   the start page, no hooks needed
  hud    the scoreboard, chat and kill feed with fake players: needs a client built with the dev hooks of
         scripts/kog/dev/README.md (the hooks are not part of the product: they fake a game state offline)

Every scene runs once per theme (cl_kog_theme 0 = classic, 1 = KoG dark, 2 = KoG light) and `runs` times; the table shows the
median over the runs of the median, p95 and p99 frame time in microseconds (the first fifth of the frames of a run is dropped:
start-up). The window is 1280x720 with vsync off and no frame limit, so the numbers are the cost of drawing, not of waiting.
A temporary HOME keeps the run away from your settings. Runs are noisy (about 5 to 10 percent); compare medians of several runs.
"""
import argparse
import os
import statistics
import subprocess
import sys
import tempfile

BASE = "gfx_fullscreen 0;gfx_screen_width 1280;gfx_screen_height 720;gfx_background_render 1;gfx_vsync 0;snd_enable 0;cl_save_settings 0;cl_show_welcome 0;cl_refresh_rate 0;cl_refresh_rate_inactive 0;cl_menu_map \"auto\""

SCENES = {
    "menu": [("start page", {}, "")],
    "hud": [
        ("scoreboard 16 players", {"KOG_HUD": "1", "KOG_FIXTURE": "16"}, ""),
        ("scoreboard 64 players", {"KOG_HUD": "1", "KOG_FIXTURE": "64"}, ""),
        ("scoreboard 128 players", {"KOG_HUD": "1", "KOG_FIXTURE": "128"}, ""),
        ("chat and kill feed (64 players)", {"KOG_HUD": "1", "KOG_FIXTURE": "64", "KOG_NOSCORE": "1"}, ""),
    ],
}
THEMES = [(0, "classic"), (1, "dark"), (2, "light")]


def run_once(client, theme, env_extra, seconds):
    with tempfile.TemporaryDirectory() as home:
        result = os.path.join(home, "frames.txt")
        env = dict(os.environ, HOME=home)
        env.update(env_extra)
        command = [client, f"{BASE};cl_kog_theme {theme}", f"benchmark_quit {seconds} {result}"]
        subprocess.run(command, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=seconds + 90, cwd=home, check=False)
        frames = [int(line.split()[1]) for line in open(result) if line.startswith("Frametime")]
    frames = sorted(frames[len(frames) // 5:])
    if len(frames) < 100:
        raise RuntimeError("too few frames: does the client start, and does it have the hooks for this scene?")
    count = len(frames)
    return frames[count // 2], frames[int(count * 0.95)], frames[int(count * 0.99)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client", required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--seconds", type=int, default=12)
    parser.add_argument("--scenes", default="menu")
    parser.add_argument("--markdown", action="store_true")
    args = parser.parse_args()

    rows = []
    for group in args.scenes.split(","):
        for name, env, _ in SCENES[group]:
            cells = []
            for theme, _label in THEMES:
                runs = [run_once(args.client, theme, env, args.seconds) for _ in range(args.runs)]
                cells.append(tuple(int(statistics.median(run[i] for run in runs)) for i in range(3)))
            rows.append((name, cells))
    header = ["scene"] + [f"{label} median / p95 / p99 (us)" for _theme, label in THEMES]
    if args.markdown:
        print("| " + " | ".join(header) + " |")
        print("| " + " | ".join(["---"] * len(header)) + " |")
        for name, cells in rows:
            print(f"| {name} | " + " | ".join(" / ".join(str(v) for v in cell) for cell in cells) + " |")
    else:
        for name, cells in rows:
            print(name)
            for (_theme, label), cell in zip(THEMES, cells):
                print(f"  {label:8} median {cell[0]:6}  p95 {cell[1]:6}  p99 {cell[2]:6}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
