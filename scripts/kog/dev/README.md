# Dev tools for measuring the HUD

These are for developers. Nothing here is part of the client you ship, and the hooks are not applied to any branch.

## Benchmarks

`scripts/kog/bench_hud.py --client build/KoG-Client --scenes menu` measures the start page against the classic look with the
client's own `benchmark_quit`. The `hud` scenes (scoreboard with 16, 64 and 128 players, chat and kill feed) need fake game
state, which a client only has online; for that, a client built with the hooks below.

## The hooks (`hud-bench-hooks.patch`)

`git apply scripts/kog/dev/hud-bench-hooks.patch` on the branch you measure. It adds, to the client only:

- `components/kog/kog_fixture.{h,cpp}`: with `KOG_FIXTURE=<n>` in the environment, fills the game client with n fake players, six
  chat lines and four kills, once, and keeps the fixture in the snapshot.
- gates in the scoreboard, chat and kill feed that let them render offline when `KOG_FIXTURE` is set (`KOG_NOSCORE=1` keeps the
  scoreboard closed), a title for the scoreboard without a map, and `KOG_HUD=1`, which draws the menu background first and
  skips the menu so that the HUD is on top of it;
- `KOG_PAGE=<page>` opens a menu page at start (15 is Settings, 6 the server browser);
- `screenshot` works in a window that is not active (the check is removed), which lets a headless session take pictures.

Keep the patch out of commits: `git apply` it, build to a separate directory, measure, `git checkout .` and delete the new files.

## Allocations

`scripts/kog/dev/allocs_per_frame.py --client build/KoG-Client --fixture 64` builds `malloc_count.c` (an interposer that counts
`malloc`, `calloc` and `realloc`) and prints the allocations per frame in steady state. macOS only.

## Layout checks

`scripts/kog/check_label_lengths.py` and `scripts/kog/check_hud_label_lengths.py` read `data/languages/*.txt` and list labels
that would be cut or shrunk.
