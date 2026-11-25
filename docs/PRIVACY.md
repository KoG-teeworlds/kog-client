# Privacy: what the client connects to

This lists every outbound connection of the client as built from this repository, with its default. It was read from the source and checked by running the client for 20 seconds without joining a server. Every host you connect to sees your IP address.

| Endpoint | Why | Default | What is sent besides your IP | Setting |
| --- | --- | --- | --- | --- |
| `master1.ddnet.org` to `master4.ddnet.org` (HTTPS) | Server list | On, at start | Nothing | None; the server browser needs it |
| `info.ddnet.org/info` (HTTPS) | Communities and countries for the server browser | On, at start | Nothing. Your player name is added only when `br_indicate_finished` is on | `br_indicate_finished` (off by default) |
| Game servers (UDP) | Playing, and pinging every server in the list | On | Your name, clan, skin and country to the server you join; the client version string | None |
| `maps.ddnet.org` (HTTPS) | Download a map you do not have | On demand | The map name. A server can name its own map host, which then sees your IP | `cl_map_download_url` |
| `skins.ddnet.org` (HTTPS) | Download skins of players you meet | On demand | The skin names of players on your server | `cl_download_skins` |
| Community icon hosts named in the info file | Icons in the server browser | On demand | Nothing | None |
| `client.kog.tw`, then `raw.githubusercontent.com` if that fails; GitHub Releases for the files | Update check; files are downloaded only after you press Update now and confirm | On, at start, in builds made with `-DAUTOUPDATE=ON` (the release builds of all three platforms; a plain build has no updater). Also when you press Check now or change the channel | Only the request for `/<channel>/update.json` and its signature file or files, with the HTTP user agent `KoG-Client/<version> (<platform>; <arch>)`; the same user agent is sent to the KoG login server and for the update files. Requests to the server list, map and skin downloads and other DDNet services keep the inherited DDNet one. No account, device or install identifier | `kog_check_updates` (1), `kog_update_channel`, `kog_update_url`; see `docs/UPDATE_CHANNEL.md` |
| `kog.tw` (HTTPS, or `cl_kog_account_url`) | Log in with KoG: discovery, token, user info, device registration and removal | Only when you press Log in or Log out, and only if `cl_kog_login` is on (canary and dev builds) | The OAuth code and PKCE verifier, the device name (a neutral `KoG Client (macOS)`, `(Windows)` or `(Linux)` by default, editable) and the public half of a device key | `cl_kog_login`, `cl_kog_account_url`, `cl_kog_device_name`; see `docs/LOGIN.md` |
| `tc_custom_communities_url` | Extra communities | Empty, so no request | Nothing | `tc_custom_communities_url` |
| Discord | Rich presence | Not compiled in (`DISCORD=OFF`), and `tc_discord_rpc` is off | Nothing | `tc_discord_rpc` |
| Links in menus (ddnet.org, wiki.ddnet.org) | Help pages | Only when you click | Nothing | None |

Not present: telemetry, analytics, advertising and crash uploads. `kog_telemetry` is off and not implemented yet. `kog_hello` (off in stable) sends the client version, its channel and the numbers of the KoG features it supports, only to servers listed for the KoG community, see [KOG-HELLO.md](KOG-HELLO.md).

## Update check in detail

Read from `CTClient::OnInit` and `CUpdater` in `src/engine/client/updater.cpp`. Set `kog_check_updates 0` (also in the settings) and no request is made. With it on, the check runs once at start and again on Check now or a channel change. It only fetches the signed manifest; nothing is installed until you accept. If the host cannot be reached, the client tries `raw.githubusercontent.com`; if that fails too, it logs "No connection to the update server", remembers "no connection" and shows nothing. A manifest with a bad signature, from the wrong channel or without a key in the build is dropped. The stable keys are still placeholders, so a stable build makes the request and then refuses it ("not configured"). The manifest lists only Linux and Windows files, so on macOS the check finds no entry and offers nothing: update there by downloading the new `.dmg`. The update row is read from the source; the 20 second run did not cover it, and the macOS result was not run.
