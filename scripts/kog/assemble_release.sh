#!/usr/bin/env bash
set -euo pipefail

# usage: assemble_release.sh CHANNEL VERSION     artifacts/kog-package-{linux,windows,macos} hold the client packages
# optional env: ROLLOUT_PERCENT MIN_VERSION NOTES_URL CHANGES (one per line)
channel=${1:?channel}
version=${2:?version}
repo=${GITHUB_REPOSITORY:-KoG-teeworlds/kog-client}
base_url=${KOG_PUBLISHED_URL:-https://client.kog.tw}
exe=$(sed -n 's/^#define KOG_CLIENT_EXECUTABLE "\(.*\)"$/\1/p' src/game/kog_version.h)
[ -n "$exe" ] || { echo "KOG_CLIENT_EXECUTABLE is not in src/game/kog_version.h" >&2; exit 1; }
if [ "$channel" = stable ]; then
  grep -q "#define KOG_CLIENT_VERSION \"$version\"" src/game/kog_version.h || { echo "KOG_CLIENT_VERSION is not $version" >&2; exit 1; }
  tag="v$version"
else
  tag="$channel-${version##*.}"
fi

rm -rf release work
mkdir -p release work/linux work/windows
tar -xf artifacts/kog-package-linux/*-linux_x86_64.tar.xz -C work/linux --strip-components=1
unzip -q artifacts/kog-package-windows/*-win64.zip -d work/windows
if [ "$(find work/windows -mindepth 1 -maxdepth 1 | wc -l)" = 1 ] && [ -d "$(find work/windows -mindepth 1 -maxdepth 1)" ]; then
  mv "$(find work/windows -mindepth 1 -maxdepth 1)"/* work/windows/ 2>/dev/null || true
fi

# the client only: a server executable anywhere in a package stops the release
servers=$(find work artifacts -iname 'DDNet-Server*' -o -iname '*-server' -o -iname '*-server.exe' | head -5)
if [ -n "$servers" ]; then
  echo "a server is in the packages:" >&2
  echo "$servers" >&2
  exit 1
fi
for archive in artifacts/kog-package-linux/*.tar.xz; do
  if tar -tf "$archive" | grep -iqE '(^|/)(DDNet-Server|kog-server)'; then echo "a server is in $archive" >&2; exit 1; fi
done
for archive in artifacts/kog-package-windows/*.zip; do
  if unzip -Z1 "$archive" | grep -iqE '(^|/)(DDNet-Server|kog-server)'; then echo "a server is in $archive" >&2; exit 1; fi
done

cp artifacts/kog-package-linux/*.tar.xz release/KoG-Client-linux.tar.xz
cp artifacts/kog-package-windows/*.zip release/KoG-Client-windows.zip
if compgen -G "artifacts/kog-package-macos/*.dmg" >/dev/null; then cp artifacts/kog-package-macos/*.dmg release/KoG-Client-macOS.dmg; fi

package_args=()
for package in release/KoG-Client-*; do
  package_args+=(--package "$(basename "$package")=$package")
done

base_args=(--base work/base.json)
if ! curl -fsS "$base_url/base/files.json" -o work/base.json; then
  base_args=(--init-base release/base-files.json)
fi
previous_args=()
if curl -fsS "$base_url/$channel/update.json" -o work/previous.json; then
  previous_args=(--previous work/previous.json)
fi
extra=()
[ -z "${ROLLOUT_PERCENT:-}" ] || extra+=(--rollout-percent "$ROLLOUT_PERCENT")
[ -z "${MIN_VERSION:-}" ] || extra+=(--min-version "$MIN_VERSION")
[ -z "${NOTES_URL:-}" ] || extra+=(--notes-url "$NOTES_URL")
while IFS= read -r change; do
  [ -z "$change" ] || extra+=(--change "$change")
done <<< "${CHANGES:-}"

python3 scripts/kog/make_manifest.py draft --channel "$channel" --version "$version" --repo "$repo" --tag "$tag" --data work/linux \
  "${base_args[@]}" "${previous_args[@]}" "${extra[@]}" \
  "${package_args[@]}" --client "linux-x86_64=work/linux/$exe" --client "win64=work/windows/$exe.exe" \
  --out release/update.draft.json --uploads work/uploads.json

python3 - <<'PY'
import json, shutil

for upload in json.load(open("work/uploads.json")):
    shutil.copyfile(upload["source"], "release/" + upload["asset"])
PY
echo "$tag" > work/tag.txt
find release -type f | wc -l
