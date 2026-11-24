# Homebrew tap for macOS

Status: design, nothing is built. Written on 2026-11-24. Client side only: the site copy is described here, the site and
the tap repository are made in their own repositories.

## 1. Goals and non-goals

Goals:

- macOS players can run `brew install --cask ...` and later `brew upgrade --cask ...`. Today the in-app updater offers
  nothing on macOS, because the signed manifest has no macOS client entry (docs/UPDATE_CHANNEL.md), so players download the
  disk image again by hand.
- Homebrew checks the SHA-256 of the disk image before it installs it.
- The tap is generated from the signed release, never written by hand.

Non-goals:

- It does not fix Gatekeeper. The app is not code-signed with a developer identity and not notarised (checked on the
  canary-12 disk image: ad-hoc, linker-signed signature, no team identifier). The first start is still blocked until the
  player allows it in System Settings. Homebrew has no way around this any more (section 4).
- It does not replace the download page, the manifest, the signatures or the attestation.
- Not for Linux and Windows.

## 2. What the build gives us

Checked on 2026-11-24 from the repository and the canary-12 release:

- `kog-package.yml` builds the macOS package on `macos-latest` without `CMAKE_OSX_ARCHITECTURES`, so the disk image is
  for one architecture. The binary in `KoG-Client-macOS.dmg` of canary-12 is `arm64` (checked with `lipo`). It is not a
  universal image and there is no Intel build.
- The disk image holds `KoG-Client.app` and an `Applications` link. The bundle id is `tw.kog.client`.
- The asset is `KoG-Client-macOS.dmg` at
  `https://github.com/KoG-teeworlds/kog-client/releases/download/<tag>/KoG-Client-macOS.dmg`, with tags `canary-N`, `dev-N` and later `vX.Y.Z`.
- The signed manifest lists the image in `packages[]` with `name`, `sha256`, `size` and `url`. The `sha256` of canary-12 in
  the published manifest equals the `shasum -a 256` of the file downloaded from that URL.
- The data folder is `~/Library/Application Support/KoG-Client` (`KOG_STORAGE_NAME`, `src/base/fs.cpp`).

Architecture decision: the cask says `depends_on arch: :arm64` and nothing about Intel. One cask, one image. Intel Macs get
a clear refusal from Homebrew instead of a binary that does not start. If an Intel or universal build is added later, the
manifest needs a second package name (for example `KoG-Client-macOS-x86_64.dmg`) and the cask gets one `sha256` per
architecture. Not needed now.

## 3. Tap layout

