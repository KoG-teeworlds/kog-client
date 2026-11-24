#!/usr/bin/env bash
set -euo pipefail

# Puts a signed manifest into the published branch of the updates repo, after checking both signatures once more.
# usage: publish_signed.sh CHANNEL VERSION DIR MESSAGE     (DIR: update.json, update.json.sig, update.json.sigb)
# env: GH_TOKEN with write access to the updates repo, or UPDATES_DEPLOY_KEY (a private key with write access); SOURCE_COMMIT, RUN_URL for the audit record; SITE_DIR for the site files (default: site/ on main of the updates repo)
channel=${1:?channel}
version=${2:?version}
dir=${3:?dir}
message=${4:?message}
repo=${KOG_UPDATES_REPO:-KoG-teeworlds/kog-client-updates}
root=$(cd "$(dirname "$0")/../.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cc -O2 -o "$work/kogsign" "$root/scripts/kog/kogsign.c" "$root/src/engine/external/monocypher/monocypher.c" \
  "$root/src/engine/external/monocypher/monocypher-ed25519.c" -I"$root/src/engine/external/monocypher"
cd "$root"
verified=0
while read -r pub; do
  if "$work/kogsign" verify "$pub" "$dir/update.json" "$dir/update.json.sig" >/dev/null 2>&1; then verified=1; fi
done < <({ python3 scripts/kog/keys_from_header.py ci "$channel"; [ "$channel" = stable ] && python3 scripts/kog/keys_from_header.py backup; } || true)
[ "$verified" = 1 ] || { echo "the CI signature is not valid for any key of the $channel channel" >&2; exit 1; }
if [ "$channel" = stable ]; then
  python3 scripts/kog/keys_from_header.py allowed-signers > "$work/allowed_signers"
  ssh-keygen -Y verify -f "$work/allowed_signers" -I owner -n kog-client-release -s "$dir/update.json.sigb" < "$dir/update.json" >/dev/null \
    || { echo "the owner signature is not valid" >&2; exit 1; }
fi
python3 - "$dir/update.json" "$channel" "$version" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
assert m["channel"] == sys.argv[2] and m["version"] == sys.argv[3], "channel or version of the manifest differ"
PY

if [ -n "${UPDATES_DEPLOY_KEY:-}" ]; then
  umask 077
  printf '%s\n' "$UPDATES_DEPLOY_KEY" > "$work/deploy_key"
  echo "github.com ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl" > "$work/known_hosts"
  export GIT_SSH_COMMAND="ssh -i $work/deploy_key -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=$work/known_hosts"
  remote="git@github.com:$repo.git"
  git_remote() { git "$@"; }
else
  header="AUTHORIZATION: basic $(printf 'x-access-token:%s' "$GH_TOKEN" | base64 | tr -d '\n')"
  echo "::add-mask::$header"
  remote="https://github.com/$repo"
  git_remote() { git -c "http.https://github.com/.extraheader=$header" "$@"; }
fi
if git_remote ls-remote --exit-code --heads "$remote" published >/dev/null 2>&1; then
  git_remote clone --quiet --branch published "$remote" "$work/published"
else
  git init --quiet -b published "$work/published"
  git -C "$work/published" remote add origin "$remote"
fi
published="$work/published"
if [ -f "$published/$channel/update.json" ]; then
  python3 - "$published/$channel/update.json" "$dir/update.json" <<'PY'
import json, sys
old, new = (json.load(open(path)) for path in sys.argv[1:3])
assert new["sequence"] > old["sequence"], "the sequence does not rise: %d after %d" % (new["sequence"], old["sequence"])
PY
fi
site_dir=${SITE_DIR:-}
if [ -z "$site_dir" ] && git_remote clone --quiet --depth 1 --branch main "$remote" "$work/main" 2>/dev/null && [ -d "$work/main/site" ]; then
  site_dir="$work/main/site"
fi
if [ -n "$site_dir" ]; then cp -R "$site_dir/." "$published/"; fi
mkdir -p "$published/$channel/releases/$version"
for f in update.json update.json.sig update.json.sigb; do
  [ -f "$dir/$f" ] || continue
  cp "$dir/$f" "$published/$channel/$f"
  cp "$dir/$f" "$published/$channel/releases/$version/$f"
done
echo client.kog.tw > "$published/CNAME"
touch "$published/.nojekyll"

sha=$(sha256sum "$dir/update.json" 2>/dev/null | cut -d' ' -f1 || shasum -a 256 "$dir/update.json" | cut -d' ' -f1)
git -C "$published" add -A
git -C "$published" -c user.name="kog-release" -c user.email="kog-release@users.noreply.github.com" commit --quiet -m "$message" -m "manifest sha256: $sha
source commit: ${SOURCE_COMMIT:-unknown}
run: ${RUN_URL:-by hand}"
git_remote -C "$published" push --quiet origin HEAD:published
echo "published $channel $version, manifest sha256 $sha"
