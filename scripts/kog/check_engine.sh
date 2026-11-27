#!/usr/bin/env bash
# Strict warnings and clang-tidy for the KoG files (kog_*.cpp) that a branch adds or changes, and with --all for the engine
# files that are ours. Files of upstream are not checked: their headers do not hold up to these warnings (KOG_STRICT_SOURCES in CMakeLists.txt). The older files of the list still have findings in tidy (uninitialised
# locals, missing const, nodiscard), so --all is for working through them and CI checks what a pull request touches.
#
# Usage: scripts/kog/check_engine.sh <build directory> (--changed <base ref> | --all) [--tidy-only|--warnings-only]
#
# The build directory has to be configured with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON and built once, so that the generated
# headers exist.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${1:?usage: check_engine.sh <build directory> (--changed <base ref> | --all) [--tidy-only|--warnings-only]}"
shift
changed=""
all=0
run_warnings=1
run_tidy=1
while [ $# -gt 0 ]; do
	case "$1" in
	--changed) changed="${2:?base ref}"; shift ;;
	--all) all=1 ;;
	--tidy-only) run_warnings=0 ;;
	--warnings-only) run_tidy=0 ;;
	*) echo "unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done

[ -n "$changed" ] || [ "$all" = 1 ] || { echo "give --changed <base ref> or --all" >&2; exit 2; }
[ -f "$build/compile_commands.json" ] || { echo "no compile_commands.json in $build" >&2; exit 2; }

list=""
if [ "$all" = 1 ]; then
	list="$(sed -n '/KOG_STRICT_SOURCES/,/)/p' "$root/CMakeLists.txt" | grep -E '^\s+src/.*\.cpp' | tr -d ' \t')"
fi
if [ -n "$changed" ]; then
	list="$list
$(git -C "$root" diff --name-only --diff-filter=AM "$changed"...HEAD | grep -E '^src/.*/kog_[^/]*\.cpp$' || true)"
fi
files="$(printf '%s\n' "$list" | grep . | sort -u)"
added=""
if [ -n "$changed" ]; then
	added="$(git -C "$root" diff --name-only --diff-filter=A "$changed"...HEAD)"
fi

# Without the precompiled headers: they come from the compiler that built them, which may not be the one that checks.
db="$(mktemp -d)"
trap 'rm -rf "$db"' EXIT
python3 - "$build/compile_commands.json" "$db/compile_commands.json" <<'PY'
import json, shlex, sys
entries = json.load(open(sys.argv[1]))
for entry in entries:
    words, kept, skip = shlex.split(entry["command"]), [], 0
    for index, word in enumerate(words):
        if skip:
            skip -= 1
        elif word == "-Xclang" and words[index + 1] in ("-include-pch", "-include") and "cmake_pch" in words[index + 3]:
            skip = 3
        elif word == "-include" and "cmake_pch" in words[index + 1]:
            skip = 1
        elif word in ("-Winvalid-pch", "-MD", "-MMD") or "cmake_pch" in word:
            pass
        elif word in ("-MT", "-MF"):
            skip = 1
        else:
            kept.append(word)
    entry["command"] = shlex.join(kept)
json.dump(entries, open(sys.argv[2], "w"))
PY
build_db="$db"

in_database() { grep -q "\"file\": \"[^\"]*$1\"" "$build_db/compile_commands.json"; }

status=0
tidy="${CLANG_TIDY:-clang-tidy}"
for file in $files; do
	[ -f "$root/$file" ] || continue
	in_database "$file" || { echo "skip $file (not in compile_commands.json)"; continue; }
	if [ "$run_warnings" = 1 ]; then
		command="$(python3 - "$build_db" "$root/$file" <<'PY'
import json, shlex, sys
for entry in json.load(open(sys.argv[1] + "/compile_commands.json")):
    if entry["file"] == sys.argv[2]:
        words, kept, skip = shlex.split(entry["command"]), [], 0
        for word in words:
            if skip:
                skip -= 1
            elif word == "-o":
                skip = 1
            elif word != "-c":
                kept.append(word)
        print(shlex.join(kept))
        break
PY
)"
		if ! (cd "$build" && eval "$command -fsyntax-only -Wall -Wextra -Wconversion -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-qual -Wno-#warnings -Werror"); then
			status=1
		fi
	fi
	# clang-tidy gates the files a branch adds: the older ones still have findings (see --all)
	if [ "$run_tidy" = 1 ] && { [ "$all" = 1 ] || printf '%s\n' "$added" | grep -qx "$file"; }; then
		"$tidy" -p "$build_db" --config-file="$root/scripts/kog/clang-tidy-engine.yaml" --quiet "$root/$file" || status=1
	fi
done
exit "$status"
