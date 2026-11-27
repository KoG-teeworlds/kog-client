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

**Fail-safe rule: one path, never two.** The server has one function `SendNotice(client, kind, key, text, value, limit)`:
if the client negotiated `notices`, it sends the message and **no chat line**; otherwise it sends the chat line exactly as
today. So a vanilla client, a KoG client without the capability, and a KoG client talking to an old server all behave as now,
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

## 5. Rules kept

Capability negotiation only (no version checks); fail closed on certificate problems (unchanged, PR #29); a KoG client
behaves as a stock client on a vanilla server (the quest list is empty there, the theme is local).
