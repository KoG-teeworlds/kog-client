# The client engine: what it costs, what we changed

Measured on macOS (Apple Silicon, OpenGL 3.3 on top of Metal), 1280x720 window, release build with symbols. Linux and
Windows are checked by CI only; nothing here was run there. Numbers are medians of two or three runs and noisy by 5 to 10
percent; the measuring tools are in `scripts/kog/` (see "Measuring").

## What the measurements say

### Where the allocations come from

Steady state, allocations per frame (malloc, calloc, realloc; `scripts/kog/dev/allocs_by_site.py`, call sites by stack walk):

| scene | per frame | where |
| --- | --- | --- |
| start page, empty | about 1150 | spin of the main loop in the AppKit event pump, Apple's OpenGL layer |
| scoreboard, 64 fake players | about 6300 | about 55% the OpenGL layer per draw call (4 blocks per `glDrawElements`), 30 to 45% the event pump, under 3% engine code |

Two findings change the picture drawn at the start of this work:

* The engine does **not** allocate a function object per command. `AddCmd` takes a `std::function` but the lambdas capture one
  reference or nothing and fit the small buffer of libc++; the stack walk finds no allocation there. Pooling the command
  buffer would remove nothing measurable. What the engine allocates itself (text containers: a vector and a buffer object
  per drawn label, the free of it) is about 130 per frame in the HUD scene, under 3%.
* The cost is in **how often the main loop runs and how many draw calls a frame has**. With the default `cl_refresh_rate 0`
  and `gfx_asyncrender_old 1` the main loop does not wait for anything: while the render thread draws, the main thread runs
  `Update()` and `CInput::Update()` in a loop at full speed (`client.cpp`, `WakeTime` stays at its maximum). Each pass
  through `SDL_PollEvent` allocates around 90 times inside AppKit. At 500 frames per second the main thread passes about 30
  times per frame, which is the 30 to 45% of the allocations of the HUD scene, and a full core.
* A tee costs about 8 draw calls (outline and fill of body, two feet, eyes), each with its own texture (the skin parts are
  not in an atlas). 64 tees are about 500 of the roughly 1000 draw calls of the scoreboard; Apple's OpenGL layer allocates
  about 4 blocks per draw call. Fewer draw calls need skin parts in an atlas or a texture array: a change to the skin
  loader, the quad containers and the shaders, which is not done here (see "Not done").

### Idle: menu, window as the player has it

Start page with the live background map, default settings, no benchmark (`scripts/kog/bench_engine.py`, idle):

| window | | frames per s | CPU of one core | context switches per s |
| --- | --- | --- | --- | --- |
| focused | before | 2084 | 169% | 25950 |
| focused | after | 60 | 5.4% | 1240 |
| unfocused (default `cl_refresh_rate_inactive 120`) | before | 112 to 116 | 9.4 to 11% | about 2000 |
| unfocused | after | 29 to 30 | 4.7 to 5.1% | about 1440 |

Before, a focused menu drew 2000 frames per second and kept 1.7 cores busy. A window that is minimised and offline was
drawn like a visible one (SDL reports a minimised window as shown): not measured on its own, it now draws nothing.

### Frame cost of a frame (drawing only, no frame limit)

Start page: median 388 to 399 us per frame, p95 1.7 to 1.9 ms (no change by this work: the benchmark runs the loop as before).
HUD scenes: see `scripts/kog/bench_engine_limits.json` for the values of the build at the time the limits were set.

## Measuring

All macOS only; the client under test must not be started through `/usr/bin/timeout` (SIP drops the interposer).

* `scripts/kog/bench_engine.py --client build/KoG-Client --scenes menu[,hud64,hud128] [--check]`: frame time, allocations per
  frame, and for `menu` idle CPU, wakeups and frames per second. The `hud` scenes need the dev hooks of
  `scripts/kog/dev/README.md`.
* `scripts/kog/dev/allocs_by_site.py`: allocations per frame by call site (`malloc_stacks.c` records stacks, `atos` and
  `dladdr` name them).
* `scripts/kog/dev/allocs_per_frame.py`, `malloc_count.c`, `frame_count.c`: the counters.

### Limits (`scripts/kog/bench_engine_limits.json`)

`--check` fails when the median over the runs is above the limit of a scene. A limit is the value measured on the machine of
the author with 25% to 30% added: noise between runs is 5 to 10% and between sessions (display state, other load) up to 20%.
The limits catch a regression of the kind this work found (a spin, a doubling of the draw calls), not 3%. Re-measure and
change the file in the same pull request as a change that moves a value on purpose. They are not run in CI: the hosted macOS
runners have no display for the window.
