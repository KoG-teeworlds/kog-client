# KoG Client

An open-source fork of [TClient](https://github.com/TaterClient/TClient), which is a fork of the
[DDNet](https://ddnet.org) client, for [King of Gores](https://kog.tw). Work in progress: there is no release yet.

## Status

Done:

- based on TClient 10.9.0 and DDNet 20.1
- removed the ChaiScript scripting engine, the chat translation (it sent chat to a third-party service) and the
  committed Steam redistributable
- no longer contacts TClient's servers (update check, information file, identification message to game servers)
- the updater verifies an Ed25519 signature on its update manifest, checks every downloaded file against its SHA-256
  and restores the previous files if an update fails (unit tests included); updates stay disabled until the
  project's signing key has been generated

Planned, not built yet:

- login with a KoG account (OAuth 2.0 in the browser, no password in the game)
- KoG messages between client and server, announced with capabilities
- opt-in client-side network statistics
- signed releases and the download page at client.kog.tw

## Build

Same as DDNet, see [docs/BUILDING.md](docs/BUILDING.md). The updater needs `-DAUTOUPDATE=ON`. Run the tests with
`cmake --build . --target run_tests`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security reports: [SECURITY.md](SECURITY.md).

## Licence

zlib, as DDNet. Teeworlds, DDRace, DDNet and TClient notices are kept in [license.txt](license.txt). What is ours,
what is not, and which assets still need a licence check: [NOTICE_KOG.md](NOTICE_KOG.md). The original TClient
README is [README_TCLIENT.md](README_TCLIENT.md).
