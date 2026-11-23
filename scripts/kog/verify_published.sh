#!/usr/bin/env bash
set -euo pipefail

# Anyone can run this: checks what the update servers serve for a channel, the way the client does, from the keys in the repository.
# usage: verify_published.sh [CHANNEL]      needs curl, python3, ssh-keygen, cc; gh for the attestation check (optional)
channel=${1:-stable}
root=$(cd "$(dirname "$0")/../.." && pwd -P)
hosts=(https://client.kog.tw https://kog-teeworlds.github.io/kog-client-updates)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$root"
cc -O2 -o "$work/kogsign" scripts/kog/kogsign.c src/engine/external/monocypher/monocypher.c src/engine/external/monocypher/monocypher-ed25519.c \
  -Isrc/engine/external/monocypher

for i in 0 1; do
  mkdir "$work/$i"
  curl -fsS "${hosts[$i]}/$channel/update.json" -o "$work/$i/update.json"
  curl -fsS "${hosts[$i]}/$channel/update.json.sig" -o "$work/$i/update.json.sig"
  [ "$channel" != stable ] || curl -fsS "${hosts[$i]}/$channel/update.json.sigb" -o "$work/$i/update.json.sigb"
done
cmp "$work/0/update.json" "$work/1/update.json" || { echo "the two hosts serve different manifests" >&2; exit 1; }
dir="$work/0"

ok=0
while read -r pub; do
  "$work/kogsign" verify "$pub" "$dir/update.json" "$dir/update.json.sig" >/dev/null 2>&1 && ok=1
done < <({ python3 scripts/kog/keys_from_header.py ci "$channel"; [ "$channel" = stable ] && python3 scripts/kog/keys_from_header.py backup; } || true)
[ "$ok" = 1 ] || { echo "FAIL: no CI key of the $channel channel verifies update.json.sig" >&2; exit 1; }
if [ "$channel" = stable ]; then
  python3 scripts/kog/keys_from_header.py allowed-signers > "$work/allowed_signers"
  ssh-keygen -Y verify -f "$work/allowed_signers" -I owner -n kog-client-release -s "$dir/update.json.sigb" < "$dir/update.json" >/dev/null \
    || { echo "FAIL: the owner signature does not verify" >&2; exit 1; }
fi
echo "signatures ok"

python3 - "$dir/update.json" "$channel" <<'PY'
import json, sys, time
m = json.load(open(sys.argv[1]))
assert m["channel"] == sys.argv[2], "channel in the manifest is %s" % m["channel"]
assert m["expires"] > time.time(), "the manifest has expired"
print("%s %s, sequence %d, valid until %s%s" % (m["channel"], m["version"], m["sequence"], time.strftime("%Y-%m-%d", time.gmtime(m["expires"])), ", HALTED" if m.get("halted") else ""))
entries = [(f["url"], f["sha256"]) for f in m["files"]] + [(p["client"]["url"], p["client"]["sha256"]) for p in m["platforms"].values()]
with open(sys.argv[1] + ".entries", "w") as out:
    for url, sha in entries:
        out.write("%s %s\n" % (url, sha))
PY
count=0
while read -r url sha; do
  got=$(curl -fsSL "$url" | { sha256sum 2>/dev/null || shasum -a 256; } | cut -d' ' -f1)
  [ "$got" = "$sha" ] || { echo "FAIL: $url has the hash $got, the manifest says $sha" >&2; exit 1; }
  count=$((count + 1))
done < "$dir/update.json.entries"
echo "$count files match their hashes"
