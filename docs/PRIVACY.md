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
| `client.kog.tw`, GitHub Releases | Update check and download | Not started yet: nothing calls the check, and `kog_check_updates` is not read yet | Nothing | `kog_check_updates`, `kog_update_url` |
| `tc_custom_communities_url` | Extra communities | Empty, so no request | Nothing | `tc_custom_communities_url` |
| Discord | Rich presence | Not compiled in (`DISCORD=OFF`), and `tc_discord_rpc` is off | Nothing | `tc_discord_rpc` |
| Links in menus (ddnet.org, wiki.ddnet.org) | Help pages | Only when you click | Nothing | None |

Not present: telemetry, analytics, advertising and crash uploads. `kog_hello` and `kog_telemetry` are off and not implemented yet.
