#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tool="python3 $root/scripts/kog/make_manifest.py"

cc -O2 -o "$work/kogsign" "$root/scripts/kog/kogsign.c" \
  "$root/src/engine/external/monocypher/monocypher.c" "$root/src/engine/external/monocypher/monocypher-ed25519.c" \
  -I"$root/src/engine/external/monocypher"

tree="$work/tree"
mkdir -p "$tree/data/sub"
echo one > "$tree/data/a.txt"
echo two > "$tree/data/sub/b.txt"
echo evil > "$tree/data/bad.dll"
echo binary1 > "$work/client1"
$tool draft --init-base "$work/base.json" --version 0.1.0 --tag v0.1.0 --data "$tree" --client linux-x86_64="$work/client1" --out "$work/draft1.json" --uploads "$work/uploads1.json" 2>/dev/null
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); assert m["files"]==[] and m["remove"]==[], m' "$work/draft1.json"

echo changed > "$tree/data/a.txt"
echo three > "$tree/data/c.txt"
rm "$tree/data/sub/b.txt"
echo binary2 > "$work/client2"
$tool draft --base "$work/base.json" --version 0.2.0 --tag v0.2.0 --data "$tree" --client linux-x86_64="$work/client2" --out "$work/draft2.json" --uploads "$work/uploads2.json" 2>/dev/null
python3 - "$work/draft2.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
assert [f["path"] for f in m["files"]] == ["data/a.txt", "data/c.txt"], m["files"]
assert m["remove"] == ["data/sub/b.txt"], m["remove"]
assert m["sequence"] == 0
PY

mkdir -p "$work/keys"
cp "$root/src/engine/client/kog_update_pubkey.h" "$work/pubkey.h"
KOG_PUBKEY_HEADER="$work/pubkey.h" KOG_BACKUP_KEY_FILE="$work/keys/backup.key" bash "$root/scripts/kog/gen_signing_key.sh" --local-only --updates-dir="$work/updates" > "$work/keygen.out"
grep -q "KOG_UPDATE_PUBKEY_PLACEHOLDER 0" "$work/pubkey.h"
test -f "$work/updates/keys/primary.pub" && test "$(stat -c %a "$work/keys/backup.key" 2>/dev/null || stat -f %Lp "$work/keys/backup.key")" = 600
if grep -q "$(sed -n 1p "$work/keys/backup.key")" "$work/keygen.out"; then
  echo "FAIL: the private key was printed" >&2
  exit 1
fi
"$work/kogsign" keygen "$work/keys/primary.key" > /dev/null
mkdir -p "$work/offline/assets"
cp "$work/draft2.json" "$work/offline/update.draft.json"
cp "$work/client2" "$work/offline/assets/client-linux-x86_64-0.2.0"
echo '{"sequence": 3}' > "$work/offline/previous.json"
bash "$root/scripts/kog/sign_release.sh" 0.2.0 --offline "$work/offline" --key "$work/keys/primary.key" > "$work/sign.out"
out="$work/offline/out"
pub=$("$work/kogsign" pubkey "$work/keys/primary.key")
echo "$pub" > "$out/pubkey.hex"
python3 -c 'import json,sys; assert json.load(open(sys.argv[1]))["sequence"]==4' "$out/update.json"
"$work/kogsign" verify "$pub" "$out/update.json" "$out/update.json.sig" >/dev/null

if command -v openssl >/dev/null; then
  python3 - "$pub" "$out/update.json.sig" "$work/pub.der" "$work/sig.bin" <<'PY'
import sys
pub, sigfile, der, sig = sys.argv[1:5]
open(der, "wb").write(bytes.fromhex("302a300506032b6570032100" + pub))
open(sig, "wb").write(bytes.fromhex(open(sigfile).read().split()[2]))
PY
  openssl pkey -pubin -inform DER -in "$work/pub.der" -out "$work/pub.pem"
  openssl pkeyutl -verify -pubin -inkey "$work/pub.pem" -rawin -in "$out/update.json" -sigfile "$work/sig.bin" >/dev/null
  echo "openssl accepts the signature"
fi

cp "$out/update.json" "$work/tampered.json"
sed -i.bak 's/"sequence": 4/"sequence": 9/' "$work/tampered.json"
if "$work/kogsign" verify "$pub" "$work/tampered.json" "$out/update.json.sig" >/dev/null 2>&1; then
  echo "FAIL: tampered manifest accepted" >&2
  exit 1
fi
echo "tampered manifest rejected"

echo corrupted > "$work/offline/assets/client-linux-x86_64-0.2.0"
if bash "$root/scripts/kog/sign_release.sh" 0.2.0 --offline "$work/offline" --key "$work/keys/primary.key" >/dev/null 2>&1; then
  echo "FAIL: a corrupted asset was signed" >&2
  exit 1
fi
echo "corrupted asset refused"

if [ -n "${KOG_TESTRUNNER:-}" ]; then
  # the runner may abort at exit after its tests passed
  result=$(KOG_UPDATE_FIXTURE_DIR="$out" "$KOG_TESTRUNNER" --gtest_filter='KogUpdate.ScriptFixture' 2>&1 || true)
  echo "$result" | grep -q "PASSED  \] 1 test" || { echo "FAIL: the C++ fixture test" >&2; exit 1; }
  echo "the C++ verifier accepts the manifest"
fi
echo "release chain ok"
