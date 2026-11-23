#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tool=(python3 "$root/scripts/kog/make_manifest.py")
fail() { echo "FAIL: $*" >&2; exit 1; }
refused() { if "$@" >/dev/null 2>&1; then fail "accepted: $*"; fi; }

cc -O2 -o "$work/kogsign" "$root/scripts/kog/kogsign.c" \
  "$root/src/engine/external/monocypher/monocypher.c" "$root/src/engine/external/monocypher/monocypher-ed25519.c" \
  -I"$root/src/engine/external/monocypher"

tree="$work/tree"
mkdir -p "$tree/data/sub"
echo one > "$tree/data/a.txt"
echo two > "$tree/data/sub/b.txt"
echo evil > "$tree/data/bad.dll"
echo binary1 > "$work/client1"
"${tool[@]}" draft --init-base "$work/base.json" --version 0.1.0 --tag v0.1.0 --data "$tree" --client linux-x86_64="$work/client1" --out "$work/draft1.json" --uploads "$work/uploads1.json" 2>/dev/null
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); assert m["files"]==[] and m["remove"]==[], m' "$work/draft1.json"

echo changed > "$tree/data/a.txt"
echo three > "$tree/data/c.txt"
rm "$tree/data/sub/b.txt"
echo binary2 > "$work/client2"
"${tool[@]}" draft --base "$work/base.json" --version 0.2.0 --tag v0.2.0 --data "$tree" --client linux-x86_64="$work/client2" \
  --package KoG-Client-linux.tar.xz="$work/client2" --rollout-percent 25 --min-version 0.1.0 --notes-url https://client.kog.tw/notes/0.2.0 --change "Faster start" --change "Fixes a crash" \
  --out "$work/draft2.json" --uploads "$work/uploads2.json" 2>/dev/null
python3 - "$work/draft2.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
assert [f["path"] for f in m["files"]] == ["data/a.txt", "data/c.txt"], m["files"]
assert m["remove"] == ["data/sub/b.txt"], m["remove"]
assert m["sequence"] == 0 and m["rollout_percent"] == 25 and m["min_version"] == "0.1.0" and len(m["changes"]) == 2
p = m["packages"][0]
assert p["name"] == "KoG-Client-linux.tar.xz" and len(p["sha256"]) == 64 and p["size"] > 0 and p["url"].endswith("/v0.2.0/KoG-Client-linux.tar.xz"), p
PY

echo "the manifest builder refuses what it must"
refused "${tool[@]}" draft --base "$work/base.json" --version 0.2.0-canary.1 --tag t --data "$tree" --client linux-x86_64="$work/client2" --out "$work/x.json" --uploads "$work/x2.json"
refused "${tool[@]}" draft --base "$work/base.json" --channel canary --version 0.2.0 --tag t --data "$tree" --client linux-x86_64="$work/client2" --out "$work/x.json" --uploads "$work/x2.json"
refused "${tool[@]}" draft --base "$work/base.json" --channel dev --version 0.2.0-canary.1 --tag t --data "$tree" --client linux-x86_64="$work/client2" --out "$work/x.json" --uploads "$work/x2.json"
refused "${tool[@]}" draft --base "$work/base.json" --version 0.2.0 --tag t --data "$tree" --client linux-x86_64="$work/client2" --change a --change b --change c --change d --out "$work/x.json" --uploads "$work/x2.json"
refused "${tool[@]}" draft --base "$work/base.json" --version 0.2.0 --tag t --data "$tree" --client linux-x86_64="$work/client2" --rollout-percent 101 --out "$work/x.json" --uploads "$work/x2.json"
mkdir -p "$tree/data/server"
echo server > "$tree/data/server/DDNet-Server"
refused "${tool[@]}" draft --base "$work/base.json" --version 0.2.0 --tag t --data "$tree" --client linux-x86_64="$work/client2" --out "$work/x.json" --uploads "$work/x2.json"
rm -r "$tree/data/server"
cp "$work/client2" "$work/DDNet-Server"
refused "${tool[@]}" draft --base "$work/base.json" --version 0.2.0 --tag t --data "$tree" --client linux-x86_64="$work/DDNet-Server" --out "$work/x.json" --uploads "$work/x2.json"
refused "${tool[@]}" draft --base "$work/base.json" --version 0.2.0 --tag t --data "$tree" --client linux-x86_64="$work/client2" --package evil.exe="$work/client2" --out "$work/x.json" --uploads "$work/x2.json"
refused "${tool[@]}" finalize --draft "$work/draft2.json" --sequence 1 --days 91 --out "$work/x.json"
refused "${tool[@]}" finalize --draft "$work/draft2.json" --sequence 0 --out "$work/x.json"
sed 's/"channel": "stable"/"channel": "dev"/;s/"version": "0.2.0"/"version": "0.2.0-dev.3"/' "$work/draft2.json" > "$work/draft_dev.json"
refused "${tool[@]}" finalize --draft "$work/draft_dev.json" --sequence 1 --days 15 --out "$work/x.json"
"${tool[@]}" finalize --draft "$work/draft_dev.json" --sequence 1 --days 14 --out "$work/dev.json"

