#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd -P)
updates_repo=KoG-teeworlds/kog-client-updates
environment=release-signing
header="${KOG_PUBKEY_HEADER:-$root/src/engine/client/kog_update_pubkey.h}"
backup_file="${KOG_BACKUP_KEY_FILE:-$HOME/kog-client-backup-signing-key.txt}"
updates_dir=""
local_only=0
for arg in "$@"; do
  case "$arg" in
    --updates-dir=*) updates_dir="${arg#--updates-dir=}" ;;
    --local-only) local_only=1 ;;
    *) echo "usage: $0 [--updates-dir=CLONE_OF_kog-client-updates] [--local-only]" >&2; exit 2 ;;
  esac
done

umask 077
work=$(mktemp -d)
wipe() {
  for f in "$work"/*.key; do
    [ -f "$f" ] && dd if=/dev/zero of="$f" bs=1 count="$(wc -c < "$f")" conv=notrunc 2>/dev/null || true
  done
  rm -rf "$work"
}
trap wipe EXIT

cc -O2 -o "$work/kogsign" "$root/scripts/kog/kogsign.c" \
  "$root/src/engine/external/monocypher/monocypher.c" "$root/src/engine/external/monocypher/monocypher-ed25519.c" \
  -I"$root/src/engine/external/monocypher"

if [ "$local_only" = 0 ]; then
  command -v gh >/dev/null || { echo "the gh CLI is required" >&2; exit 1; }
  gh api "repos/$updates_repo/environments/$environment" --silent \
    || { echo "environment $environment of $updates_repo does not exist yet: apply the infrastructure change first" >&2; exit 1; }
fi
[ ! -e "$backup_file" ] || { echo "$backup_file exists, refusing to overwrite it" >&2; exit 1; }

primary=$("$work/kogsign" keygen "$work/primary.key")
if [ "$local_only" = 0 ]; then
  gh secret set SIGNING_KEY --env "$environment" --repo "$updates_repo" < "$work/primary.key" >/dev/null
  echo "the primary private key is stored as the secret SIGNING_KEY of the environment $environment, nowhere else"
else
  echo "local-only: the primary private key is NOT stored anywhere and is discarded (for tests)"
fi

backup=$("$work/kogsign" keygen "$work/backup.key")
cp "$work/backup.key" "$backup_file"

python3 - "$header" "$primary" "$backup" <<'PY'
import sys

path, primary, backup = sys.argv[1:4]
c_bytes = lambda h: ", ".join("0x" + h[i:i + 2] for i in range(0, 64, 2))
text = open(path).read()
start = text.index("// REPLACE") if "// REPLACE" in text else text.index("#define KOG_UPDATE_PUBKEY_PLACEHOLDER")
end = text.index("#endif", start)
body = (
    "#define KOG_UPDATE_PUBKEY_PLACEHOLDER 0\n\n"
    "static const KogUpdate::SPublicKey KOG_UPDATE_PUBKEYS[] = {\n"
    "\t{{%s}}, // primary\n"
    "\t{{%s}}, // backup, its private key is kept offline\n"
    "};\n\n" % (c_bytes(primary), c_bytes(backup))
)
open(path, "w").write(text[:start] + body + text[end:])
PY

if [ -n "$updates_dir" ]; then
  mkdir -p "$updates_dir/keys"
  echo "$primary" > "$updates_dir/keys/primary.pub"
  echo "$backup" > "$updates_dir/keys/backup.pub"
fi

echo "primary public key: $primary"
echo "backup public key:  $backup"
echo "public keys written to $header${updates_dir:+ and $updates_dir/keys/}: commit them"
echo "backup private key: $backup_file"
echo "Put that file into your password manager, then delete it. It is the only copy."
