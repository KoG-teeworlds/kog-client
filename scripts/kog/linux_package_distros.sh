#!/usr/bin/env bash
# usage: linux_package_distros.sh TARBALL
# Starts the packaged Linux client in a clean container of each supported distribution (docs/LINUX.md); runs all, fails if any failed.
set -uo pipefail

tarball=${1:?tarball}
here=$(cd "$(dirname "$0")" && pwd -P)
# Fixed tags for the releases we support; fedora:latest is on purpose, it shows library versions before users get them
images=(ubuntu:22.04 ubuntu:24.04 debian:12-slim fedora:latest)

failed=()
for image in "${images[@]}"; do
  echo "::group::$image"
  bash "$here/linux_package_distro.sh" "$image" "$tarball" 15 || failed+=("$image")
  echo "::endgroup::"
done

if [ "${#failed[@]}" -gt 0 ]; then
  echo "the client did not start on: ${failed[*]}" >&2
  exit 1
fi
echo "linux package distributions: ok (${images[*]})"