"${tool[@]}" finalize --draft "$work/draft2.json" --sequence 4 --days 30 --now 1000000 --out "$work/update.json"
echo '{"channel": "stable", "version": "0.2.0", "sequence": 4}' > "$work/same.json"
echo '{"channel": "stable", "version": "0.3.0", "sequence": 4}' > "$work/newer.json"
refused "${tool[@]}" newer --manifest "$work/update.json" --previous "$work/same.json"
refused "${tool[@]}" newer --manifest "$work/update.json" --previous "$work/newer.json"
echo '{"channel": "dev", "version": "0.1.0-dev.1", "sequence": 1}' > "$work/other.json"
refused "${tool[@]}" newer --manifest "$work/update.json" --previous "$work/other.json"
echo '{"channel": "stable", "version": "0.1.0", "sequence": 1}' > "$work/older.json"
"${tool[@]}" newer --manifest "$work/update.json" --previous "$work/older.json"
"${tool[@]}" finalize --draft "$work/draft2.json" --sequence 1 --days 5 --rollout-percent 10 --min-version 0.1.0 --change "One" --out "$work/opt.json"
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); assert m["rollout_percent"]==10 and m["min_version"]=="0.1.0" and m["changes"]==["One"], m' "$work/opt.json"
"${tool[@]}" halt --previous "$work/update.json" --now 2000000 --out "$work/halt.json"
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); assert m["halted"] is True and m["sequence"]==5, m' "$work/halt.json"

echo "signatures A and B"
fixture="$work/fixture"
mkdir "$fixture"
cp "$work/update.json" "$fixture/update.json"
"$work/kogsign" keygen "$work/ci.key" > "$fixture/ci.pub"
"$work/kogsign" sign "$work/ci.key" "$fixture/update.json" > "$fixture/update.json.sig"
"$work/kogsign" verify "$(cat "$fixture/ci.pub")" "$fixture/update.json" "$fixture/update.json.sig" >/dev/null
"$work/kogsign" keygen "$work/owner.key" > /dev/null
python3 "$root/scripts/kog/sk_emulate.py" "$work/kogsign" "$work/owner.key" "ssh:" kog-client-release "$fixture/update.json" > "$fixture/update.json.sigb" 2> "$fixture/owner.pub"
echo "ssh:" > "$fixture/owner_application.txt"

if command -v ssh-keygen >/dev/null; then
  cp "$root/src/engine/client/kog_update_pubkey.h" "$work/header.h"
  ci=$(cat "$fixture/ci.pub")
  KOG_PUBKEY_HEADER="$work/header.h" python3 - "$ci" <<'PY'
import os, re, sys
path = os.environ["KOG_PUBKEY_HEADER"]
text = open(path).read()
text = re.sub(r'(KOG_STABLE_CI_KEYS\[\] = )\{[^}]*\};', lambda m: m.group(1) + '{"%s", "%s"};' % (sys.argv[1], "ab" * 32), text)
open(path, "w").write(text)
PY
  KOG_PUBKEY_HEADER="$work/header.h" python3 "$root/scripts/kog/keys_from_header.py" set-owner "$(cat "$fixture/owner.pub")" --backup "$(printf 'cd%.0s' $(seq 32))"
  grep -q "KOG_UPDATE_KEYS_PLACEHOLDER_STABLE 0" "$work/header.h" || fail "the placeholder stays with all stable keys set"
  KOG_PUBKEY_HEADER="$work/header.h" python3 "$root/scripts/kog/keys_from_header.py" allowed-signers > "$work/allowed_signers"
  ssh-keygen -Y verify -f "$work/allowed_signers" -I owner -n kog-client-release -s "$fixture/update.json.sigb" < "$fixture/update.json" >/dev/null \
    || fail "OpenSSH does not accept the signature the verifier is tested with"
  refused ssh-keygen -Y verify -f "$work/allowed_signers" -I owner -n other-namespace -s "$fixture/update.json.sigb" < "$fixture/update.json"
  sed 's/"sequence": 4/"sequence": 9/' "$fixture/update.json" > "$work/tampered.json"
  refused ssh-keygen -Y verify -f "$work/allowed_signers" -I owner -n kog-client-release -s "$fixture/update.json.sigb" < "$work/tampered.json"
  echo "OpenSSH accepts the owner signature and refuses a wrong namespace and a tampered manifest"

  ssh-keygen -q -t ed25519 -N "" -f "$work/soft" -C test
  cp "$fixture/update.json" "$work/soft_msg.json"
  ssh-keygen -Y sign -n kog-client-release -f "$work/soft" "$work/soft_msg.json" >/dev/null 2>&1
  cp "$work/soft_msg.json.sig" "$fixture/update.json.sigb.software"
  python3 -c 'import base64,sys; b=base64.b64decode(open(sys.argv[1]).read().split()[1]); print(b[-32:].hex())' "$work/soft.pub" > "$fixture/owner_software.pub"
