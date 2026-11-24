# Releasing the client

This page is for the owner. It says how a stable release is made, how you approve its signing, and what canary and
dev do on their own. It was written on 2026-11-24 from the workflows, the scripts in `scripts/kog/`, `docs/UPDATE_CHANNEL.md`
and the settings in `infra-github`. The design and the threat model are in `docs/UPDATE_CHANNEL.md`; this page is the
procedure.

## What works today and what does not

Read this first. Parts of the chain are written but not in place.

| Piece | State on 2026-11-24 |
| --- | --- |
| Release tag rules (only repository admins create `v*` tags; a `v*` tag cannot be moved or deleted) | in place (rulesets of `kog-client`) |
| `KoG release` workflow (tag, checks, build, immutable draft prerelease with `update.draft.json`) | file exists on `main`, never run (no `v*` tag exists) |
| Immutable releases | enabled for the repository |
| Environments `release-signing` and `release-publish` (reviewer: Avolicious) in `kog-client-updates` | in place; `release-signing` has a secret `SIGNING_KEY` |
| Workflow `Sign release` in `kog-client-updates` (jobs verify, sign, publish) | **planned, not in place.** The repository has only a README on `main` and one open pull request (#1, the site). No workflow, no `published` branch. |
| Your hardware key (signature B) and the offline backup key | **not made yet.** `src/engine/client/kog_update_pubkey.h` has empty `KOG_STABLE_OWNER_KEYS` and `KOG_STABLE_BACKUP_CI_KEYS`, and `KOG_UPDATE_KEYS_PLACEHOLDER_STABLE 1`: the stable channel does not update. |
| Stable CI keys a and b | made by OpenTofu; their public halves are in the header |
| Canary and dev (`KoG canary`, `KoG dev`) | workflows run on every push, but the last runs **fail** in the step "Sign and publish": `SIGNING_KEY is empty: the environment canary-signing gave this job no secret of that name` (run 45368646664). Nothing is published yet. Another builder is working on this path. |
| `client.kog.tw` | did not answer when I asked; the fallback `raw.githubusercontent.com/.../published/canary/update.json` answers 404 because the branch `published` does not exist |
| Rollout percent, minimum version, notes and changes for a stable release | the code to put them in a manifest exists (`assemble_release.sh`, `make_manifest.py`), but `KoG release` has no inputs for them and does not pass them. The design says they go to `Sign release`, which does not exist. |
| `approve_release.sh`, `halt_release.sh`, `verify_published.sh`, `gen_signing_key.sh` | scripts exist; `scripts/kog/test_release_chain.sh` tests them with a software emulation of the key. Never run with a real security key or against a real release. |

So: today you can make the keys, push a tag and get a draft release. You cannot yet finish a stable release.
The sections below mark each step as **works** or **planned**.

## The two signatures

A stable manifest (`update.json`) needs two signatures. The client accepts it only if both are valid.

- **Signature A, the CI key.** An Ed25519 key that OpenTofu makes (`tls_private_key.update_ci`, `org/update_keys.yaml`
  in `infra-github`). Its private half is the secret `SIGNING_KEY` of the protected environment `release-signing`. A job
  in that environment signs the manifest after you approved the deployment. It lands in `update.json.sig`.
- **Signature B, your hardware key.** An `ed25519-sk` key on your FIDO2 security key, made with
  `ssh-keygen`, signed with `ssh-keygen -Y sign -n kog-client-release`. Needs the PIN and a touch. The private half never
  leaves the device. It lands in `update.json.sigb`.

Why neither alone is enough: A is held by GitHub and by the encrypted OpenTofu state of a private repository without branch
protection (Free plan). Anyone who can change an `infra-github` workflow and decrypt the state could read it, and a
malicious change on `main` could make CI sign something bad. B alone is a key that signs whatever you are shown on
your own machine, but it never runs in CI and cannot publish. Together: CI cannot release without you, and you cannot
release something CI did not build and attest. Canary and dev have only A, which is why they are opt-in and marked unstable.

The client checks a channel only against that channel's keys, and the channel name is inside the signed manifest.

## One-time setup (the keys)

Needs: OpenSSH 8.3 or newer with security key support (`ssh-keygen -t ed25519-sk` must work; the `ssh-keygen` Apple ships
may not support it, in which case install OpenSSH from Homebrew; I did not check this on your machine), a C compiler
(`cc`), `python3`, `gh` logged in, and two FIDO2 security keys.

1. From a checkout of `kog-client` on `main`:

   ```
   scripts/kog/gen_signing_key.sh
   ```

   (`--keys 1` makes one key only; two is the default and the safe choice: one to use, one spare in a safe place.)
   It builds the small signing tool, then for each key prints `Plug in security key N and touch it when it blinks.`
   (`ssh-keygen` asks for a PIN to set; the key is made resident and with `verify-required`). Then it makes the offline
   backup CI key. It refuses to overwrite files that exist.

2. Where the files go:

   | File | What it is | What to do |
   | --- | --- | --- |
   | `~/.config/kog-client-signing/owner_sk_1`, `owner_sk_2` (and `.pub`) | key handles; useless without the device | keep; back them up if you like. If lost, `ssh-keygen -K` copies them back from the device (they are resident). |
   | `~/kog-client-backup-signing-key.txt` | the **offline backup CI private key**; the only copy | put it into your password manager, then delete the file. Nothing else holds it. |

   The paths can be changed with `KOG_SIGNING_DIR` and `KOG_BACKUP_KEY_FILE`.

3. The script writes the public halves into `src/engine/client/kog_update_pubkey.h` (`KOG_STABLE_OWNER_KEYS`,
   `KOG_STABLE_BACKUP_CI_KEYS`) and prints `hardware key public keys and the backup CI public key are in ...: commit it`.
   Make a pull request with that file (squash, signed commits, like any change to `main`) and merge it.

4. The CI public keys get into the same header by themselves: after an apply in `infra-github`, `apply.yml` runs
   `tofu output -json update_ci_public_keys | scripts/kog_client_keys.py --apply`, which opens the pull request "Update
   the update keys" on `kog-client` (the last one is #7, merged). Merging it is the audit point for which keys the client
   trusts. A channel's placeholder flag becomes 0 when its keys are all there; for stable that also needs your keys.
   I did not check whether the script clears the stable flag only after your keys are merged; look at the header after step 3.

5. What to back up: the password-manager entry with the backup CI private key, the spare security key (in a different
   place), and the handle files. Write down the PINs.

## Releasing stable, step by step

Before you start: the stable header is no longer a placeholder (setup above), and `Sign release` exists (**planned**).
Without those the steps after 4 cannot work.

1. **Works.** Change `KOG_CLIENT_VERSION` in `src/game/kog_version.h` to `X.Y.Z` in a pull request, let it pass
   the required checks (`Build (linux)`, `Build (macos)`, `Build (windows)`, `Analyze (cpp)`, `Analyze (python)`, `Spelling`,
   `release-chain`, `gitleaks`) and merge it. The version must be higher than the published one (a check in
   `verify_release.sh`).
2. **Works (rules), never run.** Make a signed tag on the head of `main` and push it. Only a repository admin can create
   a `v*` tag.

   ```
   git switch main && git pull
   git tag -s vX.Y.Z -m "KoG Client X.Y.Z"
   git push origin vX.Y.Z
   ```

   A pushed `v*` tag cannot be moved or deleted (ruleset "Release tags never change"). If the workflow fails afterwards,
   you cannot reuse the tag: fix the cause and release the next version number (my reading of the ruleset: nothing in
   it allows deleting).
   The workflow does not itself check that the tag is signed; the rule is that you sign it.
3. **Works, never run.** `KoG release` starts on the tag. Job `guard` refuses unless: the tag looks like `vX.Y.Z`; its commit is
   on `main` and is **the head of `main` now**; `KOG_CLIENT_VERSION` equals the tag; and the eight checks above succeeded
   on that commit. Job `package` builds the client only (no server). Job `release` assembles the files, attests them
   (build provenance), and runs `gh release create vX.Y.Z --prerelease` with `update.draft.json`. The release is immutable.
   Its note says it is not usable until it is signed twice. If `guard` says "the commit is not the head of main any
   more", something was merged after you decided: make the next version.
4. **Planned.** Start the `Sign release` workflow in `kog-client-updates` (per the design, with rollout, minimum version,
   notes and changes as inputs). Three jobs: `verify` (no secrets) checks the attestations, the hashes, the immutability,
   that the commit is on `main` and the version is higher, and makes the manifest; `sign` (environment `release-signing`)
   signs it with key A; `publish` (environment `release-publish`) publishes after both signatures exist. None of this
   is in the repository yet.
5. **Planned.** In `kog-client-updates`, Actions, open the run, press **Review deployments**, tick `release-signing`,
   **Approve and deploy**. The `sign` job then signs the manifest the `verify` job made. In the design, your signature
   (step 6) comes after the CI manifest exists, so you can do step 6 as soon as the `verify` job has finished.
6. **Script exists, workflow it needs is planned.** Approve with your key. `RUN_ID` is the number in the URL of the
   `Sign release` run (`.../actions/runs/<RUN_ID>`).

   ```
   scripts/kog/approve_release.sh stable X.Y.Z <RUN_ID>
   ```

   It downloads the CI manifest (artifact `manifest-X.Y.Z` of that run), **verifies the release again on your machine**
   (`verify_release.sh`: release is immutable, every asset has a valid attestation from `kog-release.yml` at
   `refs/tags/vX.Y.Z` and not from a self-hosted runner, all assets come from one commit, that commit is on `main`, hashes
   match, the version is newer than the one on `client.kog.tw`), rebuilds the manifest from that and compares it byte for
   byte with CI's, and prints a summary:

   ```
   channel:   stable
   version:   X.Y.Z
   sequence:  <n>
   expires:   <date>
   commit:    <40 hex>
   files:     <n> changed, <n> removed, clients: linux-x86_64, win64
   (rollout_percent / halted / min_version / notes_url if set, and the changes)
   Type the version to sign it with your hardware key:
   ```

   **What to check before you type the version:**
   - `commit` is the commit you tagged (`git rev-parse vX.Y.Z^{}`).
   - `version` is the one you meant; `channel` is `stable`.
   - `sequence` is one more than the current one (`curl -s https://client.kog.tw/stable/update.json`, field `sequence`).
   - `expires` is not more than 90 days away.
   - The number of changed and removed files fits what you changed. A surprise here is the signal to stop.
   - `halted` is absent. `rollout_percent` and `min_version` are what you intended.

   Type the version, then `Touch the security key.` (PIN, then touch). The script uploads signature B to the branch
   `approvals` of `kog-client-updates` as `approvals/stable/X.Y.Z/update.json.sigb` and prints
   `signature B uploaded: ...`. Anything but the exact version prints `not confirmed` and stops.
7. **Planned.** Back in the run, press **Review deployments**, tick `release-publish`, **Approve and deploy**. The `publish`
   job checks both signatures with the pinned keys and commits to the branch `published`; the commit message holds the
   manifest SHA-256, the source commit and the run URL. `scripts/kog/publish_signed.sh` is what writes it.
8. Verify what the servers serve. Anyone can run this, from a checkout:

   ```
   scripts/kog/verify_published.sh stable
   ```

   Expected: `signatures ok`, then `stable X.Y.Z, sequence <n>, valid until <date>`, then `<n> files match their hashes`.
   It reads both hosts (`client.kog.tw` and the raw GitHub fallback) and fails if they differ.

## Stop a release: `halt_release.sh`

If a published stable release is bad, stop clients from updating:

```
scripts/kog/halt_release.sh stable
```

Needs the backup key file back from your password manager at `~/kog-client-backup-signing-key.txt`, and your hardware key.
It fetches the current manifest from `client.kog.tw`, makes a copy with `halted: true`, a higher sequence and 7 days
of validity, prints `halting stable X.Y.Z, new sequence <n>`, asks you to type `HALT`, signs with the backup CI key
(signature A) and your hardware key (B, touch), and publishes straight to the `published` branch without CI. Delete
the backup key file again. Only stable can be halted this way. It does not work until the `published` branch and `client.kog.tw`
exist.

## Rollback

There is no script that withdraws a version, and a clean downgrade does not exist: `verify_release.sh` refuses a version that
is not higher than the published one, and releases and `v*` tags cannot be changed.
What there is:

1. `halt_release.sh stable` stops further updates at once.
2. Release the fix as the next version number (steps above). A later manifest without `halted` replaces the halted one.
3. On the client, if a new version fails to start twice, the client restores the previous version itself and remembers
   the bad one (`docs/UPDATE_CHANNEL.md`, "Fail-safe").

A staged rollout (`rollout_percent`) would limit the damage in advance; see the table at the top for why it cannot be set yet.

## Keys: rotation and loss

**Rotate a CI key (a and b pairs).** Each channel has two CI keys, a and b; clients trust both. The secret holds the active one.
Rotate only to a key that is already shipped in a client release. Steps from `docs/UPDATE_CHANNEL.md`: (1) switch `active` in
`org/update_keys.yaml` of `infra-github` to the other slot and apply; (2) run the workflow "Organisation" with
`replace=<channel>_<old slot>` (for example `stable_a`) so the retired slot gets a fresh key; (3) merge the pull request
"Update the update keys" that follows, and ship a client release with it; (4) only after that may the new slot be made
active at the next rotation. Never make a key active that no client release carries.

**Lost or broken hardware key, the other one works.** Sign with the spare: `KOG_OWNER_KEY=~/.config/kog-client-signing/owner_sk_2
scripts/kog/approve_release.sh ...` (also accepted by `halt_release.sh`). Then drop the lost key from `KOG_STABLE_OWNER_KEYS` in
a client release.

**Lost the handle files, still have the device.** `ssh-keygen -K` in an empty directory re-creates them from the device.

**Lost both hardware keys.** The design has no recovery for this beyond a new client with new owner keys. Stable clients that
are already installed would trust only the old keys, so they would have to be reinstalled by hand. This is my reading, not
a tested path.

**CI key stolen or lost.** Stable still needs your signature. Rotate as above. If both CI slots are gone, the backup
CI key plus the hardware key can still sign (this is what `halt_release.sh` uses), and a client release replaces the CI keys.

**Backup key lost.** `halt_release.sh` stops working. Replace it through a client release.

## Canary and dev: automatic

| Channel | Trigger | Version | Signature | Manifest valid | Environment |
| --- | --- | --- | --- | --- | --- |
| canary | every push to `main` (`KoG canary`) | `X.Y.Z-canary.<run number>`, tag `canary-<run number>` | CI key only | 30 days | `canary-signing` (no reviewer, `main` only) |
| dev | every push to `dev` (`KoG dev`) | `X.Y.Z-dev.<run number>` | CI key only | 14 days | `dev-signing` (no reviewer, `dev` only) |

Each push builds the client for Linux, Windows and macOS, attests the files, makes a GitHub prerelease, keeps the
last 10 of each channel, verifies the release, makes the manifest, and signs and publishes it in one job using the
secrets `SIGNING_KEY` and `UPDATES_DEPLOY_KEY` (a deploy key that can write to `kog-client-updates` only). Nothing to approve. If
a push is not the head of its branch any more when the run starts, the build is skipped. They are never built for forks
or pull requests. When no push comes within the validity, the manifest expires and clients stop accepting it.

Today this path fails at the signing step, as in the table at the top.

## Troubleshooting

| Message or symptom | Meaning and fix |
| --- | --- |
| `SIGNING_KEY is empty: the environment ... gave this job no secret of that name` | The environment secret has no value, or the job is not in the environment. Check `infra-github` (apply ran? the secret exists in `canary-signing`) and the branch policy of the environment. |
| `the commit is not the head of main any more` or `the commit is not on main` | `main` moved after you tagged. A tag cannot move: release the next version. |
| `KOG_CLIENT_VERSION is not X.Y.Z` | The version change was not merged before the tag. Release the next version. |
| `the check '<name>' has not passed on this commit` | A required check did not run or failed on the tagged commit. Fix on `main`, release the next version. |
| `release vX.Y.Z is not immutable` | The repository setting "immutable releases" is off. It is on today. |
| `gh attestation verify` fails | An asset was changed or not built by `kog-release.yml` from that tag. Do not sign. |
| `the manifest of CI differs from the one made here from the verified release` | CI's manifest is not what the verified files give. Do not sign; find out why. |
| `version X is not newer than the published Y` | The version does not exceed the published one. |
| `the sequence does not rise` | The manifest sequence is not higher than the published one. Do not publish the older one again. |
| `no hardware key handle at ...` | Run `gen_signing_key.sh`, or point `KOG_OWNER_KEY` at the handle file, or `ssh-keygen -K`. |
| `that is not the backup key the client trusts` | The file is not the backup key whose public half is in the header. Fetch the right file. |
| `the owner signature is not valid` | The key that signed is not in `KOG_STABLE_OWNER_KEYS`, or the key was not touched. |
| `ssh-keygen`: unknown key type `ed25519-sk` | Your OpenSSH has no security key support. Use a newer OpenSSH. |
| A release build fails about placeholders | A channel's header flag `KOG_UPDATE_KEYS_PLACEHOLDER_*` is still 1. |
| `the two hosts serve different manifests` (verify_published) | One host is stale or withheld. `client.kog.tw` is Pages from the `published` branch; wait, then compare. |

## Not verified

- I ran no script that signs, uploads or publishes, and did not make a tag or a release. The security key flow
  (`ssh-keygen -O resident -O verify-required`, the PIN prompt, `ssh-keygen -K`) was not run with a device.
  `docs/UPDATE_CHANNEL.md` says the same: signatures were checked with a software emulation, not a real key.
- Everything marked planned: `Sign release`, the approval screens of `release-signing` and `release-publish`, the `approvals`
  and `published` branches. The click path ("Review deployments", "Approve and deploy") is GitHub's standard environment
  review and was not tried here.
- The exact output of the scripts is read from their `echo` and `print` lines.
- Whether the stable placeholder flag in the header is cleared by the keys pull request or by hand.
- That canary and dev work once `SIGNING_KEY` has a value: the run stops at that step.
- The statements about OpenSSH on macOS, and recovery after losing both hardware keys, are my reading.
