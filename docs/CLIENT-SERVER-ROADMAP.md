# Client and server: what exists, what the client uses, what is missing

Written 2026-11-27 from the code of this repository (`main` and the open pull requests named below) and from the server
documents in `KoG-teeworlds/kog-2.0-server` (`docs/protocol/*`, `docs/MAP-QUESTS.md`, `docs/TOURNAMENTS.md`, branch
`feat/event-signing`). Labels: **[R]** read in code or a document, **[N]** not verified by running it. I did not build or run
the server (not allowed), so nothing here was tested against a live KoG server.

## 0. The rule: stock clients first

Owner rule (2026-11-27): every feature must work for Teeworlds (0.6 and 0.7) and DDNet clients. So for each feature the
**vanilla path** comes first: what a stock client sees through a chat line, a broadcast, a snapshot item or a console
command, and that path is complete on its own. A KoG client message is only an addition: it is sent only to a client that
announced the capability in the hello and that the server agreed to, and it never replaces the information on the vanilla
path in a way that loses it (the server sends one or the other, never neither). 0.7 clients get chat and broadcast through
the same server functions as 0.6 (the server translates), so "chat line" below covers both.

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

| Candidate | Vanilla path (Teeworlds 0.6/0.7, DDNet) | KoG client addition | Verdict |
| --- | --- | --- | --- |
| Achievement notices (`PLAYER_NOTICES`) | A server chat line to that player, built (backend `ChatMessage`) | A toast on the same line today; a structured message later (section 4b) | Vanilla path built; the message is a proposal |
| Quest completion | The chat line `Quest completed: <name>` | The same toast | Built |
| Quest list | Lines in the map's server settings that every client ignores; nothing to see | The quest card on the Server info page, read from the map file | Built here |
| Quest progress | None today. Vanilla path to add on the server: a chat line at completion only (already), optionally a broadcast of "x of y" at most once a minute | A progress notice (capability `quest_progress`) | Needs server work; vanilla part first |
| Tournament and 1v1 status | The round clock (game info item), broadcast and chat texts, `/tournament` | Nothing needed | Nothing to do |
| Maintenance and drain countdown | Today: the name suffix ` [UPDATING]`, the refusal text, the start warning. Vanilla path to add: one chat line and one broadcast "Server restarts in 5 minutes", repeated at 5, 1 minute and 10 seconds | A banner with a local countdown from one notice | Needs server work; vanilla part first |
| Moderation visibility | The per-viewer player info item (disguises) | Nothing needed | Nothing to do |
| Login status and account name | `/login` chat command, chat answer | Device key login (built), the state chip on the start page | Built before |
| Certified server badge | None (stock clients ignore it) | Badge after PR 29 and 30 | Open |
| Checkpoint, rank and time display | Stock race messages and HUD | Nothing needed | Nothing to do |
| Save and load feedback | Chat lines | Nothing needed | Nothing to do |

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

## 4a. What the backend sends today (read, for the notice design)

**[R]** `temp_backend/docs/MAINTENANCE.md`, `temporary_backend/notices.py`, `kog-www/docs/ACHIEVEMENTS.md` section 10:

- The website writes `{"id","name","text","expires_at"}` on the durable queue `backend:player-notices`, only while a backend
  reports `BACKEND_CAPABILITY_PLAYER_NOTICES` (= 12 in `capabilities.proto`). `text` is one chat line (at most 200 bytes
  from the website, 250 in the backend; control characters become spaces).
- The backend delivers it as the existing `server.actions.ChatMessage` (`EventResponse.server_chat_message`) to the session of
  the name, or keeps it (5 per name, 7 days) and delivers on the next join. The same `id` is shown once.
- The game server turns that into `SendChatTarget(client, text)`: a server chat line (`NETMSGTYPE_SV_CHAT`, client id -1)
  that every client, vanilla included, shows. **Nothing on the wire says it is a notice**; only the text does
  ("Achievement unlocked: Finisher, silver (+150 coins)"). Quest completions are the same kind of line ("Quest completed: <name>").
- Maintenance: `MaintenanceOrder` (EventResponse field 250, `SERVER_CAPABILITY_MAINTENANCE = 13`, `BACKEND_CAPABILITY_MAINTENANCE
  = 19`) is defined in the protocol but "game server and edge act on it: not built". A draining game server today only changes its
  name and refuses new players; there is no countdown.

So the client can recognise two lines today and nothing else (section 4c). Everything below is a **proposal for the owner**.

## 4b. Proposal: one notice message, negotiated through the KoG hello

Goal: achievements, quest completion and progress, and a maintenance countdown reach a KoG client as structured data, with
no change for anyone else. Nothing of this is implemented; the numbers are proposals and need to be taken in the registries
(`docs/PROTOCOL-NUMBERS.md`) when it is built.

**Game connection (server and client).** One extended message, vital, server to client, `kog-notice@kog.tw`
(UUID computed with the formula of `docs/protocol/README.md`, like the hello):

| Field | Type | Meaning |
| --- | --- | --- |
| kind | int 1..8 | 1 info, 2 achievement, 3 quest completed, 4 quest progress, 5 maintenance, 6 warning. Unknown kinds are shown as info |
| key | string, at most 40 bytes | the notice id or the quest id; the client shows the same key once per connection, and a progress notice with the same key replaces the previous one |
| text | string, at most 250 bytes UTF-8 | exactly the line that would have been sent as chat; the client never needs to understand anything else |
| value | int | quest progress: counter now; maintenance: seconds left. 0 when not used |
| limit | int | quest progress: the target; 0 when not used |

