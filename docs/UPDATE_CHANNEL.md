# Update channel

The client updates itself from a signed manifest. Nothing the network delivers is trusted until the signatures check.
The updater is only compiled with `-DAUTOUPDATE=ON`, and a release build fails while a channel's keys are placeholders.

## Channels

| Channel | Built from | Signatures | Keys | Manifest valid for |
| --- | --- | --- | --- | --- |
| `stable` | a release started by hand on `main` | CI key (A) and the owner's hardware key (B) | stable CI keys a/b, owner keys, offline backup | up to 90 days |
| `canary` | every push to `main`, automatic | CI key | canary CI keys a/b | up to 30 days |
| `dev` | every push to `dev`, automatic | CI key | dev CI keys a/b | up to 14 days |

A client checks a channel only against that channel's own keys. The channel name is inside the signed manifest, so a
manifest signed for one channel is refused on another. `kog_update_channel` (settings, or the config file) chooses the
channel; it is never set from the network. Canary and dev builds carry the channel in the version (`1.3.0-canary.57`),
are marked as unstable in the dialog ("Unstable build, best effort.") and are GitHub prereleases; the last 10 of each are kept.
Switching to a channel with an older version asks for confirmation in that session.

Best effort only: this client is volunteer-maintained, without warranty or a support guarantee, and may change or
disappear without notice. Nobody needs it to play on KoG.

## Keys

| Key | Held by | Made by | Where the client has it |
| --- | --- | --- | --- |
| CI keys a and b, per channel (3 channels, 6 keys) | private half: environment secret `SIGNING_KEY` and the encrypted OpenTofu state of `infra-github` | OpenTofu (`tls_private_key`, `org/update_keys.yaml`) | `KOG_<CHANNEL>_CI_KEYS` in `src/engine/client/kog_update_pubkey.h` |
| Owner hardware keys (stable, signature B) | the owner's FIDO2 security keys, never exported | `scripts/kog/gen_signing_key.sh` | `KOG_STABLE_OWNER_KEYS` |
| Offline backup CI key (stable) | the owner's password manager, not in any repository | `scripts/kog/gen_signing_key.sh` | `KOG_STABLE_BACKUP_CI_KEYS` |

The public CI keys reach the client as a pull request that `infra-github` opens from the OpenTofu outputs
(`scripts/kog_client_keys.py`); merging it is the audit point for which keys the client trusts. The hardware key and the
backup key are never generated or stored in infrastructure.

Both signatures are Ed25519. A is a raw signature in `update.json.sig`. B is an OpenSSH signature
(`ssh-keygen -Y sign -n kog-client-release`, type `sk-ssh-ed25519@openssh.com`) in `update.json.sigb`; the client checks the
application, that the key was touched (user presence flag), the namespace and the signature, with Monocypher.

### Rotating a CI key

A channel has two CI keys, a and b; the client trusts both, and the secret holds the active one. To rotate without
leaving an installed client behind:

1. Make sure the client in the field already trusts the key that will be used next (it does: a and b are always shipped).
2. Switch `active` in `org/update_keys.yaml` to the other slot and apply: the secret now holds the other key.
3. Run the workflow *Organisation* with `replace=<channel>_<old slot>` (`tofu apply -replace=tls_private_key.update_ci["stable_a"]`):
   the retired slot gets a fresh key. The pull request that `infra-github` opens puts its public key into the client.
4. Ship a client release with that pull request before the next rotation. Only then may the slot that was just filled become active.

A key that is not shipped in a client release is never made active. After a loss of the CI keys the owner backup key
(stable) still signs together with the hardware key, and a client release replaces the CI keys.

## Release

Stable:

1. `KoG release` (workflow dispatch on `main`): refuses unless the commit is the head of `main`, the required checks passed, and
   the version equals `KOG_CLIENT_VERSION`. It builds the client only (`-DSERVER=OFF -DTOOLS=OFF`), fails if a server is in a
   package, attests the files (provenance, source ref `refs/heads/main`) and makes an immutable prerelease with `update.draft.json`.
2. `Sign release` in `kog-client-updates`, three jobs: *verify* (no secrets) checks every asset with
   `gh attestation verify --signer-workflow .../kog-release.yml --source-ref refs/heads/main`, the hashes, that the release is
   immutable, that the version is higher than the published one and that the commit is on `main`, and makes the manifest;
   *sign* (environment `release-signing`) signs only that manifest; *publish* (environment `release-publish`, reviewer) needs both signatures.
3. The owner runs `scripts/kog/approve_release.sh stable <version> <run id>`: it verifies the release again on the owner's machine,
   rebuilds the manifest and compares it byte for byte with CI's, prints a summary, asks for the version, and signs with the
   hardware key. The signature goes to the branch `approvals` of the updates repo.
4. The owner approves the *publish* job. It checks both signatures with the keys pinned in `tools.lock`, then commits to
   `published`. The commit message holds the manifest SHA-256, the source commit and the run URL.

Canary and dev run the same checks in `kog-channel.yml`, with one signature and the environments `canary-signing` and
`dev-signing` (restricted to `main` and `dev`). They are never built for forks or pull requests.

`scripts/kog/verify_published.sh [channel]` verifies what the servers serve, from the keys in the repository, on both hosts.

### Emergency stop

