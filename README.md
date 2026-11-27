> [!WARNING]
> # Best effort only
>
> **The KoG client is maintained on a best-effort basis by volunteers.** There is no warranty, no support guarantee and no release schedule. It can break, change or disappear without notice. Security fixes are made as time allows. Builds from `main` are not releases and can be broken.
>
> **You do not need this client to play on KoG.** Stock DDNet clients keep working. If you want guaranteed support or a stable experience, use the official DDNet client.
>
> Use at your own risk. Bug reports and pull requests are welcome, but please do not expect an answer within any time frame.

# KoG Client

An open-source fork of [TClient](https://github.com/TaterClient/TClient), which is a fork of the
[DDNet](https://ddnet.org) client, for [King of Gores](https://kog.tw). Work in progress: there is no stable release yet; canary builds are published as GitHub prereleases.

## Status

Done:

- based on TClient 10.9.0 and DDNet 20.1
- removed the ChaiScript scripting engine, the chat translation (it sent chat to a third-party service) and the
  committed Steam redistributable
- no longer contacts TClient's servers (update check, information file, identification message to game servers)
- the updater verifies an Ed25519 signature on its update manifest, checks every downloaded file against its SHA-256
  and restores the previous files if an update fails (unit tests included); the keys are compiled in
  (`src/engine/client/kog_update_pubkey.h`): the canary and dev keys are real, so those channels update; the stable
  keys are still placeholders, so stable does not update

- login with a KoG account (OAuth 2.0 in the browser, no password in the game), on by default in canary and dev
  builds only; the client proves its device to KoG servers that ask for it (`cl_kog_auto_login`, off by default), see
  [docs/LOGIN.md](docs/LOGIN.md)
- KoG hello: tells KoG servers which KoG features the client supports and uses only what both sides have
  (`kog_hello`, on in canary and dev builds only), see [docs/KOG-HELLO.md](docs/KOG-HELLO.md)
- signed canary and dev releases and the download page at client.kog.tw

Planned, not built yet:

- stable releases (the stable keys are placeholders)
- a Homebrew tap for macOS: the design is in [docs/HOMEBREW.md](docs/HOMEBREW.md)
- opt-in client-side network statistics
- a page that shows a returning player what changed and lets them decide each change, see [docs/WHATS-CHANGED.md](docs/WHATS-CHANGED.md)

## Build

Same as DDNet, see [docs/BUILDING.md](docs/BUILDING.md). The updater needs `-DAUTOUPDATE=ON` (default OFF; the release workflows turn it on). Run the tests with
`cmake --build . --target run_tests`.

How a release is made and signed (owner guide): [docs/RELEASING.md](docs/RELEASING.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security reports: [SECURITY.md](SECURITY.md).

## Licence

zlib, as DDNet. Teeworlds, DDRace, DDNet and TClient notices are kept in [license.txt](license.txt). What is ours,
what is not, and which assets still need a licence check: [NOTICE_KOG.md](NOTICE_KOG.md). The original TClient
README is [README_TCLIENT.md](README_TCLIENT.md).
