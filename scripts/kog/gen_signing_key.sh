#!/usr/bin/env bash
set -euo pipefail

# The owner's two roles in the release signature: hardware keys (signature B) and the offline backup CI key.
# The CI keys come from OpenTofu (infra-github), never from here.
root=$(cd "$(dirname "$0")/../.." && pwd -P)
dir="${KOG_SIGNING_DIR:-$HOME/.config/kog-client-signing}"
backup_file="${KOG_BACKUP_KEY_FILE:-$HOME/kog-client-backup-signing-key.txt}"
keys=2
while [ $# -gt 0 ]; do
  case "$1" in
    --keys) keys=$2; shift 2 ;;
    *) echo "usage: $0 [--keys 1|2]   (two security keys: one to use, one as a spare in a safe place)" >&2; exit 2 ;;
  esac
done
[[ $keys =~ ^[12]$ ]] || { echo "--keys must be 1 or 2" >&2; exit 2; }
command -v ssh-keygen >/dev/null || { echo "ssh-keygen (OpenSSH 8.2 or newer) is required" >&2; exit 1; }
[ ! -e "$backup_file" ] || { echo "$backup_file exists, refusing to overwrite it" >&2; exit 1; }
mkdir -p "$dir"
chmod 700 "$dir"
cd "$root"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -O2 -o "$work/kogsign" scripts/kog/kogsign.c src/engine/external/monocypher/monocypher.c src/engine/external/monocypher/monocypher-ed25519.c \
  -Isrc/engine/external/monocypher

application=$(sed -n 's/^#define KOG_OWNER_KEY_APPLICATION "\(.*\)"/\1/p' src/engine/client/kog_update_pubkey.h)
owners=()
for n in $(seq 1 "$keys"); do
  handle="$dir/owner_sk_$n"
  [ ! -e "$handle" ] || { echo "$handle exists, refusing to overwrite it" >&2; exit 1; }
  echo "Plug in security key $n and touch it when it blinks."
  ssh-keygen -t ed25519-sk -O "application=$application" -O resident -O verify-required -N "" -C "kog-client-release key $n" -f "$handle" >/dev/null
  owners+=("$(python3 scripts/kog/keys_from_header.py pub-hex "$handle.pub")")
done

umask 077
backup=$("$work/kogsign" keygen "$work/backup.key")
cp "$work/backup.key" "$backup_file"
dd if=/dev/zero of="$work/backup.key" bs=1 count="$(wc -c < "$work/backup.key")" conv=notrunc 2>/dev/null || true

python3 scripts/kog/keys_from_header.py set-owner "${owners[@]}" --backup "$backup"

echo "hardware key public keys and the backup CI public key are in src/engine/client/kog_update_pubkey.h: commit it"
echo "key handles (useless without the device): $dir"
echo "backup CI private key: $backup_file"
echo "Put that file into your password manager and delete it: it is the only copy, and it is not in any repository."