`scripts/kog/halt_release.sh stable` signs a copy of the current manifest with `halted: true` and a higher sequence, using the
hardware key and the offline backup CI key, and publishes it without waiting for CI. Clients that see it do not update.

## Manifest

`update.json` is JSON, format 2. Fields:

| Field | Meaning |
| --- | --- |
| `format`, `channel`, `version`, `sequence`, `issued`, `expires` | required; `sequence` rises per channel; validity is at most the channel's maximum |
| `files[]` | `path` (below `data/`), `sha256`, `size`, `url`, optional `mirrors[]` tried in order |
| `platforms.<linux-x86_64\|win64>.client` | the client binary: `sha256`, `size`, `url` |
| `remove[]` | data paths to delete |
| `packages[]` | optional: `name`, `sha256`, `size`, `url` of the installers (zip, tar, dmg) for the download page; clients ignore it |
| `rollout_percent` | optional, 0-100: share of installs that get the update. The bucket comes from a random per-install value in the config, never sent anywhere |
| `halted` | optional: the update is withdrawn, clients do not apply it |
| `min_version` | optional: an older client shows a blocking "update required" dialog that explains why; never silent |
| `notes_url` | optional: on `client.kog.tw` or `github.com/KoG-teeworlds` only |
| `changes[]` | optional: up to 3 plain-text lines of at most 120 characters |

A field that is absent means the old behaviour. Unknown fields are ignored by the client but are signed with the rest.
Paths and URLs come from allow-lists; executables are never updated through `files`.

## What the client checks

- both signatures (stable) or the CI signature, against the channel's keys; the channel in the payload equals the configured one
- `sequence` is higher than the last one applied on that channel, the manifest has not expired and is not from the future, the version has the channel's form
- a version older than the installed one is only applied after the user confirmed a channel switch
- `halted`, `rollout_percent` bucket, `min_version`, and a version the client rolled back from before (bad-version memory)
- every file's SHA-256 and size before it replaces anything, enough free disk space, per-file retry, then the next mirror

The manifest is fetched from `https://client.kog.tw/<channel>/update.json`, then from
`https://raw.githubusercontent.com/KoG-teeworlds/kog-client-updates/published/<channel>/update.json`. The fallback is on another
name than the primary because GitHub redirects `github.io` to the custom domain. Trust never depends on the host: both signatures are verified.
Offline or unreachable is quiet. The settings show the last check and its result.

### Fail-safe

The previous version stays beside the new one (`.kogprev`), replaced atomically. A start marker is written on launch and
removed when the start screen is reached; after 2 starts that did not reach it, the client restores the previous version and
remembers the bad one. Settings are never replaced; a config backup is made when the version changes.

## Threat model

| Threat | Result |
| --- | --- |
| Stolen GitHub token or malicious pull request | cannot publish: the release workflow runs only from `main`, publishing needs the environments and, for stable, the hardware key |
| Malicious workflow change on `main` | stable still needs signature B, made after the owner's own verification; canary and dev are exposed, which is why they are opt-in |
| CI key stolen | stable needs the hardware key as well; canary/dev testers are exposed until the key is rotated (above) |
| Owner hardware key stolen | needs the PIN and a touch, and the CI signature; revoke by a client release that drops the key |
| Malicious or compromised action | no third-party actions; first-party actions are pinned by SHA; the signing job has no code from the release, only the verified manifest |
| Pages or DNS takeover of `client.kog.tw` | cannot forge a manifest; it can withhold updates, so the fallback host serves the same branch under another name |
| Rollback or freeze | `sequence`, expiry, version checks and the bad-version memory; a freeze ends when the manifest expires |
| Channel confusion, cross-channel replay | per-channel keys and the channel inside the signed payload |
| `main` rewritten | the asset's source commit must be on `main`, branch rules forbid force-pushes, and the owner sees the commit in the summary |
| Asset swap on a release | releases are immutable, attestation and hash checks must agree, and the client checks hashes again |
| Key loss | two hardware keys, the offline backup key, and a client release that replaces keys |

### Residual risks

- **The CI private keys (both slots of every channel) are in the encrypted OpenTofu state of a private repository without
  branch protection (Free plan).** Anyone who can change an `infra-github` workflow and decrypt the state can read them. For
  stable that is not enough: signature B is needed, and the hardware key is never in infrastructure. For canary and dev the
  blast radius is the opted-in testers. Mitigations: the state is encrypted, apply runs only on `main`, environment secrets are
  write-only, and a key can be replaced with `replace=<channel>_<slot>` once its successor is in a client.
- The reviewer of `release-signing` and `release-publish` is the owner, who may approve their own run (one maintainer). A second
  maintainer should replace that.
- Releases of a data file reuse the hash of an earlier verified release; the earlier verification is trusted.
- Canary and dev signing and publishing run in one job, so the deploy key and the CI key share an environment.
- The Windows free-disk check and the executable-bit handling were not run on Windows.

## Out of scope

Delta updates beyond whole-file hashes, silent updating, and crash telemetry (an opt-in idea for canary and dev only).
Server-side hints about a channel are a suggestion and not built.

## Not verified

The workflows were checked with actionlint and shellcheck but have not run on GitHub; the ruleset, environment and key
resources are in `infra-github` pull request 217. Hardware key signatures were checked with a software emulation of the
format against OpenSSH, not with a real security key.
