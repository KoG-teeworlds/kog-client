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

## What changed

### Pacing of the offline client (`src/engine/shared/kog_pacing.h`)

When no game is running (state offline: menus, server browser, settings), the loop

* draws at 60 frames per second with a focused window and 30 with an unfocused one, or lower when the player set
  `cl_refresh_rate` or `gfx_refresh_rate` lower (they can only lower it);
* draws nothing while the window is minimised or hidden, and wakes every 250 ms to serve the network, jobs and downloads;
* waits for events between frames (`IEngineInput::WaitForEvent`, `SDL_WaitEventTimeout`) instead of spinning, so a key or
  mouse event ends the wait at once and is handled in the same iteration;
* draws at once when the window becomes visible or the focus state changes (`CPacing` compares the state of the last frame),
  and on `RequestRedraw()`.

It does **not** apply, and the old loop runs unchanged, when a game is connected, connecting, loading or a demo plays (state
not offline), in the editor, when video is recorded, under `benchmark_quit`, or when `gfx_backgroundrender 0` and the window
is closed. Screenshots in a visible window work as before (a frame is drawn at least every 33 ms); the screenshot
of a minimised window waits until it is visible again.

The "dirty / animating" contract: the menu has a live background map, so an offline visible window is always animating and
its pace is the rate above. A page without background would be static; deciding that needs every animation of the UI to
announce itself, and a missed one is a stale frame. The contract is therefore only the small part that is needed: whoever
changes what is on screen outside of an input event calls `CPacing::RequestRedraw()`, and a frame is drawn at the next
iteration. Moving the pace to zero for static pages is possible on top of this when the UI code reports its animations
(a follow-up for whoever owns the UI).

Not event-driven yet, because it could not be verified here: the game view. While online the main loop still runs without a
wait when `cl_refresh_rate` is 0. Waiting there needs one wait that ends on a packet, on an input event and on the end of
the render thread's frame; today there are three separate primitives (socket, SDL, condition variable). The design is a
wake-up through `SDL_PushEvent` from a socket thread and from the render thread, which touches the network code and needs a
server to test; it is the next step with the largest gain on laptops (a full core while playing).

### Strictness

* `KOG_STRICT_SOURCES` in `CMakeLists.txt`: the engine files that are ours compile with `-Wall -Wextra -Wconversion
  -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-qual` under clang (sign conversion and 64 to 32 bit narrowing
  included: the 37 findings of `kog_login.cpp`, `kog_update.cpp` and `kog_keyfile.cpp` are fixed with explicit casts of values that
  are non-negative or bounded by the buffer they describe). `-DKOG_WERROR=ON` makes them errors; CI sets it. The findings in `kog_update.cpp` that were real (byte pushes without a
  cast) are fixed.
* `scripts/kog/check_engine.sh <build> --changed <base>`: the same warnings on the `kog_*.cpp` files a branch adds or changes,
  and clang-tidy (`scripts/kog/clang-tidy-engine.yaml`: bugprone, performance, nodiscard, member init, narrowing) on the ones
  it adds. `--all` runs both on the whole list. Upstream files are not checked: their headers do not pass `-Wconversion`
  (thousands of findings). The older KoG files still have tidy findings (uninitialised locals, missing const, signed
  bitwise operators in `kog_update.cpp`), which is why tidy gates only new files.
* `.github/workflows/kog-engine-checks.yml` runs that on pull requests that touch the engine. Verified locally with Apple
  clang and clang-tidy 23; the Linux job is only verified by running it.
* Not done: TSan run of the render thread hand-off, RAII for the graphics handles (texture, buffer and quad container
  indices are plain ints owned by the callers), `std::span` for the bounds of the command buffer.

## The game loop while online: design, and why it is not changed

While online with `cl_refresh_rate 0` the main loop still spins (a full core, and the AppKit event pump allocating about 90
times per pass). The pacing of this document does not apply there. The change that would fix it, and what stopped it:

**Design.** One wait with three wake-up sources and the frame limiter as the upper bound:

1. input and window events: the wait is `SDL_WaitEventTimeout`, which is what the offline pacing already uses;
2. a received packet: a small thread waits in `net_socket_read_wait` on the socket of the connection and posts one
   `SDL_UserEvent` with `SDL_PushEvent` (thread safe), then waits for the loop to take the packet before it waits again, so a
   readable socket does not post events in a loop; the thread ends when the connection does. `net_socket_read_wait` has a
   timeout, so ending it takes at most that timeout (100 ms) unless the socket is closed first;
3. the end of the frame of the render thread: `CGraphicsBackend_Threaded::ThreadFunc` posts the same kind of event after
   `RunBuffer` (it already notifies a condition variable at that point).

The deadline of the wait is the earliest of the next prediction tick (input has to be sent on time), the next frame of the
frame limiter (`cl_refresh_rate`, `gfx_refresh_rate`, vsync stays with the render thread), and the housekeeping deadline.

**Why it is not in this series.** It changes when packets are read, when input is sent and when frames start while a game
runs, and that can only be judged against a running server: frame interval jitter (p99), ping as the server browser and the
scoreboard show it, input sent late by a tick. This work had no server to play on. The server is not built on macOS, the
container recipe needs protobuf and redis-plus-plus built from source and a KoG gateway stand-in, and joining a public server
from a measuring script was not authorised. Shipping it untested is a risk to every player, the gain is battery life, so it
waits for a test setup: a KoG-style server in the Linux container, the macOS client on a `connect 127.0.0.1:8303`, and
`bench_engine.py` extended with the scene "online" (frame interval p50/p99 from `benchmark_quit`, CPU, wakeups) run before and
after.

## Merge points (upstream-shared files)

| file | change | size |
| --- | --- | --- |
| `src/engine/client/client.cpp` | include, `KogPacing::CPacing Pacing`, `Paced` flag in the render block, `WaitForEvent` in the wait | about 30 lines in `CClient::Run` |
| `src/engine/client/input.cpp`, `input.h` | `WaitForEvent`, `m_WindowHidden` kept from the window events | about 25 lines |
| `src/engine/input.h` | two virtual functions on `IEngineInput` | 4 lines |
| `CMakeLists.txt` | `KOG_STRICT_SOURCES`, `KOG_WERROR`, the header and the test | about 25 lines |

Everything else is in new KoG files. A merge conflict in `CClient::Run` is resolved by keeping upstream's loop and re-adding
the `Paced` block: it only overrides `RenderDue`, `AsyncRenderOld`, `WakeTime` and the wait.

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
