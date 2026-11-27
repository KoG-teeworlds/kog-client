# Client UI direction: from black slabs to game glass

Status: built for the HUD, menu views, controls, popups and the start page. What is not done or not seen is in section 8.
Code: `src/game/client/kog_style.h` (tokens), `kog_draw.{h,cpp}` (primitives), `kog_theme.h`, `kog_hud.h` (what components ask).
Tests: `src/test/kog_style_test.cpp`, `kog_draw_test.cpp`, `kog_hud_test.cpp`. Measurements: `scripts/kog/bench_hud.py`.

## 1. What the owner saw, and why it was right

Feedback: "it doesn't look too good, make it fit the game, look at ddnet-rs, I want a full modernized system".
Looking at the screenshots of the design C restyle (PRs #37 to #48) critically:

| Finding | Why it is weaker |
| --- | --- |
| Scoreboard is a near-black rectangle with a solid orange header | A flat neutral slab does not belong to a world of pastel skies and round clouds. The saturated orange bar is the loudest thing on the screen and carries only the map name. |
| Square corners (radius 2) on HUD, kill feed and chat | The Tee, the clouds, the cursor and the menu tabs are all round. The HUD is the only angular thing. |
| No elevation: every panel sits on the same plane | Without a shadow nothing says "this is above the game". The only separation is a darker colour, so on a dark map the panel disappears and on a bright map it is a hole. |
| Neutral black and grey | The palette has no relation to the game's evening sky (blue-violet to amber). Black over amber looks dirty. |
| Empty ghost flags in every row | KoG servers send no country; the engine draws its "unknown" placeholder 64 times. 64 identical empty icons are noise. |
| Column headers in full text colour, same weight as the data | Hierarchy is flat: header, data, ping all shout. |
| Menu: grey and green upstream buttons and yellow-green column headers stay on a violet-free dark page | The restyle recoloured surfaces but left the controls' own colours, so the page is two systems. |
| Icons from three sources (font icons, sprite flags, the engine's emoticon ghost) at different weights | Inconsistent; see section 8 for what is left. |

## 2. What ddnet-rs does (inspiration only)

ddnet-rs (github.com/ddnet/ddnet-rs, Rust, egui based) is licensed Apache-2.0. We copy no code, no asset and no layout
data from it. What I looked at: the screenshots in its issues #144 (New UI, main layout) and #156 (New UI, design guidelines, Figma
mock-ups). Its README has no screenshots. Reference images are kept privately for the comparison page only and are not in this repository.

Observed, not guessed from code:

- **Colour.** The menu sits on the game's own sky, blurred, with a large pale mascot silhouette. Panels are translucent dark blue
  (left rail, right rail), the active item a darker blue. In the Figma mock-ups the panels are purple-grey, textured, with
  creamy-white text. Accent colours are yellow (names, links, "Join") and light blue (secondary text).
- **Shape.** Rounded panels (large radius), pill buttons, circular badges, a slanted map thumbnail that bleeds out of a row.
- **Depth.** Soft dark drop shadows on icons and on pill buttons; text with a dark outline; subtle vertical gradient on icons
  (white to pale yellow).
- **Layout.** A left navigation rail with big filled glyphs and one-word labels, a right rail of friends with avatars and status
  dots, a lot of empty space.
- **Not verified.** Animations and sound feedback: static images only, so I do not claim anything about them. The in-game HUD of
  ddnet-rs (scoreboard, chat, kill feed, vote, emote wheel): I found no screenshot in the sources above, so the HUD comparison is
  of the menus' language, not of a HUD I saw.

## 3. Current against ddnet-rs against target

| | Current (design C) | ddnet-rs | Target (game glass) |
| --- | --- | --- | --- |
| Surface | opaque near-black | translucent blue / purple-grey | translucent blue-violet, 86% so text keeps 7:1 over a white map |
| Corners | 2 (HUD), 6 to 10 | large, pills | 12 panels, 4 rows, pills for kill feed and toasts |
| Depth | none | soft shadows | 4 elevation levels with a soft shadow and a 1 unit rim light |
| Accent | solid orange header | yellow and light blue | the same orange, only as a small bar, the self row mark, the one primary button |
| Text | white, same weight | cream, dark outline | warm white, muted for headers and secondary |
| Background blur | none | blurred sky | none: see section 4 |
| Icons | mixed | one filled style | font icons kept, empty placeholders removed (section 8) |

## 4. Rendering: what is possible without a shader

The client's graphics API (`IGraphics`) has quads with per vertex colours (`SetColor4`), rounded rectangles with a corner mask
(`DrawRect`, `DrawRect4`), textures and sub-rectangles. It has no blur and no render-to-texture in the UI path. Options I weighed:

| Wanted | Way | Cost | Verdict |
| --- | --- | --- | --- |
| Rounded glass panel | `DrawRect4` with a lighter top colour, a 1 unit rim quad | 1 draw each | built (`KogDraw::Panel`) |
| Soft shadow | one generated 64x64 texture (a blurred rounded box, alpha only, made at start from a formula, no asset), drawn as a 9-slice: 9 quads in one batch, any blur by scaling the corner slices | 1 batch, 1 texture of 16 KB | built (`KogDraw::Shadow`), the 9-slice plan is a pure function with a property test |
| Layered quads as a shadow | 4 to 6 expanding rounded rects | 4 to 6 draws, banding | rejected |
| Background blur ("frosted glass") | needs a render target and a blur shader (two passes) in all backends (OpenGL, Vulkan, Metal via MoltenVK, GLES) | a new shader per backend, a full-screen copy per frame, a risk for every backend | **not feasible without engine shader work**, not done |
| Alternative to blur | translucency (86%) plus a vertical gradient plus the rim light; in the menu a lighter scrim so the map shows | none | built; the map is visible through the panel but not blurred |

Performance: the shadow texture is created once at start. Drawing never allocates. The 9 quads of one shadow are submitted with one
`QuadsDrawTL` per slice inside one `QuadsBegin`; batching all shadows of a frame into one call would need a retained list and is not done.
Numbers are in section 7.

## 5. The system

**Tokens** (`kog_style.h`, one place, no configuration read there so they are testable):

- Themes: `CLASSIC` (upstream, nothing applied), `DARK`, `LIGHT`, `GLASS` (new default, `cl_kog_theme 3`). `ThemeFromSetting` maps anything unknown to Classic.
- Palette per theme; `GLASS` is blue-violet (`#1b1838` at 86%, raised `#2a2650` at 90%, text `#faf6ec`, muted `#c4c2dc`, accent `#f06a30`).
- Radius: `Control` 6, `Card` 10, `Panel` 12, `Pill` (half the height).
- Elevation: `FLAT`, `SMALL` (kill feed, tooltips, chips), `RAISED` (scoreboard, menu views, buttons), `FLOATING` (toasts, popups): blur, y offset, shadow alpha, rim alpha.
- Motion: hover 0.10 s, panel 0.18 s, toast 0.20 s; `EaseOutCubic`, `Progress(elapsed, duration, reduceMotion)`.
- Icon sizes 12, 16, 24. Spacing and type scale unchanged (12 is the smallest text).

**Primitives** (`kog_draw.{h,cpp}`): `Shadow`, `Panel` (shadow, gradient fill, rim), `Pill`, `PlanShadow` (pure), `Lightened`.

**What components ask** (`kog_hud.h`, `kog_theme.h`): `KogHud::DrawPanel`, `KogTheme::DrawView`. In Classic they draw exactly what upstream drew.

**Widget styles.** Panel/card, button (secondary: glass card, primary: brand gradient with shadow), tab, list row, scrollbar, input,
toggle, tooltip (glass, small elevation), toast (glass, floating, slides in 16 units in 0.2 s), chip, kill feed pill. Buttons, inputs, boxes, rails,
tabs keep the per-theme recolouring from PR #37; the glass theme only changes the palette they read.

**Accessibility.** Contrast is a unit test per theme over the worst backing (white behind a dark panel, black behind a light one),
including the lighter top of a glass panel and the self row. Status colours differ in lightness. Reduce motion: `cl_kog_reduce_motion`
(Settings, Graphics) makes toasts and hover fades jump. UI scale and minimum text 12 are unchanged from the HUD work.

## 6. Where it applies

| Screen | State |
| --- | --- |
| HUD: scoreboard | glass panel, title with accent bar and hairline, muted headers, hairline rows instead of zebra, 1 unit separators between column groups, self row with a bar, palette ping colours (number always shown), no placeholder flags. Two variants were built (zebra rows, hairline rows with separators); the second is delivered, the first is in the comparison page |
| HUD: kill feed, chat, tooltips, toasts, vote bar, emote wheel, spectator menu | glass pills and panels; chat lines get 0.45 em padding and 0.18 em spacing between wrapped rows (upstream: 0.17 and none) |
| Menu views (browser, settings, demos, in-game pages) | glass view with a shadow (`KogTheme::DrawView`, 15 call sites) |
| Controls | one style set: `KogTheme::DrawControl` (gradient, rim) for buttons and edit boxes; `KogTheme::Control` keeps the meaning of an upstream colour (green good, red bad, yellow warn, blue other) and replaces the colour with the palette's; cached buttons (dropdowns, `DoButton_Menu` with properties) too; grid headers (sorted column in the brand colour); popups and dropdown lists as one glass panel with a shadow |
| Cards, list headers, rows | `KogTheme::MenuFill`: while the menus draw, upstream's translucent pure white becomes a theme layer in `CUIRect::Draw`; our own layers are white 0.996 so they are not dimmed twice |
| Browser text | search hits in the soft brand colour, game types muted, ping in the three status colours (`KogTheme::Highlight`, `GametypeText`, `PingText`) |
| Settings page list, top tab bar | a glass panel behind each, so the text keeps its contrast over the sky in every theme |
| Popups (connecting, messages), rank and score boxes, movement information | glass; boxes with white HUD text stay dark in the light theme |
| Motion | every widget that asks `CUi::ButtonColorMul` fades between default and hot in 0.1 s (`KogMotion::CHoverTable`, a fixed table, no allocation); reduce motion or no smooth scrolling jumps |
| Start page | scrim that lets the sky show, glass cards, brand gradient Play button |

## 7. Measurements

`scripts/kog/bench_hud.py --runs 2 --seconds 10 --scenes menu,hud`, macOS Apple silicon, Release, 1280x720, vsync off, frame time in microseconds, median / p95 / p99 (2 runs, noise 5 to 10 percent):

| scene | classic median / p95 / p99 (us) | dark median / p95 / p99 (us) | light median / p95 / p99 (us) | glass median / p95 / p99 (us) |
| --- | --- | --- | --- | --- |
| start page | 389 / 2036 / 2624 | 384 / 1721 / 2638 | 435 / 2781 / 4183 | 521 / 3689 / 5496 |
| scoreboard 16 players | 920 / 3855 / 8643 | 851 / 2869 / 3782 | 829 / 3002 / 3897 | 958 / 3508 / 4660 |
| scoreboard 64 players | 2181 / 5622 / 9630 | 1749 / 3740 / 4737 | 1662 / 3326 / 4120 | 1653 / 3424 / 4184 |
| scoreboard 128 players | 3110 / 5150 / 5801 | 1734 / 3434 / 4816 | 1684 / 3436 / 4835 | 1702 / 3444 / 4850 |
| chat and kill feed (64 players) | 1652 / 3947 / 5691 | 1894 / 7110 / 18065 | 1707 / 3976 / 5540 | 1833 / 4638 / 8168 |

Reading: the glass HUD costs the same as the dark and light themes within noise (and less than Classic at 64 and 128 players, because the KoG layout shows one fixed row height).
A first run with 2 runs per cell showed the start page 130 us slower in glass. That was noise: `cl_menu_map "auto"` picks a random map per run. With 4 runs per cell the start page is 368 / 370 / 369 / 371 us median (classic / dark / light / glass), so no per-frame cache of the panel quads was needed.
Allocations per frame (`scripts/kog/dev/allocs_per_frame.py`, includes the engine): scoreboard with 64 players 6321 classic against 5674 glass; start page 1178 classic against 1170 glass. No new allocation per frame comes from our code.

Sanitizers: a RelWithDebInfo client with `-fsanitize=address,undefined` ran 20 scenes (HUD with 128 and 24 players, start page, browser, settings controls, each in 4 themes) with no ASan or UBSan report. LeakSanitizer is not supported on macOS arm64 and was off.


## 8. What is not done, and not verified

Seen on screen on a live public DDNet server (joined read-only as a normal player, dev hooks local): connecting dialog, in-game HUD,
scoreboard (real, 14 players), Esc menu, chat input, emote wheel, rank boxes, kill feed, in glass, and the light and dark themes for most of them.

- **Not seen:** the vote bar (calling a vote would affect other players), the spectator menu (needs spectator mode; the flag hook only set its state and nothing drew), the player list page and the Server info page of the Esc menu, popups other than "connecting", the loading screen after the map download.
- **Esc menu and tab bar:** the tabs use upstream's large text and the Esc menu does not fade or slide in.
- **Spectators panel** under the scoreboard is upstream's, and tall even when empty.
- **Keyboard:** only text fields (a ring while editing) and the start page have a focus ring. Upstream's menus have no keyboard navigation of buttons, sliders and tabs, and building that is a larger piece of work than a style; it is not done.
- **Ghost placeholders** in the communities and countries lists ("None", "unknown country") are working filter entries, not decoration; they are unchanged.
- Cached widgets (dropdowns) now rebuild when the theme changes, because the cache remembers the theme it was built for.
- Hover easing covers every widget that asks `CUi::ButtonColorMul` (buttons, edit boxes, boxes, list rows); cached dropdowns and the popup buttons use their cached states and do not ease.
- Icons: no new icon set and no atlas script (the shipped Font Awesome 6 Free is the one set; no sprite was missing).
- Sound feedback: none.
- Contrast is tested for the palettes; it is not tested per widget.
- Windows and Linux builds are verified by CI only; built, tested and run on macOS arm64.

## 9. Upstream merge points

New code: `kog_draw.{h,cpp}`, additions to `kog_style.h`, `kog_theme.h`, `kog_hud.h`. Edits in upstream-shared files, each a one-line call:
`ui.cpp` (`KogDraw::Init`), `scoreboard.cpp` (panels, title, headline colour, self mark, flag skip), `infomessages.cpp` (pill), `tooltips.cpp`,
`menus*.cpp` (15 `Draw` to `KogTheme::DrawView` plus includes), `menus_settings_graphics.cpp` (theme button, reduce motion), `config_variables_kog.h`.

## 10. The start page after ddnet-rs, and the QA checklist

### ddnet-rs start screen (observed in its issue screenshots, 1928x1024)

Left rail 244 px wide (12.7 percent), full height, translucent dark blue over the sky: the logo block on top (about 110 px), five
navigation rows of about 58 px (icon at 45 px, label at 90 px, the current row darker), a divider, free space, a divider, then at the
bottom the account row (round avatar, a small blue "Logged as" caption over the name in yellow, a log-in icon on the right) and the Settings row. A slim right rail
(76 px) with friend avatars on top and three entries at its bottom (online count, new messages, newsletter), each an icon with a tiny caption,
that opens into a 318 px "Friends" panel. The middle is empty: the blurred sky and a large pale mascot.

### What the KoG start page does with it (`kog_menu_start.cpp`, `KogStyle::Start::PlanStart`)

| ddnet-rs | KoG |
| --- | --- |
| left rail with logo, navigation | glass rail 160 to 232 units wide, logo, Demos, Editor, Run server |
| account lower left | tee in a socket, "Logged in as" or "Not logged in", the name or "Log in with KoG", a status mark; a click logs in, cancels, or opens Log out and Devices on kog.tw |
| Settings at the bottom of the rail | Settings and Quit at the bottom of the rail |
| empty middle | the logo and the one primary Play button |
| right rail with online, messages, newsletter | right rail with News, Website, Tutorial |
| (version) | version, console and update status where the footer draws them |

Not in KoG: a friends panel (the client has the friends tab in the browser), a Discord link (no KoG Discord address is known to the client).

### QA checklist (macOS screenshots, glass theme unless named)

| Screen | Seen | Finding | Fixed |
| --- | --- | --- | --- |
| Start page 16:9, 4:3, 640x480, dark, light, glass | yes | the first rail was inset by the menu's margin; hero off centre; caption lines touching | rails run to the window edge, hero centred, 4 unit gap |
| Server browser | yes | grey and green buttons, yellow-green headers, blue and purple text | palette colours (PR #60, #61) |
| Favourites, demos, news pages | yes | glass views, nothing wrong seen | |
| Settings controls and graphics | yes | light grey cards and key fields, dark pills on the sky in the light theme | theme layers, rail panels |
| Esc menu | yes | large tab text | open |
| Connecting dialog | yes | black slab and grey button | glass popup |
| Quit popup | captured (`qa-quit`), not looked at closely | | |
| HUD: scoreboard, chat, kill feed, rank boxes, emote wheel | yes, live | light grey rank slabs, cramped wrapped chat, ghost flags | glass boxes, spacing, flags skipped |
| Spectators panel | yes, live | tall and empty | one line when empty |
| Chat input | yes, live | bare text | glass box |
| Toasts | no (needs a server notice) | | |
| Editor frame | not looked at | | |
| Vote bar, spectator menu, loading screen | not seen | | |
| Long names and translations | partly: ellipsis everywhere new code draws text, tested by `check_hud_label_lengths.py` for the HUD | | |
| Fractional scales | not checked | | |

Performance: the start page with the rails, measured like before, is in the bench table of section 7.