Repository `KoG-teeworlds/homebrew-kog` (the `homebrew-` prefix gives the short tap name, as "How to Create and Maintain a
Tap" recommends). Public, no formulae, only:

```
Casks/kog-client@canary.rb     now
Casks/kog-client@dev.rb        optional, same generator
Casks/kog-client.rb            stable, when a stable release exists
README.md                      what it is, the Gatekeeper note, the commands
.github/workflows/             the checks in section 4
```

Install name: `brew install --cask kog-teeworlds/kog/kog-client@canary`. Homebrew writes tap names in lower case. The `@`
suffix follows the Cask Cookbook rule for casks that exist in several versions or channels; Acceptable Casks names `@beta`
and `@nightly` as examples of channel tokens.

Canary cask as the generator writes it (values from the manifest, here canary-12):

```ruby
cask "kog-client@canary" do
  version "0.1.0-canary.12"
  sha256 "b87f160e85d333d285cf6d31dfc27b1ff10b2b36203380ced1276b187a23085d"

  url "https://github.com/KoG-teeworlds/kog-client/releases/download/canary-12/KoG-Client-macOS.dmg"
  name "KoG Client"
  desc "Unstable canary build of the KoG Teeworlds client"
  homepage "https://client.kog.tw/"

  conflicts_with cask: "kog-client@dev"
  depends_on arch: :arm64
  depends_on macos: ">= :big_sur"

  app "KoG-Client.app"

  zap trash: "~/Library/Application Support/KoG-Client"

  caveats <<~EOS
    This is an unstable build and it is not notarised by Apple. macOS blocks the first start:
    open System Settings > Privacy & Security and choose "Open Anyway" for KoG-Client.
    Settings stay in ~/Library/Application Support/KoG-Client; "brew uninstall --zap" deletes them.
  EOS
end
```

Notes:

- `livecheck`: none. The Cookbook says to omit it when no strategy is needed. We do not poll for versions; the release
  workflow writes the new version itself, so the tap has no second version source that could disagree with the manifest. If
  `brew audit --online` insists, add `livecheck do skip "Updated by the release workflow" end` (the `skip` form is from the
  Cookbook as I remember it; not tested here).
- `depends_on macos`: Apple Silicon needs macOS 11 (Big Sur) at least. CMake sets the deployment target 10.15 and the bundle
  has no `LSMinimumSystemVersion`, so the real minimum is stated nowhere. Open question 1.
- `zap` never runs on a plain `brew uninstall`. The Cookbook: "The `zap` procedures will never be performed by default, but
  only if the user uses `--zap` on `uninstall`."
- `caveats` is shown on install and `brew info`; the Cookbook asks for sparing use, for install matters.
- No `auto_updates`, so `brew upgrade` upgrades it. The version string changes with every canary build, so each build is an
  upgrade.
- Old canary releases are deleted after the last 10. A cask only points at the newest one, so this does not matter.

## 4. How the cask is generated

One script, `scripts/kog/make_cask.py`, with a test (`scripts/kog/test_make_cask.py`, run by `kog-checks.yml`). A template
is kept next to it. No value is typed twice.

1. Input: the signed `update.json` and its signature file(s) of a channel, the files the sign job already has.
2. It verifies the signature again with the keys in `kog_update_pubkey.h` (the `kogsign verify` and `keys_from_header.py`
   calls of `publish_signed.sh`; stable also needs the owner signature). A manifest that does not verify gives no cask.
3. It takes `version`, and from `packages[]` the entry named `KoG-Client-macOS.dmg`: `url` and `sha256`. It refuses a URL
   that is not `https://github.com/KoG-teeworlds/kog-client/releases/download/<tag>/KoG-Client-macOS.dmg`, a channel that
   does not match the cask, a `halted` manifest, and a version that is not higher than the cask in the tap.
4. It writes `Casks/kog-client@<channel>.rb`. The output is deterministic: the test compares it with a stored cask byte for
   byte and checks that a changed hash, URL or channel fails.

Where it runs: a job `tap` in `kog-canary.yml` and `kog-dev.yml`, `needs: sign`, so only after signing, verification
(`verify_release.sh`: attestation, immutability, hashes) and publication. For stable the signing is in `kog-client-updates`;
see open question 3.

How the tap changes (decision): the job pushes a branch `bump/canary-<N>` to the tap and the tap's own workflow does the
rest. The client repository opens no pull request.

- Credential: a deploy key with write access to the tap only, as `UPDATES_DEPLOY_KEY` already works for the updates
  repository. OpenTofu makes it; the private half is the secret `TAP_DEPLOY_KEY` of a new environment `tap-publish` in
  kog-client, restricted to `main`. Nothing new for a person to hold, no new GitHub App, no personal token. (The existing
  Auto-merge App of infra-github `docs/OWNER-GITHUB-APPS.md` is not used: its key would also give write access to
  kog-client itself.)
- The tap workflow `verify.yml` runs on a push to `bump/**` and on pull requests, on `macos-latest`:
  1. fetches `https://client.kog.tw/<channel>/update.json` and the signatures, verifies them with `kogsign` built from a
     pinned kog-client commit, and requires that the cask's `version`, `url` and `sha256` equal the manifest's entry;
  2. downloads the disk image from the cask URL and compares its SHA-256 itself, not trusting Homebrew's check alone;
  3. runs `brew style`, `brew audit --cask --strict --online`, `brew install --cask` on the tapped cask, checks that
     `KoG-Client.app` exists, then `brew uninstall --cask`;
  4. if all pass and the change touches only that cask file, fast-forwards `main` to the branch.
- Branch rules of the tap (made in infra-github): only the deploy key and that workflow push `bump/**`; `main` takes only
  the fast-forward of step 4, no force push, no deletion. A person can still fix the tap through a pull request. Whether a
  ruleset on a public repository on the Free plan can allow that is open question 2.
- Commits are made by the workflow's service name, not by a person, and are not signed with a person's key. The proof is
  the signed manifest, not the commit signature. The tap README says so.

What was checked of Homebrew (2026-11-24; docs.brew.sh pages fetched today, and the local Homebrew 7.0.16 source):

- Casks go in `Casks/` at the top of the tap; `brew tap-new` makes the layout (How to Create and Maintain a Tap).
- Homebrew 6.0.0 (11 June 2026) made non-official taps need explicit trust. docs.brew.sh/Tap-Trust: "Non-official taps
  require explicit trust by default since Homebrew 6.0.0". Installing `user/repository/cask` with `--cask` trusts only that
  cask; a short name needs `brew trust --cask` first. A vendor install guide found by search says a fully qualified name was
  still refused, so the site copy includes `--cask` and the tap README shows `brew trust --cask`. Not tested with a real tap.
- `--no-quarantine` is not in `brew install --help` of 7.0.16. Acceptable Casks says apps Gatekeeper can assess "must pass
  Homebrew's Gatekeeper checks and must not require System Integrity Protection or Gatekeeper to be disabled or bypassed".
  Search results say the flag was removed with Homebrew 5.0.0 and that failing casks of the official tap would be disabled by
  September 2026; I could not confirm that this happened. That rule is for `homebrew/cask`, not for our tap.
- In 7.0.16 `audit_signing` (cask/audit.rb) returns early for a non-official tap unless a signing check is asked for, so
  `brew audit --cask --strict --online` should not fail for missing notarisation. Read in the source, not run on our cask.
- Homebrew still quarantines the download and the app is not notarised, so the first start is blocked. We do not add a
  `postflight` that removes the quarantine attribute: it would hide Apple's warning from the player.

Not verified: `brew audit` on our cask (token with `@`, `skip` livecheck), `brew install` from a real tap, the first start of
the installed app, the Homebrew page on Gatekeeper checks (HTTP 503 when fetched), and what actually happened to unsigned
casks in September 2026.

## 5. Trust model

What the tap adds:

- an install and upgrade path on macOS, and a SHA-256 check by Homebrew on every download;
- a check of cask against signed manifest in the tap CI (section 4).

What it does not add:

- No signature of its own. Homebrew checks only the hash written in the cask; it does not know our signing keys. The signed
  manifest, the attestation (`gh attestation verify`) and the immutable release stay the root of trust, checked before the
  cask exists. The in-app updater checks our signatures on every update; a Homebrew install does not.
- No notarisation and no developer identity. Gatekeeper behaves as for the disk image.

A compromised tap repository or deploy key can publish a cask with any URL and hash, and every player who runs `brew upgrade`
gets it. That is the strength of a compromised download page for a player who downloads by hand. Limits: the key reaches the
tap only; the tap CI compares every change with the signed manifest; a player who trusted one cask (`brew trust --cask`) is
not exposed to other files in the tap. The tap README tells careful players to compare the disk image with the SHA-256 on
the download page.

Rule: the tap never carries a hash, URL or version that is not in the signed manifest of that channel. The generator refuses
otherwise, the tap CI refuses otherwise, and a cask edited by hand fails the same CI.

## 6. The in-app updater and "What changed"

- On macOS the updater offers nothing today, because the manifest has no macOS client entry (docs/UPDATE_CHANNEL.md). A
  Homebrew install does not fight the updater. The first version of the tap needs no client change.
- If a macOS client entry is added to the manifest later, the client must not replace an app that Homebrew manages. That is
  detectable: `<prefix>/Caskroom/kog-client@<channel>` exists (`/opt/homebrew` or `/usr/local`). The updater should then say
  "Updated by Homebrew: brew upgrade --cask kog-client@canary" and download nothing. Recommendation: write the check when the
  macOS entry is added, not before.
- The data folder is outside the app, so `brew upgrade` and `brew uninstall` keep settings, the device key and
  `kog_changes_seen`. The "What changed" page (docs/WHATS-CHANGED.md in PR #22) appears after a brew upgrade as after a
  download. Only `brew uninstall --zap` removes the folder; the player then counts as a fresh install.
- `brew upgrade` replaces the whole app, so the `.kogprev` copy of the fail-safe is gone afterwards. The settings backup made
  on a version change is in the data folder and stays.

## 7. Site copy

The macOS tab of the download page shows, in this order:

1. A copy block: `brew install --cask kog-teeworlds/kog/kog-client@canary`, and below it `brew upgrade --cask
   kog-client@canary` to update. The canary page shows canary, the dev page dev, later stable the plain name.
2. The disk image as the second option, with its SHA-256.
3. A note: "The app is not notarised by Apple, so the first start is blocked: open System Settings, Privacy & Security, and
   choose Open Anyway. This is the same for both ways to install. Apple Silicon Macs only."

The Homebrew block is hidden while the switch in section 8 is off. The commands never use `gh`.

## 8. Kill switch, rollback, stable

Switch: repository variable `KOG_TAP_MODE` of kog-client with the values `off`, `dry-run`, `on`. Anything else counts as
`off`; unset counts as `dry-run` (the convention of `AUTOMERGE_MODE`).

- `off`: the `tap` job is skipped. The tap keeps its last cask, installed apps are untouched, nothing else changes.
- `dry-run`: the job makes the cask, runs the unit checks and uploads the cask as a workflow artifact. It pushes nothing.
- `on`: the job pushes the branch.

Default: `dry-run` until one cask has passed the tap CI, then `on` for canary and `dry-run` for dev. The owner sets it with
`gh variable set KOG_TAP_MODE --body off --repo KoG-teeworlds/kog-client`; this is an owner command and never appears on the
public pages. The site follows the same value (open question 6).

Rollback: revert the commit in the tap (a new fast-forward commit, no force push). Players who ran `brew upgrade` in between
keep the app they have. A bad build is also stopped at the source: a halted manifest makes the generator refuse, and the
next good canary replaces the cask. To pull a cask completely, delete the file or mark it `disable!` with a reason; an
installed app keeps working.

At the stable launch: add `Casks/kog-client.rb` from the stable manifest (both signatures needed, so the generator runs after
the publish step of *Sign release*). The stable page shows `brew install --cask kog-teeworlds/kog/kog-client`. The canary cask
stays. A player who moves from canary to stable uninstalls one and installs the other (`conflicts_with` says so).

## 9. Rollout

1. infra-github: create `KoG-teeworlds/homebrew-kog` (public, issues on), its ruleset (section 4), the deploy key, and the
   environment secret `TAP_DEPLOY_KEY` in kog-client. `KOG_TAP_MODE` stays unset.
2. kog-client: `make_cask.py`, its template and test, in `kog-checks.yml`. Dry run against the published canary manifest.
3. kog-client: the `tap` job in `kog-canary.yml` (and `kog-dev.yml`) behind the switch; tap repository: `verify.yml` and
   README. Switch `dry-run`, then `on` after one good run.
4. Site: the macOS tab (section 7).
5. Stable cask when the first stable release is signed.

Each step starts the next by itself. The only human acts are merging these pull requests and setting the switch.

## 10. Open questions

1. Minimum macOS version. Recommendation: read the real value from a build (`otool -l`, `LC_BUILD_VERSION`), put it in the
   cask and in `Info.plist` (`LSMinimumSystemVersion`); until then `>= :big_sur`.
2. Can a ruleset on a public repository on the Free plan let only the workflow fast-forward `main`? Recommendation: test it
   in infra-github. If not, the workflow opens a pull request with a GitHub App token limited to the tap, merged by the
   existing Auto-merge workflow.
3. Stable trigger. Recommendation: the publish job of `kog-client-updates` runs the generator after publishing, so a cask is
   only made from a manifest that has both signatures.
4. A dev cask as well? Recommendation: yes, same generator, one more file, not shown on the site until asked for.
5. Intel Macs. Recommendation: not now; add an Intel or universal build only if players ask.
6. Hide the Homebrew block on the site when the switch is off? Recommendation: yes, derived from the same variable (a small
   file in the published branch written by the tap job), not a second setting.
7. The Homebrew trust prompt. Recommendation: test once on a clean account before the site copy goes live and copy the exact
   wording into the tap README.
8. Submit to the official `homebrew/cask`? Recommendation: no. It wants signed and notarised apps and stable versions.
