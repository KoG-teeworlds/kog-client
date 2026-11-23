#!/usr/bin/env bash
set -euo pipefail
set +x

# CI signature A for a channel without a second signature (canary, dev), then publication. The key is in SIGNING_KEY, the PEM from OpenTofu.
# usage: sign_and_publish.sh CHANNEL VERSION DIR     (DIR: update.json, source.txt)
channel=${1:?channel}
version=${2:?version}
dir=${3:?dir}
[ "$channel" != stable ] || { echo "stable needs the second signature: use the workflow Sign release" >&2; exit 2; }
root=$(cd "$(dirname "$0")/../.." && pwd -P)
umask 077
work=$(mktemp -d)
cleanup() {
  [ -f "$work/signing.key" ] && dd if=/dev/zero of="$work/signing.key" bs=1 count="$(wc -c < "$work/signing.key")" conv=notrunc 2>/dev/null || true
  rm -rf "$work"
}
trap cleanup EXIT

cc -O2 -o "$work/kogsign" "$root/scripts/kog/kogsign.c" "$root/src/engine/external/monocypher/monocypher.c" \
  "$root/src/engine/external/monocypher/monocypher-ed25519.c" -I"$root/src/engine/external/monocypher"
printf '%s' "$SIGNING_KEY" > "$work/signing.key"
"$work/kogsign" sign "$work/signing.key" "$dir/update.json" > "$dir/update.json.sig"
SOURCE_COMMIT=$(cat "$dir/source.txt") bash "$root/scripts/kog/publish_signed.sh" "$channel" "$version" "$dir" "Release $channel $version"
