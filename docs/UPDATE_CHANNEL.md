# Update channel

How the client updates itself, and why a compromised download host cannot hurt players.

## What the client trusts

Only an Ed25519 public key compiled into the client (`src/engine/client/kog_update_pubkey.h`, a primary and a backup
key). Not the host, not TLS, not GitHub. A manifest is used only if its signature verifies.

## Files

| URL | What |
| --- | --- |
| `https://client.kog.tw/update.json` | the manifest |
| `https://client.kog.tw/update.json.sig` | its signature, `kog-update-sig-v1 <key id> <signature hex>` |
| `https://client.kog.tw/base/files.json` | hashes of the data files of the first release, used to build the manifests |
| `https://github.com/KoG-teeworlds/kog-client/releases/download/...` | the files the manifest lists |

The site is served by GitHub Pages from the `published` branch of the public repository `kog-client-updates`.

## Manifest

```json
{
  "format": 2,
  "channel": "stable",
  "sequence": 7,
  "issued": 1766534400,
  "expires": 1769126400,
  "version": "0.2.0",
  "files": [{"path": "data/a.png", "sha256": "<64 hex>", "size": 10, "url": "https://github.com/KoG-teeworlds/kog-client/releases/download/v0.2.0/<sha256>"}],
  "platforms": {"linux-x86_64": {"client": {"sha256": "<64 hex>", "size": 100, "url": "..."}}},
  "remove": ["data/old.png"]
}
```

Updates exist for `linux-x86_64` and `win64`. macOS and Android are manual downloads for now.

## What the client checks

1. The signature, against the embedded keys. A key id that is not embedded, or an all-zero key, never matches.
2. The format, the channel, `version` as `MAJOR.MINOR.PATCH`, and `issued < expires <= issued + 90 days`.
3. Every path starts with `data/`, has no `..`, no hidden component and no executable extension; every URL is on
   `github.com/KoG-teeworlds/kog-client/releases/download/` or `client.kog.tw`, with no query or userinfo.
4. Not expired, and not issued more than two days in the future (a wrong clock fails closed).
5. `sequence` is not lower than the last one seen (stored in `kog_update_state.txt`): no rollback.
6. `version` is higher than the running one: no downgrade.

Each download is checked against its SHA-256 and size by the HTTP layer. Files are moved in with a backup and put
back if anything fails. If GitHub is unreachable the client keeps running its current version.

## Keys

- The **primary** private key exists in one place: the secret `SIGNING_KEY` of the environment `release-signing` of
  `kog-client-updates`. It is write-only on GitHub. It is not in any repository, in infrastructure state, in a CI log
  or on a laptop.
- The **backup** private key is created by the same command, written once to a local file for the owner to put into a
  password manager, and never uploaded anywhere.
- `scripts/kog/gen_signing_key.sh` does both on the owner's machine, never prints a private key, and writes the two
  public keys into the client and into `keys/` of `kog-client-updates`.
- Rotation: a release signed with the backup key that embeds a new primary and a new backup. Clients accept it
  because the backup key is embedded. After that the old primary is gone from the client.
- A lost primary key is handled the same way. Losing both keys means a new client has to be installed by hand.

## Release

1. `KoG release` in `kog-client` (manual): takes the artifacts of a build run, creates the packages and the draft
   manifest, attests all of them with `actions/attest-build-provenance`, and creates a prerelease.
2. `Sign release` in `kog-client-updates` (manual): runs in the environment `release-signing`. A required reviewer
   approves the job in GitHub; this is the only step the owner does. The job downloads the draft manifest and the
   client binaries, verifies their attestations and hashes, sets `sequence`, `issued` and `expires`, signs, checks the
   signature with the committed public key and publishes to the `published` branch.
3. The signing tool is built from a commit of `kog-client` pinned in `tools.lock`; changing it is a reviewed change.

The offline fallback is `scripts/kog/sign_release.sh` with the primary key file; it does the same without GitHub's
environment.

## Threat model

| Threat | Effect | Why |
| --- | --- | --- |
| The Pages host, the DNS record or `published` is taken over | nothing happens | no valid signature can be made; old manifests are refused by `sequence`, stale ones by `expires` |
| A release asset is replaced after signing | the update fails | the hash in the signed manifest does not match |
| A compromised build workflow | the signer notices client binaries and the draft manifest that have no attestation from the build workflow; it does not see changed source code | review of what is built stays a human job |
| Someone with write access to `kog-client-updates` changes the sign workflow | could print the key during an approved run | `CODEOWNERS` and a branch rule on `main`, the environment is limited to `main`, the reviewer sees which commit runs |
| The owner's GitHub account is taken over | the attacker can approve a run and read what the owner can read | use a hardware security key for the account's two-factor login; this is the weakest point |
| A manifest is replayed after a bad release | players on the bad version cannot be moved back | issue a new release with a higher version; revoke by letting `expires` run out (30 days by default) |
| The player's clock is wrong | updates stop, nothing else | fail closed |

## Not verified

The workflows have not run. GitHub release downloads redirect to another host; whether the HTTP layer follows
redirects as needed was not tested. The updater has been compiled and its verification logic unit-tested, but an
update has not been installed end to end. Windows and macOS builds were not made.
