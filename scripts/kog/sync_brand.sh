#!/usr/bin/env bash
# Fetch the official KoG logos from brand_and_media at a pinned commit into other/brand/ and verify them.
set -euo pipefail
cd "$(dirname "$0")/../.."

COMMIT=700f64d29bca1413a0c1fbab435852a5cc2e47c7
BASE="https://raw.githubusercontent.com/KoG-teeworlds/brand_and_media/$COMMIT"
DEST=other/brand
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

while read -r sum path; do
  remote=${path#logo/}
  [ "$path" = "$remote" ] && remote=$path || remote=brand/logo/$remote
  mkdir -p "$tmp/$(dirname "$path")"
  curl -fsSL "$BASE/$remote" -o "$tmp/$path"
  got=$(shasum -a 256 "$tmp/$path" | cut -d' ' -f1)
  [ "$got" = "$sum" ] || { echo "sync_brand: $path does not match the pinned hash" >&2; exit 1; }
done < "$DEST/SHA256SUMS"

while read -r _ path; do
  mkdir -p "$DEST/$(dirname "$path")"
  cp "$tmp/$path" "$DEST/$path"
done < "$DEST/SHA256SUMS"
echo "brand assets match brand_and_media@$COMMIT"
