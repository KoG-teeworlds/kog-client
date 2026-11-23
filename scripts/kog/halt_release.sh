#!/usr/bin/env bash
set -euo pipefail

# Emergency stop: a signed manifest with halted=true, signed here with the hardware key and the offline backup CI key,
# published straight to the updates repo without waiting for CI.
# usage: halt_release.sh CHANNEL     (stable only: the other channels are halted by a CI run of the workflow "Halt")
channel=${1:?channel}
[ "$channel" = stable ] || { echo "only the stable channel is halted from here" >&2; exit 2; }
dir="${KOG_SIGNING_DIR:-$HOME/.config/kog-client-signing}"
key=${KOG_OWNER_KEY:-$dir/owner_sk_1}
backup_file="${KOG_BACKUP_KEY_FILE:-$HOME/kog-client-backup-signing-key.txt}"
root=$(cd "$(dirname "$0")/../.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
[ -f "$key" ] || { echo "no hardware key handle at $key" >&2; exit 1; }
[ -f "$backup_file" ] || { echo "the offline backup key is not at $backup_file: fetch it from your password manager" >&2; exit 1; }

cc -O2 -o "$work/kogsign" "$root/scripts/kog/kogsign.c" "$root/src/engine/external/monocypher/monocypher.c" \
  "$root/src/engine/external/monocypher/monocypher-ed25519.c" -I"$root/src/engine/external/monocypher"
backup_pub=$("$work/kogsign" pubkey "$backup_file")
python3 "$root/scripts/kog/keys_from_header.py" backup | grep -qx "$backup_pub" || { echo "that is not the backup key the client trusts" >&2; exit 1; }

curl -fsS "https://client.kog.tw/$channel/update.json" -o "$work/previous.json"
python3 "$root/scripts/kog/make_manifest.py" halt --previous "$work/previous.json" --out "$work/update.json"
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print("halting %s %s, new sequence %d" % (m["channel"], m["version"], m["sequence"]))' "$work/update.json"
printf 'Type HALT to continue: '
read -r answer
[ "$answer" = HALT ] || { echo "not confirmed" >&2; exit 1; }

mkdir "$work/ci" "$work/owner" "$work/out"
cp "$work/update.json" "$work/ci/"
cp "$work/update.json" "$work/owner/"
"$work/kogsign" sign "$backup_file" "$work/ci/update.json" > "$work/out/update.json.sig"
echo "Touch the security key."
ssh-keygen -Y sign -n kog-client-release -f "$key" "$work/owner/update.json" >/dev/null
cp "$work/owner/update.json.sig" "$work/out/update.json.sigb"
cp "$work/update.json" "$work/out/update.json"
GH_TOKEN=${GH_TOKEN:-$(gh auth token)} bash "$root/scripts/kog/publish_signed.sh" "$channel" "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$work/update.json")" "$work/out" "Halt $channel"
