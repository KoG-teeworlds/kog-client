# Workflows

Our workflows are the `kog-*.yml` files. The upstream T-Client/DDNet workflows stay in the tree untouched so upstream merges do not conflict, and are disabled in the repository settings. Do not enable them: most use unpinned actions.

| Workflow | Trigger | What it does |
| --- | --- | --- |
| `kog-build.yml` | pull request, push to `main` | The client only, on Ubuntu 22.04 (older glibc for the release binary), macOS and Windows: debug and release builds, unit tests that do not need the server, packaged artifacts (kept 14 days) |
| `kog-codeql.yml` | pull request, push to `main`, manual | CodeQL for C++ and Python |
| `kog-spelling.yml` | pull request, push to `main` | typos |
| `kog-checks.yml` | pull request, push to `main` | Release chain checks: manifest, signing, verifier |
| `kog-release.yml` | manual | Builds release files and a draft manifest. It never signs: signing runs in the protected `release-signing` environment of `kog-client-updates` |

How the release workflows are used, and what is not in place yet: [docs/RELEASING.md](../docs/RELEASING.md).

The server is not built here. Use upstream DDNet or the KoG server repository for servers.

Rules for every workflow here: `permissions: {}` at the top and minimal grants per job, `persist-credentials: false` on checkout, a `timeout-minutes` on every job, `cancel-in-progress` for pull requests, no `pull_request_target`, and every `uses:` pinned to a full commit SHA with the version in a comment.

## Allowed actions

The repository allows GitHub-owned actions and this pattern, and requires SHA pinning:

- `crate-ci/typos@*`

## Updating a pinned action

Dependabot opens one grouped pull request a week for the `github-actions` ecosystem. To bump by hand, resolve the tag to its commit (`gh api repos/<owner>/<repo>/git/ref/tags/<tag>`, and dereference an annotated tag with `git/tags/<sha>`), replace the SHA and the version comment, and run `actionlint` and `zizmor`.

## Not enabled yet

Upstream's Android, iOS and Emscripten builds, clang-tidy, the sanitizers, style checks and the Rust checks. The release has no Android package until an Android build is added. Each needs its own pinned copy; none should be switched on as is.
