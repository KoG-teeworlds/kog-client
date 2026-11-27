#!/usr/bin/env bash
# usage: linux_package_distro.sh IMAGE TARBALL [SECONDS]
# Starts the packaged Linux client in a clean container of IMAGE that has only the documented runtime packages (docs/LINUX.md).
set -euo pipefail

image=${1:?image}
tarball=$(realpath "${2:?tarball}")
seconds=${3:-15}
here=$(cd "$(dirname "$0")" && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir "$work/client"
tar -xf "$tarball" -C "$work/client" --strip-components=1
exe=$(sed -n 's/^#define KOG_CLIENT_EXECUTABLE "\(.*\)"$/\1/p' "$here/../../src/game/kog_version.h")

docker run --rm -v "$work/client:/client" -v "$here:/scripts:ro" -e "EXE=$exe" -e "SECONDS_TO_RUN=$seconds" -e "HOST_OWNER=$(id -u):$(id -g)" \
  "$image" bash /scripts/linux_package_distro_inner.sh
