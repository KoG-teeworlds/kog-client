#!/usr/bin/env bash
set -euo pipefail

# Everything that must be true of a draft release before anyone signs it. Needs gh, logged in, and GH_TOKEN or a login.
# usage: verify_release.sh CHANNEL TAG OUTDIR      writes OUTDIR/update.draft.json and OUTDIR/source.txt
channel=${1:?channel}
tag=${2:?tag}
out=${3:?outdir}
repo=${KOG_REPO:-KoG-teeworlds/kog-client}
root=$(cd "$(dirname "$0")/../.." && pwd -P)
case "$channel" in
  stable) workflow=kog-release.yml branch=main ;;
  canary) workflow=kog-channel.yml branch=main ;;
  dev) workflow=kog-channel.yml branch=dev ;;
  *) echo "unknown channel" >&2; exit 2 ;;
esac
source_ref="refs/heads/$branch"
[ "$channel" != stable ] || source_ref="refs/tags/$tag"
mkdir -p "$out/assets"
tool="python3 $root/scripts/kog/make_manifest.py"

immutable=$(gh api "repos/$repo/releases/tags/$tag" --jq '.immutable')
[ "$immutable" = true ] || { echo "release $tag is not immutable" >&2; exit 1; }

gh release download "$tag" -R "$repo" -p update.draft.json -D "$out" --clobber
python3 - "$out/update.draft.json" "$channel" "$repo" "$tag" > "$out/asset_names.txt" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
if m["channel"] != sys.argv[2]:
    sys.exit("the draft is of channel %s" % m["channel"])
urls = [f["url"] for f in m["files"]] + [p["client"]["url"] for p in m["platforms"].values()] + [p["url"] for p in m.get("packages", [])]
prefix = "https://github.com/%s/releases/download/%s/" % (sys.argv[3], sys.argv[4])
for url in urls:
    if url.startswith(prefix):
        print(url[len(prefix):])
PY

verify() {
  gh attestation verify "$1" --repo "$repo" --signer-workflow "$repo/.github/workflows/$workflow" \
    --source-ref "$source_ref" --deny-self-hosted-runners --format json
}
sources=""
for name in update.draft.json $(sort -u "$out/asset_names.txt"); do
  if [ "$name" != update.draft.json ]; then
    gh release download "$tag" -R "$repo" -p "$name" -D "$out/assets" --clobber
    file="$out/assets/$name"
  else
    file="$out/update.draft.json"
  fi
  sources+="$(verify "$file" | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["verificationResult"]["signature"]["certificate"]["sourceRepositoryDigest"])')"$'\n'
done
source=$(printf '%s' "$sources" | sort -u)
[ "$(printf '%s\n' "$source" | wc -l)" = 1 ] || { echo "the assets were not built from one commit" >&2; exit 1; }
echo "$source" > "$out/source.txt"

status=$(gh api "repos/$repo/compare/$branch...$source" --jq .status)
case "$status" in behind | identical) ;; *) echo "commit $source is not on $branch" >&2; exit 1 ;; esac

$tool check --manifest "$out/update.draft.json" --assets "$out/assets"
while read -r name; do
  [ -f "$out/assets/$name" ] || { echo "missing asset $name" >&2; exit 1; }
done < <(sort -u "$out/asset_names.txt")
rm -f "$out/previous.json"
rc=0
bash "$root/scripts/kog/published_file.sh" "$channel/update.json" "$out/previous.json" || rc=$?
[ "$rc" = 0 ] || [ "$rc" = 3 ] || exit 1
$tool newer --manifest "$out/update.draft.json" --previous "$out/previous.json"
echo "release $tag verified: channel $channel, commit $source"
