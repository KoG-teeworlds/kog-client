# Client and server: what exists, what the client uses, what is missing

Written 2026-11-27 from the code of this repository (`main` and the open pull requests named below) and from the server
documents in `KoG-teeworlds/kog-2.0-server` (`docs/protocol/*`, `docs/MAP-QUESTS.md`, `docs/TOURNAMENTS.md`, branch
`feat/event-signing`). Labels: **[R]** read in code or a document, **[N]** not verified by running it. I did not build or run
the server (not allowed), so nothing here was tested against a live KoG server.

## 1. The finding that shapes the list

The server's own protocol index says it: KoG adds almost nothing of its own to the game connection. What exists on the wire
today **[R]** (`docs/protocol/README.md`, `protocol_ex_msgs.h` on server `main`):

| Message | Direction | State | Client |
| --- | --- | --- | --- |
| `kog-client-hello` / `kog-server-hello` (capability ids; only `client_login` = 1) | both | built | built (`docs/KOG-HELLO.md`) |
| `kog-login-challenge`, `kog-login-proof`, `kog-login-result` | both | built | built (`docs/LOGIN.md`) |
| server certificate in the login challenge | server to client | server built, client open as PR #29 | open |

There is **no** `PLAYER_NOTICES` and no `QUESTS` capability on the server, no notice or achievement message, no
tournament message, no drain message. The capability table of the server (`KOG_CAPABILITIES`) has one row. A client feature
that needs a new message therefore needs a server change first, and this series does not invent wire formats for the server.

Everything else a KoG server shows a player goes through standard DDNet parts, which every client already renders **[R]**:

| Server feature | How it reaches the client | Client today |
| --- | --- | --- |
| Tournament round clock | `NETOBJTYPE_GAMEINFO` time limit and round start tick (`tournament-display.md`) | the stock clock; nothing to add |
| Tournament and role texts, "GO!", announcements | broadcast and chat | stock |
| Roles, costumes, disguises (`troll_look`), rainbow | the normal player info item, rewritten for each viewer (`player-look.md`) | shown as any look. Staff see through disguises on the server side, so there is nothing to do in the client |
| Drain for updates | server name gets ` [UPDATING]`, the server leaves the lists, a refusal text on connect (`server-drain.md`) | the name and the refusal text are shown as they are |
| Map barriers | laser outline, a changed map file through the normal map change (`map-barriers.md`) | stock |
| Race time, checkpoints, ranks | standard race messages and chat | stock DDNet HUD |
| Map quests | lines in the map's server settings, `kog_quest ...`, plus a chat line `Quest completed: <name>` (`MAP-QUESTS.md`) | **built here** (section 3) |
| Save and load of teams | chat | stock |

## 2. Candidates, checked one by one

| Candidate from the request | Verdict | Why |
| --- | --- | --- |
| In-game notices and achievement popups (`PLAYER_NOTICES`) | **Blocked on the server** | No message and no capability exist. The achievements document is not in the server's `docs/` on `main`. Design the message in `docs/protocol/` first (capability id 2 is the next free one), then build both sides |
| Map quests: list and progress | **List built**, progress blocked | The quest lines are in the map file, the client can read them without any network message. Progress is not sent; the server only writes the completion as a chat line |
| Tournament and 1v1 status display | Nothing to do | The clock and texts are stock. A richer panel (bracket, next opponent) needs data the server does not send |
| Maintenance and drain countdown | Nothing to do | No countdown is sent; only the name suffix and the refusal text. A banner needs a server message with the planned time |
| Moderation visibility (disguises, trolls) | Nothing to do | Done on the server per viewer: every client with an auth level sees through a disguise |
| Login status and account name | **Built before** (PR #8, #28) | The start page now shows the state (this series, section 4 of `CLIENT-STYLE.md`) |
| Certified server badge in the browser | Open | Needs PR #29 (certificate check) and a flag in the server list; PR #30 reads the signed master list. Build after both merge |
| Checkpoint and time display from new score messages | Nothing to do | There are no new score messages; the stock race HUD shows them |
| Save and load feedback | Nothing to do | Chat lines |

## 3. Built in this series

**Quests of the map on the server info page.** `src/engine/shared/kog_quests.h` parses the `kog_quest` lines exactly as
the server document specifies (id, scope, name, description, one to eight conditions, 32 quests, byte limits, UTF-8, the
known stats and weapon ids) and refuses what the server refuses, so the list never shows a quest that cannot be completed.
`CGameClient::LoadMapSettings` feeds each settings line to the list before the console runs it (the console still ignores
the unknown command, as before). The in-game "Server info" page shows a Quests card next to the message of the day, with
name, description and the words Team or Solo (never only a colour). 7 unit tests (`src/test/kog_quests_test.cpp`).

Behaviour on other servers: a map without `kog_quest` lines gives an empty list and the page is exactly the old page. No
capability is needed because nothing is sent. Not verified by running it on a real quest map **[N]**; the parser is tested
against the examples of the server document.

## 4. Missing, in the order I would build it

1. **Server: a notices message and capability** (`PLAYER_NOTICES`, id 2): one line of text, a kind (info, warning,
   achievement, maintenance), an optional "ends at" tick. It would carry achievements, quest completions with the stats
   and a maintenance countdown with one mechanism. Client: a toast component in the new style (`CLIENT-STYLE.md`, notices).
2. **Client: certified badge** after #29 and #30 are merged.
3. **Server: quest progress** (counters per player) if a progress bar is wanted; today only the completion is known.
4. **Client: recent servers and the Continue card** of `docs/MAIN-MENU.md` (local data only).
5. **Client: the rail** and the in-game frame of the same design, once the settings pages are checked at 800x600.

## 5. Rules kept

Capability negotiation only (no version checks); fail closed on certificate problems (unchanged, PR #29); a KoG client
behaves as a stock client on a vanilla server (the quest list is empty there, the theme is local).
