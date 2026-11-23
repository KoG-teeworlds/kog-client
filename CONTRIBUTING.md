# Contributing

## Branches

- `master` mirrors TClient's `master`. Never commit KoG changes to it; update it only by fast-forwarding to
  `upstream/master`. This keeps GitHub's "Sync fork" working.
- `main` is the KoG client and the default branch. Open pull requests against it.

## Upstream

`scripts/kog/sync_upstream.sh REF` fast-forwards `master`, then merges the given upstream tag or branch into a new
`sync/REF` branch off `main` and lists the conflicts. GitHub cannot notify us of another repository's releases, so a
maintainer runs it when TClient publishes a version. Keep KoG code in new files (`kog_*`) and hooks in shared files
small, so merges stay cheap.

## Changes

- Build and test as described in [docs/BUILDING.md](docs/BUILDING.md); run `cmake --build . --target run_tests`.
- Format with `scripts/fix_style.py`.
- Comment only where the reason is not obvious.
- One short subject line per commit, in the imperative.
- New behaviour towards a server or another party is off by default.

## Updates

The update channel is described in [docs/UPDATE_CHANNEL.md](docs/UPDATE_CHANNEL.md). Release signing keys are never
stored in this repository or in GitHub secrets.
