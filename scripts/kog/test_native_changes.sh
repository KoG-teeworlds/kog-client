#!/usr/bin/env bash
set -euo pipefail

tool="$(cd "$(dirname "$0")" && pwd -P)/native_changes.sh"
check() {
  local want=$1 got
  shift
  got=$(printf '%s\n' "$@" | bash "$tool")
  [ "$got" = "native=$want" ] || { echo "FAIL: '$*' gave $got, wanted native=$want" >&2; exit 1; }
}

check false docs/PRIVACY.md README.md
check false .github/workflows/kog-spelling.yml scripts/kog/check_brand.sh
check false
check true docs/PRIVACY.md src/engine/shared/kog_login.cpp
check true CMakeLists.txt
check true other/icons/kog.png
check true data/languages/german.txt
check true cmake/FindFoo.cmake
check true scripts/kog/make_package.sh
check true scripts/kog/linux_package_check.sh
check true scripts/kog/linux_package_distros.sh
check true scripts/kog/macos_package_check.py
check true scripts/kog/windows_package_check.py
check true .github/workflows/kog-build.yml
check true .github/workflows/kog-package.yml
check true Cargo.lock
echo "native_changes: ok"
