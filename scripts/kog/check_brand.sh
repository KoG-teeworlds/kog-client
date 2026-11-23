#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/../.."
fail=0
report() { echo "brand check: $1" >&2; fail=1; }

grep -qE '#define KOG_CLIENT_NAME "[^"]*(TClient|T-Client|DDNet)[^"]*"' src/game/kog_version.h && report "KOG_CLIENT_NAME must not be TClient or DDNet"

hits=$(git grep -n -I -i -E 'tclient|t-client|taterclient|tater' -- other ':!other/icons/license.txt' ':!other/emscripten' 2>/dev/null) && report "old name in packaging files: $hits"
hits=$(git grep -n -w -E 'TClient|T-Client|TaterClient|Tater' -- CMakeLists.txt | grep -v -E '^CMakeLists.txt:[0-9]+:\s*#') && report "old name in CMakeLists.txt: $hits"
hits=$(git grep -n -E 'TClient\.(app|exe)|TClient-' -- . ':!NOTICE_KOG.md' ':!README_TCLIENT.md' ':!.github/workflows/build*.yml' ':!.github/workflows/fast-build.yml' ':!.github/workflows/debug-build.yml' ':!scripts/kog/check_brand.sh' 2>/dev/null) && report "old product file name: $hits"
hits=$(git grep -n -E 'Init\("TClient|TCLocalize\("TClient( Settings)?"\)|SetCaption\("TClient' -- src 2>/dev/null) && report "old name in a window title or label: $hits"

exit $fail
