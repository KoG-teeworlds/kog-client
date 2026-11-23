#!/usr/bin/env bash
set -euo pipefail

version=${1:?usage: assemble_release.sh VERSION}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "invalid version" >&2; exit 2; }
grep -q "#define KOG_CLIENT_VERSION \"$version\"" src/game/kog_version.h || { echo "KOG_CLIENT_VERSION is not $version" >&2; exit 1; }

repo=${GITHUB_REPOSITORY:-KoG-teeworlds/kog-client}
rm -rf release work
mkdir -p release work/linux work/windows

tar -xf artifacts/ddnet-ubuntu-22.04/*-linux_x86_64.tar.xz -C work/linux --strip-components=1
unzip -q artifacts/ddnet-windows-latest/*-win64.zip -d work/windows
if [ "$(find work/windows -mindepth 1 -maxdepth 1 | wc -l)" = 1 ] && [ -d "$(find work/windows -mindepth 1 -maxdepth 1)" ]; then
  mv "$(find work/windows -mindepth 1 -maxdepth 1)"/* work/windows/ 2>/dev/null || true
fi

cp artifacts/ddnet-ubuntu-22.04/*.tar.xz release/KoG-Client-linux.tar.xz
cp artifacts/ddnet-windows-latest/*.zip release/KoG-Client-windows.zip
cp artifacts/ddnet-macOS-latest/*.dmg release/KoG-Client-macOS.dmg
cp artifacts/ddnet-android/*.apk release/KoG-Client-android.apk

base_args=(--base work/base.json)
if curl -fsS https://client.kog.tw/base/files.json -o work/base.json; then
  :
else
  base_args=(--init-base release/base-files.json)
fi
previous_args=()
if curl -fsS https://client.kog.tw/update.json -o work/previous.json; then
  previous_args=(--previous work/previous.json)
fi

python3 scripts/kog/make_manifest.py draft --version "$version" --repo "$repo" --tag "v$version" --data work/linux \
  "${base_args[@]}" "${previous_args[@]}" \
  --client linux-x86_64=work/linux/DDNet --client win64=work/windows/DDNet.exe \
  --out release/update.draft.json --uploads work/uploads.json

python3 - <<'PY'
import json, shutil

for upload in json.load(open("work/uploads.json")):
    shutil.copyfile(upload["source"], "release/" + upload["asset"])
PY
find release -type f | wc -l
