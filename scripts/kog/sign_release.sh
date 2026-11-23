#!/usr/bin/env bash
set -euo pipefail

usage() { echo "usage: $0 VERSION [--key FILE] [--days N] [--yes] [--dry-run] [--offline DIR]" >&2; exit 2; }
[ $# -ge 1 ] || usage
version=$1
shift

key="${KOG_SIGNING_DIR:-$HOME/.config/kog-client-signing}/primary.key"
days=30
yes=0
dry=0
offline=""
while [ $# -gt 0 ]; do
  case "$1" in
    --key) key=$2; shift 2 ;;
    --days) days=$2; shift 2 ;;
    --yes) yes=1; shift ;;
    --dry-run) dry=1; shift ;;
    --offline) offline=$2; shift 2 ;;
    *) usage ;;
  esac
done

root=$(cd "$(dirname "$0")/../.." && pwd -P)
repo=KoG-teeworlds/kog-client
updates_repo=KoG-teeworlds/kog-client-updates
tag="v$version"
tool="python3 $root/scripts/kog/make_manifest.py"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

[ -f "$key" ] || { echo "no signing key at $key: run scripts/kog/gen_signing_key.sh" >&2; exit 1; }
cc -O2 -o "$work/kogsign" "$root/scripts/kog/kogsign.c" \
  "$root/src/engine/external/monocypher/monocypher.c" "$root/src/engine/external/monocypher/monocypher-ed25519.c" \
  -I"$root/src/engine/external/monocypher"

assets="$work/assets"
mkdir -p "$assets"
if [ -n "$offline" ]; then
  cp "$offline/update.draft.json" "$work/draft.json"
  cp -R "$offline/assets/." "$assets/" 2>/dev/null || true
  previous=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["sequence"])' "$offline/previous.json" 2>/dev/null || echo 0)
else
  command -v gh >/dev/null || { echo "the gh CLI is required" >&2; exit 1; }
  gh release download "$tag" -R "$repo" -p update.draft.json -D "$work" --clobber
  mv "$work/update.draft.json" "$work/draft.json"
  python3 - "$work/draft.json" > "$work/client_assets.txt" <<'PY'
import json, sys
for platform in json.load(open(sys.argv[1]))["platforms"].values():
    print(platform["client"]["url"].rsplit("/", 1)[1])
PY
  while read -r name; do
    gh release download "$tag" -R "$repo" -p "$name" -D "$assets" --clobber
    gh attestation verify "$assets/$name" -R "$repo" >/dev/null
  done < "$work/client_assets.txt"
  previous=$(curl -fsS https://client.kog.tw/update.json | python3 -c 'import json,sys;print(json.load(sys.stdin)["sequence"])' 2>/dev/null || echo 0)
fi

$tool check --manifest "$work/draft.json" --assets "$assets"
sequence=$((previous + 1))
$tool finalize --draft "$work/draft.json" --sequence "$sequence" --days "$days" --out "$work/update.json"
"$work/kogsign" sign "$key" "$work/update.json" > "$work/update.json.sig"
"$work/kogsign" verify "$("$work/kogsign" pubkey "$key")" "$work/update.json" "$work/update.json.sig" >/dev/null

python3 - "$work/update.json" <<'PY'
import json, sys, time
m = json.load(open(sys.argv[1]))
print("version %s, channel %s, sequence %d, valid until %s" % (m["version"], m["channel"], m["sequence"], time.strftime("%Y-%m-%d", time.gmtime(m["expires"]))))
print("%d changed files, %d removals, platforms: %s" % (len(m["files"]), len(m["remove"]), ", ".join(sorted(m["platforms"]))))
for f in m["files"][:20]:
    print("  " + f["path"])
if len(m["files"]) > 20:
    print("  ... and %d more" % (len(m["files"]) - 20))
PY

if [ -n "$offline" ]; then
  mkdir -p "$offline/out"
  cp "$work/update.json" "$work/update.json.sig" "$offline/out/"
  exit 0
fi
if [ "$dry" = 1 ]; then
  mkdir -p out
  cp "$work/update.json" "$work/update.json.sig" out/
  echo "dry run: wrote out/update.json and out/update.json.sig"
  exit 0
fi
if [ "$yes" != 1 ]; then
  read -r -p "Publish $tag? [y/N] " answer
  [ "$answer" = y ] || { echo "not published"; exit 1; }
fi

gh repo clone "$updates_repo" "$work/updates" -- --quiet --branch published
cp "$work/update.json" "$work/update.json.sig" "$work/updates/"
mkdir -p "$work/updates/releases/$version"
cp "$work/update.json" "$work/update.json.sig" "$work/updates/releases/$version/"
git -C "$work/updates" add -A
git -C "$work/updates" commit -q -m "Release $version"
git -C "$work/updates" push -q
echo "published $tag"
