#!/usr/bin/env bash
set -euo pipefail

# One file of the `published` branch of the updates repo, read through the GitHub API: that branch is the truth, the website
# is only a copy of it. usage: published_file.sh PATH OUT
# exit 0: written to OUT; exit 3: the file does not exist (a first publish); exit 1: it could not be read, which is never "no file"
path=${1:?path}
out=${2:?out}
repo=${KOG_UPDATES_REPO:-KoG-teeworlds/kog-client-updates}
api=${KOG_API_URL:-https://api.github.com}
args=(-sS -o "$out" -w '%{http_code}' -H "Accept: application/vnd.github.raw+json")
[ -z "${GH_TOKEN:-}" ] || args+=(-H "Authorization: Bearer $GH_TOKEN")
code=$(curl "${args[@]}" "$api/repos/$repo/contents/$path?ref=published" 2>/dev/null) || code=000
case "$code" in
  200) exit 0 ;;
  404) rm -f "$out"; exit 3 ;;
  *) rm -f "$out"; echo "the published branch could not be read: HTTP $code for $path" >&2; exit 1 ;;
esac
