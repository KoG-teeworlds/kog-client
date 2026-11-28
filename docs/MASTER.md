# The KoG master list (client side)

The server side, its format file and the local test recipe are in the repository `KoG-teeworlds/kog-master`. This page is what the client does.
Design: `docs/KOG-MASTER-SERVER.md` in `KoG-teeworlds/kog-2.0-server` (#241). Nothing here is needed to play: with the feature off or the master
down the client behaves as before.

Labels: **[R]** read in this repository, **[T]** run or tested, **[N]** not verified.

## What it does

The client can fetch a **signed list of KoG servers** and add those servers to the server browser (community `kog`). The list is
public, the same for everybody, and checked against keys that are built into the client. It is a second source: the DDNet list is
still loaded as before **[R]** `serverbrowser_http.cpp`.

| Setting | Default | Meaning |
| --- | --- | --- |
| `kog_master` | `1` | Kill switch. `0`: no request, cached list not used |
| `kog_master_url` | empty (`KOG_MASTER_URL_DEFAULT` in `src/game/kog_version.h`) | URL of the signed list. **Empty = off.** Only `https://*.kog.tw` on port 443 is accepted (the `EndpointAllowed` rule of the login code, also used here) |
| `kog_master_ddnet_fallback` | `1` | Reserved: the DDNet list is always loaded in this version (see the plan below) |
| `kog_master_dns` | `0` | Reserved, **not implemented**: DNS discovery (see below) |

Why the URL is empty: no master is deployed yet and the built-in root keys (`src/engine/client/kog_master_pubkey.h`) are placeholders, so a
release would have nothing to verify against. When the dev master is up: put the root keys of the channel in that header and set
`KOG_MASTER_URL_DEFAULT` for the channel. With no root key the feature stays off even if a URL is set.

## When it fetches

At start, and each time the server browser refreshes, but at most once in 30 seconds. No timer. One request at a time, never in parallel.
Order of sources, the first that gives a valid list wins:

| Step | Source | What the player sees if it works / if it fails |
| --- | --- | --- |
| 1 | `kog_master_url` (the master of the channel, behind Cloudflare) | KoG servers in the browser at once / next step |
| 2 | The Pages mirror `https://client.kog.tw/list/<channel>/servers.json` (only when step 1 is the official address) | same bytes / next step |
| 3 | URLs named in the last signed list (`masters`, type `kog`), only this run | same / next step |
| 4 | The **last good signed list on disk** (`kog_master_list.json`), checked again on load: valid up to its expiry plus **7 days** | KoG servers as of the last start / next step |
| 5 | The DDNet list and the DDNet info file (`servers-kog`), as before | KoG servers as DDNet's list knows them, **unsigned** |

Steps 1 to 3 are tried one after the other within a fetch; step 4 is loaded at start so the browser has something before the first
answer; step 5 is always loaded in this version because the browser's DDNet tab needs it. A failed fetch is retried at the next refresh
(30 s at the earliest). Nothing is shown to the player on failure: it is a line in the log (`kogmaster`).

## Format

Defined in `kog-master` (`docs/FORMAT.md`, `shared/SPEC.md`), checked here by `src/test/kog_master_test.cpp` including a **list that was
signed by the Rust tool and the python generator** (`src/test/kog_master_vector.h`).

* The body is JSON, at most 1 MiB: `{"v":1,"delegation":{payload,root,sig},"list":{payload,sig}}`. A `payload` is a JSON document written as
  a string; the signed bytes are the string's bytes after a fixed prefix (`kog-master-delegation-v1\n`, `kog-master-list-v1\n`). No
  canonicalisation.
* **Delegation**: signed by a root key (`a` or `b`, compiled in, chosen by the first 4 bytes). It names the key that signs lists
  (`pub`, `kid`), the channel (`env`), `not_before`, `not_after` (at most 45 days apart) and `seq`. The client takes a new delegation from
  every list it accepts, so the frequent rotation (30 days) needs no client release. Only a root change needs one.
* **List**: signed by the delegated key. `env` must be the channel of this build, `kid` the delegation's. At most 2000 servers, 4
  addresses each (`tw-0.6+udp://` or `tw-0.7+udp://`, IP and port only), names and maps printable, at most 64 bytes, community `kog`,
  at most 8 `masters`.
* A build of channel `stable` never accepts a list of `dev` or `canary`: the key chain and the `env` are both checked **[T]**.

### Expiry, order and stale copies

* A list is refused after its `not_after` plus 2 days of clock skew. A cached list is used up to 7 days beyond that (`MAX_STALE_SECONDS`).
* The client remembers the highest delegation `seq` and list `seq` (`kog_master_state.txt`). A copy with a **lower** `seq` is a **stale
  copy**: a CDN may serve 104 after 105 inside its cache time. It is ignored silently (debug level), the current list is kept, and it is
  **not** a failure: no next source is tried **[T]**. A copy that is really old is refused by its expiry instead, so `not_after` bounds a
  replay. Everything else wrong (bad signature, wrong channel, expired, malformed) is a failure and moves to the next source.
* The request has no cookie, no query string, no conditional header and does not follow redirects.

### Trust of what is in the list

A server in a signed list gets `CServerInfo::m_KogSigned = true` (also an entry that came from the DDNet list and is in the signed one).
Servers from the DDNet list are **unsigned third-party data**. Anything that needs to trust a server (the KoG hello, the login proof, a
"KoG server" badge) must require `m_KogSigned`, or later a valid server certificate chain. Nothing uses the flag yet.

### The `masters` array

`[{"t":"kog","u":url}, {"t":"ddnet-json","u":url}]`, in fallback order. `kog`: must pass `EndpointAllowed` (https, `*.kog.tw`, 443).
`ddnet-json`: the unsigned DDNet list, only the four exact URLs `https://master1..4.ddnet.org/ddnet/15/servers.json`. Anything else is
dropped, the rest of the list stays usable **[T]**. `kog` entries are followed in the same run; `ddnet-json` entries are validated and remembered but
not used yet: the browser keeps its own built-in list of four URLs until the DDNet list is switchable (plan, step 2).

## DNS discovery (future, not implemented)

Records `_kogmaster.<env>.kog.tw` (production: `_kogmaster.kog.tw`), TXT, TTL 300 to 3600, one answer under about 512 bytes:
`v=KOG1; m=<kog list url>; m=<kog list url>; f=ddnet-json:<url>; e=<yyyy-mm-dd>` (each string at most 255 bytes).

* The list is signed, so a spoofed record cannot forge servers or keys. It can only steer the client to a host (which then sees its IP) or
  withhold data. Therefore URLs from DNS pass the same rules as `masters` (`EndpointAllowed`, the exact DDNet URLs, the same size,
  sequence and expiry checks) and are treated as untrusted hints. The signed `masters` array wins over DNS when they differ.
  DNSSEC is on for `kog.tw`; the client uses the validation only where its resolver gives it.
* The **TXT lookup itself is a shared component** (also for the trust bundle) that has not been decided: the game client only has the
  system resolver API. Until the owner decides, `kog_master_dns` does nothing.

## Local testing

```
cmake -S . -B build -G Ninja -DDEV=ON -DKOG_CHANNEL=dev -DKOG_BUILD_NUMBER=1 -DKOG_MASTER_DEV_ROOT=<root a public from the master demo>
```

`KOG_MASTER_DEV_ROOT` exists only for `-DKOG_CHANNEL=dev -DDEV=ON` (CMake refuses it otherwise): it adds one more root key and lets
`kog_master_url` name `http://127.0.0.1:<port>`. It does not exist in any package. Also set `http_allow_insecure 1` (the existing DDNet
switch for plain http) and `kog_master_url "http://127.0.0.1:8080/v1/dev/servers"` in the console or the settings file. The log line
`kogmaster: Accepted a list with N servers` shows it worked **[T]**. Steps for the master side: the README of `kog-master`.

## Inventory: what the client takes from DDNet today

**[R]** unless marked. Line numbers are of this branch.

| Dependency | Where | Use | KoG replacement | New KoG endpoint | Breaks if just removed |
| --- | --- | --- | --- | --- | --- |
| `master1..4.ddnet.org/ddnet/15/servers.json`, best one by speed, file `ddnet-serverlist-urls.cfg` overrides | `serverbrowser_http.cpp:543-585` | The server list, all communities | The signed KoG list for KoG; others only through an optional DDNet community | the master (this page) | The browser is empty except KoG; the DDNet tab disappears |
| `info.ddnet.org/info` (`?name=` with `br_indicate_finished`) | `client.cpp:5388`, `serverbrowser.h:22` | Communities (incl. the `kog` entry and its icon), countries, `servers`, `servers-kog`, `maps` (finish flags), `location`, `news`, `map-download-url`, `stun-servers-ipv4/6`, `points`, `version`, `connecting-ip`, `warn-pnglite-incompatible-images` (`client.cpp:2616-2690`, `serverbrowser.cpp:1494-1668`) | Communities and `servers-kog`: the signed list or a KoG community file; `news`, `version`: the signed update manifest; `map-download-url`: KoG map URL; `stun-servers`, `connecting-ip`: a KoG file or drop; `points`, `maps` finish flags: from the KoG account later or drop | a signed `communities` file next to the list; a STUN list | KoG's own presence in the community list, finish marks, STUN hole punching, the "new version" hint |
| `maps.ddnet.org` | `config_variables.h:149`, `client.cpp:1810` | Map download default | `cl_map_download_url` pointing at a KoG map host | a KoG map URL (`maps.kog.tw` exists as a name in the zone **[N]**) | Players without the map cannot join a vanilla map server |
| `skins.ddnet.org` | `config_variables.h:162-163` | Skin download | A KoG skin host or drop | possibly | Other players show the default skin |
| Community icons named in info | `serverbrowser.cpp` | Icons | Icons of the KoG file | no | No icons |
| Menu links (ddnet.org, wiki.ddnet.org) | `client.cpp` | Help pages | Own pages or drop | no | Dead links |

`docs/PRIVACY.md` lists each of these as a row; the row for the master is new.

## Staged plan to depend less on DDNet

1. **This slice.** KoG master and signed `masters` in addition to the DDNet path, last good list cached on disk. DNS later.
2. **Next.** A build option `KOG_NO_DDNET_INFRA` and a setting that use only KoG endpoints, with a test that records every request and fails on any `*.ddnet.org` host.
   The player can add the DDNet community back through the existing `tc_custom_communities_url`: its code is named
   `...DDNetInfo` and reads a file in the DDNet info format (`custom_communities.cpp`), so a DDNet-format community file by URL looks possible
   **[N]** (not run). Needs the KoG endpoints from the table (communities file, news in the manifest, map host).
3. **Later, only if the merge cost is acceptable.** Deleting code in `serverbrowser*.cpp` (3,178 lines in these two files with `serverbrowser_http.cpp`) and `client.cpp` (5,733) is
   upstream-heavy; prefer a few guarded lines over deletion. Measure the churn against the upstream tag before deciding.

Constraint: KoG **game servers keep registering at the DDNet master** (`sv_register_url`, `config_variables.h:474`) so stock clients find them. The
KoG client only stops depending on it. Registering at both (a KoG master and DDNet, each with a kill switch, plus a server version bump) is a follow-up
slice, not this one.

## If everything is down

| Layer | Protects against |
| --- | --- |
| Cloudflare cache (about 15 s) | a flood of list requests; a short master outage (stale served) |
| Pages mirror | the master or Cloudflare being down |
| The signed `masters` array | a master moved or added: no client release |
| DNS discovery (later) | the same, at bootstrap |
| Last good list on disk (7 days past expiry) | the network being down |
| DDNet list and info (step 5) | all of the above; also the only source for other communities |

When everything is down the browser shows what DDNet's list gives (KoG servers via `servers-kog`), or, with DDNet down too, the cached
DDNet list the client already keeps. The master is a convenience and a trust anchor for later features, not a requirement for play.
Volumetric attacks on the origin cannot be fully stopped (see the design, 3.3): the layers above are why it is not fatal.
