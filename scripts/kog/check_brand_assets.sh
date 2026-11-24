#!/usr/bin/env bash
# The committed logos must be the unmodified files of the pinned brand_and_media commit.
set -u
cd "$(dirname "$0")/../../other/brand" || exit 1
fail=0
bad() { echo "brand assets: $1" >&2; fail=1; }

shasum -a 256 -c SHA256SUMS --quiet >/dev/null 2>&1 || bad "a file differs from SHA256SUMS (the logos must not be edited, run scripts/kog/sync_brand.sh)"
for f in logo/*; do
  grep -q "  $f\$" SHA256SUMS || bad "$f is not listed in SHA256SUMS"
done
grep -q "$(grep -oE '[0-9a-f]{40}' ../../scripts/kog/sync_brand.sh | head -1)" README.md || bad "README.md names another commit than sync_brand.sh"
grep -q "NonCommercial-NoDerivatives" LICENSE || bad "LICENSE is missing"
exit $fail
