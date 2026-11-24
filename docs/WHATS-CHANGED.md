# What changed: showing a returning player the changes

Status: design, not built. Client side only. Nothing here sends data anywhere.

## Problem

A player who updates after some months gets new settings, other defaults, renamed or removed options and changed behaviour, and
sees none of it. The client should show these once, let the player decide each one, and offer two shortcuts: "Set to sane
default" and "Ignore all changes".

## Rules that cannot be bent

1. A privacy-relevant feature (it sends data, contacts a server, or stores an identity) is never switched on by default, by
   skipping, by closing the page, by "Set to sane default" or by "Ignore all changes". Only an explicit choice on that entry turns it on.
2. No page over gameplay. It appears in the menu only, never while connected to a server or in a demo, editor or race.
3. Works offline. The content is compiled into the client and covered by the signed update. No fetch, no translation service.
4. Nothing is sent by this feature, not even that a page was shown.
5. A player's own value is never replaced by a new default. At most the page lists it as "you set this".
6. A headless or dedicated start never shows or waits for the page.
7. The feature has a kill switch (see below).

## 1. Data model: one table in code

One table, `src/game/client/kog_changes.cpp`, ordered by id. Each entry:

| Field | Meaning |
| --- | --- |
| `id` | Whole number, ascending, never reused. The newest id is the client's "revision". |
| `kind` | `NewSetting`, `ChangedDefault`, `Renamed`, `Removed`, `Behaviour`, or `NoDecision` (see coverage test; never shown) |
| `title`, `text` | Plain language, in the localisation system (`Localizable()`), short enough for a small window |
| `settings` | The config variable names touched (empty for a pure behaviour change) |
| `choice` | `Toggle`, `Choice` (list of labelled values), `Number`, `Text`, or `InfoOnly` |
| `recommended` | The value "Set to sane default" applies. Ignored for `InfoOnly` |
| `old_default` | For `ChangedDefault`: the value before, used for pinning (section 2) |
| `superseded_by` | Id of a later entry that replaces this one. Superseded entries are hidden when the later one is shown |
| `reason` | Only for `NoDecision`: one line, why no decision is needed |

Derived, not typed a second time:

- the current value, the new default, minimum, maximum and type come from the config variable itself;
- `privacy_relevant` is derived from a new flag `CFGFLAG_PRIVACY` on the config declaration (for example on `kog_hello`,
  `kog_telemetry`, `cl_kog_auto_login`). The flag lives in one place, next to the variable. A test fails when a privacy entry
  has `recommended` other than off, and when a variable with the flag has no entry or `NoDecision` marker;
- the page in the docs is generated from the table by a script (`scripts/kog/gen_changes_doc.py`, run by the build, checked in
  CI by comparing output), not written by hand.

### Coverage test (fewer values to maintain)

A generated baseline file lists every config variable of the last release as `name type default min max flags`. The release
workflow rewrites it as part of cutting a release; nobody edits it. The test `kog_changes_test` compares the current
variables with the baseline and fails when a variable was added, removed, renamed (a removed plus an added one), or had its
default, range or type changed, unless the table has an entry with an id above the baseline's revision that names it in
`settings` (any kind, including `NoDecision` with a reason). It also checks: ids ascend and are unique; every
`superseded_by` points to a later id; every named setting exists or the entry is `Removed`/`Renamed`; a `recommended` value lies
in the variable's range; no cycles in `superseded_by`; every title and text exists in the source language file.

## 2. State

- One saved number, `kog_changes_seen` (default 0, in `settings_tclient.cfg` next to the other `kog_` variables). It is the
  highest id the player has dealt with.
- Fresh install: no `settings_ddnet.cfg` and no `settings_tclient.cfg` exist. The client sets the number to the newest id and
  saves it. No page.
- Existing player, number missing (an older client, or only `settings_ddnet.cfg` copied by hand): treated as 0, so all entries
  that still matter appear. The table starts at the first KoG release, so this is a short list.
- Revision higher than the client knows (downgrade, or a config copied from a newer client): the number is left alone, no page,
  and a log line says so. Entries above the client's newest id are not invented.
- Config missing, unreadable or corrupt: the client already falls back to defaults. That is a fresh start only when both files
  are absent; a file that exists but fails to parse counts as an existing player with number 0, so the page can still be
  shown, and the page's "Set to sane default" works on top of the defaults. The page never writes over a file it could not read:
  the existing save logic keeps the old file until a write succeeds.
- Because only values that differ from the default are saved, "the player set this" means: the variable has a line in the file
  (or was changed this session), which is the same as differing from the default of the version that wrote it.

### A changed default must never override a player's own value

The save rule hides one case: a player who deliberately chose a value equal to the old default has no line in the file, so a
new default would silently move them. To close that:

- A release that changes a default carries the entry with `old_default`. On the first start after the update (seen number below
  the entry's id) and before any page logic, for every `ChangedDefault` entry whose variable has no line in the file, the client
  sets the variable to `old_default`, which now differs from the new default and is therefore saved. Behaviour stays as it was.
- The page then offers the new value as the recommendation. "Set to sane default" resets the variable to the new default.
  "Ignore all changes" keeps the pinned old value.
- A player with a line in the file is not pinned (they chose something already) and sees the entry as "you set this, kept".
- Fresh installs are not pinned: they get the new default, as nobody had the old one.
- Pinning is a local, silent copy of a value. It runs even with the kill switch off, because skipping it would change behaviour.

## 3. Flow

When it appears: on the first start where `kog_changes_seen` is below the newest id and at least one entry survives collapsing
(below). It is a page in the main menu (a banner "N changes since your last version, Review" on the start page, and the page
itself). It never opens by itself over a running game, a connecting state, the editor or a demo; if the client starts directly
into a server by command line, the banner waits until the player is back in the menu. A headless or dedicated start skips all of
it and does not touch the number.

Collapsing: the entries to show are those with id above the seen number, minus every entry whose `superseded_by` chain ends in
an entry with an id the player has already seen or that is itself shown instead. A player returning after six months sees only
the end state. Example: entry 4 turns a feature on by default, entry 9 removes the feature: 4 is superseded by 9; a player who
never saw 4 sees only "removed"; if 9 only needs an info line, that is all.
Entries whose settings the player set to a non-default value and whose kind is `ChangedDefault` are shown as info ("kept your value").
`NoDecision` entries never show.

Per entry, by choice type: toggle, choice list, number, text, or only a note with OK. The current value is shown first, then the
recommended value with a short reason. Controls: "Keep current", "Use recommended" and the specific control. Nothing is
applied until the player leaves the entry's control or presses Apply; "Back" discards the unapplied changes of this visit.

Global actions at the top:

- **Review each** (default focus): go through the entries one by one or as a list, then "Done" marks everything as seen.
- **Set to sane default**: applies `recommended` for every entry that is not privacy-relevant and not info only. Privacy-relevant
  entries stay off and are listed afterwards as "still off, turn on in Settings". Asks for confirmation with a count.
- **Ignore all changes**: keeps all current values (including pinned old defaults), new features stay off, marks all as seen.
- **Dismiss for now**: closes the page and does not advance the seen number; the banner returns next start.

After Done, Set to sane default or Ignore all changes the number is set to the newest id and saved. The page stays reachable at
Settings, "What changed", which shows the same list again for the current table (entries already decided show the current value,
so it works as a review of the privacy-relevant switches too).

## 4. Hard rules in detail

- Kill switch `cl_kog_changes_page` (default 1, in every environment: it is a display feature that changes nothing by itself).
  OFF means: no banner and no automatic page; the seen number does not advance (so turning it on later still shows what was
  missed); the Settings page "What changed" stays reachable; pinning still runs; nothing else changes.
- Compiled in: the table is part of the binary, hence of the signed package. A manifest or file from a server or channel
  cannot add, edit or remove an entry. The table can contain only values for existing variables, never commands.
- Applying a choice goes through the same config setter as the settings page (range check, read-only handling), so a bad table
  value cannot write outside a variable's range; the test checks that.
- Accessibility: every control reachable by keyboard (Tab, Enter, Space, arrows) and by a controller (the existing menu focus
  and gamepad navigation), a visible focus marker, no meaning by colour alone ("Recommended" is text), text scales with the
  UI scale, the list scrolls and the buttons stay visible at the smallest supported window size.
- Localisation: titles and texts go through the existing `Localize` / `Localizable` mechanism and `data/languages`, with the
  source string as fallback. Entries are written in English; a missing translation shows English. Placeholder format
  specifiers must match (the client already ignores translations that differ).
- Offline: nothing in this feature uses the network.

## 5. UI sketch

```
+--------------------------------------------------------------+
| What changed since your last version (1.1.0 -> 1.4.0)        |
| 5 changes need a look. Nothing is sent anywhere.             |
|                                                              |
| [ Review each ]  [ Set to sane default ]  [ Ignore all ]     |
|                                                              |
|  1. Announce capabilities to servers          Privacy  Off   |
|  2. Update check                              Toggle   On    |
|  3. Update channel                            Choice         |
|  4. UI scale step                             Number   100   |
|  5. Device name                               Text           |
|  6. Chat translation removed                  Info           |
|                                                              |
|                                  [ Dismiss for now ]         |
+--------------------------------------------------------------+
```

Toggle (privacy-relevant, so no recommended-on):

```
 Announce capabilities to servers                      [Privacy]
 Tells a KoG server which features this client has.
 Sends: a list of feature names. Does not send: name, device, account.
 Now: Off        Recommended: Off
 ( ) On    (x) Off                      [ Keep ]
```

Choice:

```
 Update channel
 Which releases this client offers you.
 Now: canary     Recommended: stable
 ( ) stable   (x) canary   ( ) dev        [ Use recommended ] [ Keep ]
```

Number:

```
 Scroll speed
 The default changed from 3 to 5. You set 4, so it stays.
 Now: 4          Recommended: 5
 [ - ] 4 [ + ]                              [ Use recommended ] [ Keep ]
```

Text:

```
 Device name
 Shown on your account page.
 Now: (empty)    Recommended: (empty)
 [ KoG Client (Linux)              ]        [ Keep ]
```

Info only:

```
 Chat translation was removed
 It sent chat text to a third-party service. Nothing to do.
                                            [ OK ]
```

## 6. Edge cases

- Several versions skipped: collapsing as above; one page, not one per version.
- Entry whose setting was removed later: if a later `Removed` entry supersedes it, only the removal shows (as info). A
  `Removed` entry never offers a control. Saved lines of unknown variables are kept by the existing unknown-command handling.
- Two entries touching one setting: the later one supersedes the earlier one unless both are needed; if the test finds two
  non-superseded entries on the same setting it fails and asks for `superseded_by` or an explicit merge.
- Settings changed by a command line flag or `autoexec`: values set that way this session are shown as "set by command line,
  not saved" and are not changed by the page; saved config is not touched by them. A value from `autoexec` counts as the player's own.
- Channel switch (canary to stable): the table is part of each build; the seen number is a single number. Moving to an older
  build leaves a higher number (downgrade rule, no page). Moving to a newer one shows only what is above the number. A canary
  entry that never reached stable is simply absent from the stable table, so the numbers must not be reused by a different
  entry: ids come from `main`, and a stable release cuts from `main`.
- Reset to defaults (the `reset` command or a settings reset): does not change the seen number. Only deleting the config files
  makes a fresh install.
- Multiple profiles, config directories or a portable install: the number lives in the config directory of that profile, so
  each is handled on its own. The TClient profiles file is not touched.
- Settings changed while the page is open (console): the page re-reads values when it draws.
- A save failure: the seen number is not advanced in memory more than it is on disk; the page shows again next start rather than lose a decision.

## 7. Tests

Unit tests (`src/test/kog_changes_test.cpp`):

- table checks and the coverage test from section 1;
- collapsing: skipped versions, chains, an entry superseded by one already seen;
- state: fresh install, missing number, higher number, corrupt file, kill switch off;
- pinning: a changed default with no line in the file is pinned; with a line it is not; a fresh install is not;
- "Set to sane default": no privacy-relevant variable changes, an out-of-range recommended value is rejected;
- "Ignore all changes": no value changes, number advanced;
- localisation: all strings exist; format specifiers match.

Not testable here: how it looks and feels (layout at small sizes, controller navigation, focus handling), which are checked by
hand on one build per platform; real upgrades across months (simulated by writing config files of earlier shapes); the
interaction with the updater restart (checked by hand once).

## 8. Relation to existing settings

- `kog_hello` (default 0) and `kog_telemetry` (default 0) are the first real consumers: both privacy-relevant, so the page
  offers them as toggles that never come on by default or by "Set to sane default". `kog_telemetry` is opt-in only.
- `kog_check_updates` (default 1) is a network request that sends nothing but the signed-manifest fetch (see `docs/PRIVACY.md`);
  it is listed as a toggle. Whether it counts as privacy-relevant is an open question below.
- `cl_kog_auto_login` and the planned key passphrase (stores or unlocks an identity): privacy-relevant. When the passphrase
  arrives, its entry is a `Choice` ("ask each start" / "keep unlocked this session"), recommended is the safer one, and it is
  never switched on by the page.
- `docs/PRIVACY.md` keeps its table; a later step can generate the "Default" column from the same flag.

## 9. Rollout and open questions

Steps:

1. After the open client pull requests (capability hello, key file and passphrase, user agent) merge: add the table, the flag
   `CFGFLAG_PRIVACY`, the coverage test and the generated baseline, with entries only for what those pull requests added.
   No UI yet. This alone stops anyone from adding a setting without a decision.
2. Minimal slice of UI: the Settings page "What changed" (a list with Keep and Use recommended per entry, global Ignore all), the
   seen number and fresh-install handling, kill switch.
3. The start-page banner, collapsing, pinning of changed defaults, "Set to sane default".
4. Translations, controller polish, docs generation, and the release workflow rewriting the baseline.

Open questions for the owner, each with a recommendation:

1. Is `kog_check_updates` privacy-relevant? Recommendation: yes for the page, because it contacts a server; recommended value
   stays "on" only because it is already the default and is shown, not changed, by "Set to sane default".
2. Show the page automatically, or only the banner? Recommendation: banner on the start page, never a modal.
3. Pin old defaults for everyone, or only when the old value is a deliberate-looking choice? Recommendation: pin all, since
   it cannot be told apart.
4. One flag on config declarations (`CFGFLAG_PRIVACY`) versus a field in the table? Recommendation: the flag, one place.
5. Keep the kill switch default 1 in every environment? Recommendation: yes; the feature is read-only until the player acts.
6. Should "Dismiss for now" expire after a number of starts? Recommendation: no, the banner is small and stays.
7. Show entries for a switched-off feature the player cannot use (for example login on stable)? Recommendation: hide them
   when the build does not contain the feature.
