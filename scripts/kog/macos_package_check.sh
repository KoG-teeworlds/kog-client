#!/usr/bin/env bash
# usage: macos_package_check.sh DMG
# Opens the DMG, checks every library the app loads (macos_package_check.py), then starts the app far enough for dyld to load
# all of them. With HIDE_HOMEBREW=1 the Homebrew prefix is moved away first (needs sudo; for the last step of a job).
# The hosted runners have no GPU, so the client stops at graphics start-up; only a dyld failure counts here.
set -uo pipefail

dmg=$(cd "$(dirname "${1:?dmg}")" && pwd -P)/$(basename "$1")
here=$(cd "$(dirname "$0")" && pwd -P)
work=$(mktemp -d)
mnt="$work/mnt"
trap 'hdiutil detach "$mnt" -quiet 2>/dev/null; rm -rf "$work"' EXIT

mkdir "$mnt"
hdiutil attach "$dmg" -nobrowse -readonly -mountpoint "$mnt" -quiet || { echo "cannot open $dmg" >&2; exit 1; }
app=$(find "$mnt" -maxdepth 1 -name '*.app' | head -n 1)
[ -n "$app" ] || { echo "no app in the DMG" >&2; exit 1; }
cp -R "$app" "$work/app"
hdiutil detach "$mnt" -quiet

python3 -I "$here/macos_package_check.py" "$work/app" || exit 1

if [ "${HIDE_HOMEBREW:-}" = 1 ]; then
  for prefix in /opt/homebrew /usr/local/Homebrew; do
    if [ -d "$prefix" ]; then sudo mv "$prefix" "$prefix.hidden" || exit 1; fi
  done
fi

exe=$(find "$work/app/Contents/MacOS" -type f -perm -100 | head -n 1)
export HOME="$work/home" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
mkdir -p "$HOME"
env -i HOME="$HOME" PATH=/usr/bin:/bin SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$exe" "cl_save_settings 0" "stdout_output_level 1" > "$work/out.txt" 2>&1 &
pid=$!
sleep 10
if kill -0 "$pid" 2>/dev/null; then
  kill "$pid"
  echo "the app is running"
fi
wait "$pid" 2>/dev/null
echo "--- output"
head -n 30 "$work/out.txt"
if grep -E 'dyld|Library not loaded|image not found|Symbol not found' "$work/out.txt"; then
  echo "macos package check: the app does not load" >&2
  exit 1
fi
echo "macos package check: started ok"