fi

cp "$work/fixture/update.json" "$work/tampered_ci.json"
sed -i.bak 's/"sequence": 4/"sequence": 9/' "$work/tampered_ci.json"
refused "$work/kogsign" verify "$(cat "$fixture/ci.pub")" "$work/tampered_ci.json" "$fixture/update.json.sig"

if command -v openssl >/dev/null; then
  python3 - "$(cat "$fixture/ci.pub")" "$fixture/update.json.sig" "$work/pub.der" "$work/sig.bin" <<'PY'
import sys
pub, sigfile, der, sig = sys.argv[1:5]
open(der, "wb").write(bytes.fromhex("302a300506032b6570032100" + pub))
open(sig, "wb").write(bytes.fromhex(open(sigfile).read().split()[2]))
PY
  openssl pkey -pubin -inform DER -in "$work/pub.der" -out "$work/pub.pem"
  openssl pkeyutl -verify -pubin -inkey "$work/pub.pem" -rawin -in "$fixture/update.json" -sigfile "$work/sig.bin" >/dev/null
  echo "openssl accepts the CI signature"
fi

echo "the release assembly refuses a package with a server"
run="$work/run"
mkdir -p "$run/artifacts/kog-package-linux" "$run/artifacts/kog-package-windows" "$run/pkg/linux/data" "$run/pkg/windows/data"
ln -s "$root/scripts" "$run/scripts"
mkdir -p "$run/src/game"
cp "$root/src/game/kog_version.h" "$run/src/game/"
echo client > "$run/pkg/linux/KoG-Client"
echo data > "$run/pkg/linux/data/a.txt"
echo client > "$run/pkg/windows/KoG-Client.exe"
(cd "$run/pkg" && tar -cJf "$run/artifacts/kog-package-linux/KoGClient-1-linux_x86_64.tar.xz" --transform 's,^linux,KoGClient-1-linux_x86_64,' linux && cd windows && python3 -c 'import zipfile; z=zipfile.ZipFile("../../artifacts/kog-package-windows/KoGClient-1-win64.zip","w"); z.write("KoG-Client.exe")')
(cd "$run" && KOG_PUBLISHED_URL=http://127.0.0.1:9 bash scripts/kog/assemble_release.sh canary 0.1.0-canary.5 >/dev/null 2>&1) || fail "a clean client package was refused"
test -f "$run/release/update.draft.json" || fail "no draft"
echo server > "$run/pkg/linux/DDNet-Server"
(cd "$run/pkg" && tar -cJf "$run/artifacts/kog-package-linux/KoGClient-1-linux_x86_64.tar.xz" --transform 's,^linux,KoGClient-1-linux_x86_64,' linux)
if (cd "$run" && KOG_PUBLISHED_URL=http://127.0.0.1:9 bash scripts/kog/assemble_release.sh canary 0.1.0-canary.5 >/dev/null 2>&1); then fail "a server in the package was accepted"; fi

if [ -n "${KOG_TESTRUNNER:-}" ]; then
  # the runner may abort at exit after its tests passed
  result=$(KOG_UPDATE_FIXTURE_DIR="$fixture" "$KOG_TESTRUNNER" --gtest_filter='KogUpdate.ScriptFixture' 2>&1 || true)
  echo "$result" | grep -q "PASSED  \] 1 test" || { echo "$result" | tail -15 >&2; fail "the C++ fixture test"; }
  echo "the C++ verifier accepts the manifest with both signatures"
fi
echo "release chain ok"
