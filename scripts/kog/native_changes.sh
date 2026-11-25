#!/usr/bin/env bash
# Reads changed file names on stdin; prints "native=true" if any of them can change the native build, else "native=false"
set -euo pipefail

if grep -qE '^(src/|data/|datasrc/|other/|cmake/|ddnet-libs|CMakeLists\.txt$|Cargo\.(toml|lock)$|scripts/kog/[^/]*package[^/]*$|\.github/workflows/(kog-build|kog-package)\.yml$)'; then
  echo "native=true"
else
  echo "native=false"
fi