Capability ids in the number space of the hello (`client_login` is 1): **2 `notices`** (kinds 1 to 3, 5, 6) and **3
`quest_progress`** (kind 4; separate so it can be switched off alone, because it can be frequent). The server offers each
with its own setting like `sv_kog_cap_client_login`; the client offers each when it can draw it.

**Vanilla path first, one path, never two.** The primary path is the chat line (and, for maintenance, a broadcast): it is what
a Teeworlds 0.6, 0.7 or DDNet client gets and it carries the complete text. The KoG message is an enhancement only for clients
that negotiated it. The server has one function `SendNotice(client, kind, key, text, value, limit)`:
if the client negotiated `notices`, it sends the message and **no chat line** (the KoG client writes the same text into its chat
log, so history is the same); otherwise it sends the chat line exactly as today (and the broadcast for maintenance). So a vanilla client, a KoG client without the capability, and a KoG client talking to an old server all behave as now,
and a capable client never sees the line twice. The client also writes every notice it shows to the chat log, so nothing is lost.

**Limits on the client.** Read only if negotiated (as the login messages); text cut at 250 bytes and control characters
replaced; at most 4 toasts in 10 seconds (the rest go to the chat log only); a toast lasts 5 seconds, a maintenance banner until
its countdown ends; nothing is clickable and no notice can open a link or run a command.

**Backend (`temp_backend`, protocol repository).**
- Protocol: `server.actions.PlayerNotice { BaseInfo target; int32 kind; string key; string text; int32 value; int32 limit; }` as a
  new `EventResponse` field (number taken from the numbers table when built) and a **server** capability
  `SERVER_CAPABILITY_PLAYER_NOTICE` (the game server announces it in its hello, like the other server capabilities, number free
  from 9 on; 13 is taken by maintenance in the proposed table).
- `notices.py` today builds `chat(...)`. With the change it checks the capability of the target's game server: if present it
  builds `PlayerNotice` (kind from an optional `kind` field of the queue JSON, default 1; the website puts `kind: 2` on
  achievements), otherwise the `ChatMessage` as now. The queue format only gains optional fields; the old backend ignores them,
  `BACKEND_CAPABILITY_PLAYER_NOTICES` (12) stays the gate for the website. The keep-for-next-join logic is unchanged.
- Maintenance: when the backend sends a `MaintenanceOrder` with a deadline, the game server turns it into one `PlayerNotice`
  kind 5 to every connected capable client (`value` = seconds to the deadline, `text` "Server restarts in 5 minutes") and again
  when the deadline moves or is called off (`value` 0 and a text that says so); the client counts down locally, so the server
  does not repeat it every second. Clients without the capability get the same text once as chat.

**Website (`kog-www`).** `PlayerNotices::send` gains an optional `kind` and the achievement call passes 2. No other change; the
page and API stay as they are.

**Game server (`kog-2.0-server`).**
- `protocol_ex_msgs.h`: register the message; `kog_protocol.h` table: two rows; `KogOffered()` already filters by settings.
- `SendNotice` as above; `edgeservice.cpp` `kServerPlayerNotice` and the old `kServerChatMessage` both call it.
- Quests: `CQuests` already holds the counters; with `quest_progress` negotiated the server sends kind 4 when a counter that a
  quest uses changes, at most once per second per player and quest, and kind 3 on completion (today a chat line).
- Tests: the pack and unpack of the message with every bound; a client without the capability gets the chat line and no message;
  a client with it gets the message and no line; unknown ids and an unknown kind.

**Client (`kog-client`).** `kog_protocol.h` table: two rows, offered when the toast component exists; `CKogNotices` reads the
message, draws the toast with the tokens of `CLIENT-STYLE.md` (kind colour from `Ok`, `Warn`, `Err`, `Brand`, always with a word
or icon), and keeps the same component for the chat lines of section 4c.

**Order.** (1) protocol repository: message and capability; (2) server; (3) backend; (4) website field; (5) client. Each step is
safe alone because nothing is used until both ends of a connection have it.

**Open questions for the owner.** Should a capable client also keep the chat line (a duplicate in the chat is an option, the
design avoids it)? Should achievements have a sound? Is 5 seconds the right toast time? Who may send kind 6 (a warning): only the
game server's own code, never the backend queue? (Recommended: never the queue.)

## 4c. Built on what is on the wire today

`CKogNotices` shows a toast for a server chat line (client id -1) that starts with `Achievement unlocked: ` or `Quest completed: `,
only on a server listed for the KoG community (the same test as the hello), and the line stays in the chat. A server that is
not a KoG server never gets a toast; a stock server sends no such lines. The text of a line is displayed, never interpreted. When
the proposal above is built, the same component takes the message instead of the line.

## 4d. Tests for the fallback

Where the repository has test infrastructure the fallback is tested:

- Client (built): `src/test/kog_notices_test.cpp` checks that only the exact words of the backend and the server make a notice, and that a
  line is never changed (the chat keeps it); the toast code runs only for servers listed for the KoG community, so a stock
  server or a stock client pairing shows exactly the old chat. `kog_quests_test.cpp`: a map without `kog_quest` lines gives an
  empty list, a vanilla map is unchanged.
- Server and backend (proposed, in their repositories when built): a client without the capability gets the chat line and no
  message; a client with it gets the message and no line; a 0.7 client gets the chat line; the backend uses `ChatMessage` when the
  game server has no `SERVER_CAPABILITY_PLAYER_NOTICE`; unknown kinds and capability ids are ignored.
- Not testable here: a running 0.6, 0.7 and DDNet client against a KoG server (needs the server, which was not built).

## 5. Rules kept

Capability negotiation only (no version checks); fail closed on certificate problems (unchanged, PR #29); a KoG client
behaves as a stock client on a vanilla server (the quest list is empty there, the theme is local).
