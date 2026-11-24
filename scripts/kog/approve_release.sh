#!/usr/bin/env bash
set -euo pipefail

# Signature B: verifies the release again on this machine, shows what will be signed, signs with the hardware key.
# usage: approve_release.sh CHANNEL VERSION RUN_ID     (RUN_ID: the "Sign release" run in kog-client-updates)
channel=${1:?channel}
version=${2:?version}
run=${3:?run id}
updates=KoG-teeworlds/kog-client-updates
dir="${KOG_SIGNING_DIR:-$HOME/.config/kog-client-signing}"
key=${KOG_OWNER_KEY:-$dir/owner_sk_1}
namespace=kog-client-release
root=$(cd "$(dirname "$0")/../.." && pwd -P)
case "$channel" in stable) tag="v$version" ;; *) tag="$channel-${version##*.}" ;; esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
[ -f "$key" ] || { echo "no hardware key handle at $key: run scripts/kog/gen_signing_key.sh" >&2; exit 1; }

gh run download "$run" -R "$updates" -n "manifest-$version" -D "$work/ci"
bash "$root/scripts/kog/verify_release.sh" "$channel" "$tag" "$work/own"

python3 - "$work/ci/update.json" "$work/own/update.draft.json" "$work/own/update.json" "$channel" "$version" "$root/scripts/kog/make_manifest.py" <<'PY'
import json, subprocess, sys
ci, draft, out, channel, version, tool = sys.argv[1:7]
m = json.load(open(ci))
assert m["channel"] == channel and m["version"] == version, "the manifest is not %s %s" % (channel, version)
assert (m["expires"] - m["issued"]) % 86400 == 0
extra = []
if "rollout_percent" in m:
    extra += ["--rollout-percent", str(m["rollout_percent"])]
if "min_version" in m:
    extra += ["--min-version", m["min_version"]]
if "notes_url" in m:
    extra += ["--notes-url", m["notes_url"]]
for change in m.get("changes", []):
    extra += ["--change", change]
subprocess.check_call(["python3", tool, "finalize", "--draft", draft, "--sequence", str(m["sequence"]), "--now", str(m["issued"]),
                       "--days", str((m["expires"] - m["issued"]) // 86400), "--out", out] + extra)
PY
cmp "$work/ci/update.json" "$work/own/update.json" || { echo "the manifest of CI differs from the one made here from the verified release" >&2; exit 1; }

python3 - "$work/ci/update.json" "$(cat "$work/own/source.txt")" <<'PY'
import json, sys, time
m = json.load(open(sys.argv[1]))
print("channel:   %s\nversion:   %s\nsequence:  %d\nexpires:   %s\ncommit:    %s" % (m["channel"], m["version"], m["sequence"], time.strftime("%Y-%m-%d", time.gmtime(m["expires"])), sys.argv[2]))
print("files:     %d changed, %d removed, clients: %s" % (len(m["files"]), len(m["remove"]), ", ".join(sorted(m["platforms"]))))
for field in ("rollout_percent", "halted", "min_version", "notes_url"):
    if field in m:
        print("%-10s %s" % (field + ":", m[field]))
for change in m.get("changes", []):
    print("  - " + change)
PY
printf 'Type the version to sign it with your hardware key: '
read -r answer
[ "$answer" = "$version" ] || { echo "not confirmed" >&2; exit 1; }

echo "Touch the security key."
ssh-keygen -Y sign -n "$namespace" -f "$key" "$work/own/update.json" >/dev/null
sigb="$work/own/update.json.sig"
path="approvals/$channel/$version/update.json.sigb"
branch=approvals
gh api "repos/$updates/git/ref/heads/$branch" --silent 2>/dev/null \
  || gh api -X POST "repos/$updates/git/refs" -f ref="refs/heads/$branch" -f sha="$(gh api "repos/$updates/git/ref/heads/main" --jq .object.sha)" --silent
existing=$(gh api "repos/$updates/contents/$path?ref=$branch" --jq .sha 2>/dev/null || true)
gh api -X PUT "repos/$updates/contents/$path" -f message="Approve $channel $version" -f branch="$branch" -f content="$(base64 < "$sigb" | tr -d '\n')" ${existing:+-f sha="$existing"} --silent
echo "signature B uploaded: $updates $branch $path"
