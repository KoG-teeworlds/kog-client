#!/usr/bin/env bash
# usage: linux_package_check.sh BINARY [MAX_GLIBC]
# Fails when the Linux client needs a shared library that is not on the list below, has an absolute RPATH or needs a newer glibc
# than MAX_GLIBC (default 2.35, Ubuntu 22.04). Keep the list in step with docs/LINUX.md.
set -euo pipefail

bin=${1:?binary}
max_glibc=${2:-2.35}

# glibc, which every distribution has
allowed=(libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 ld-linux-x86-64.so.2)
# system libraries that are not linked in: the documented runtime packages
allowed+=(libSDL2-2.0.so.0 libcurl.so.4 libfreetype.so.6 libGL.so.1 libvulkan.so.1 libnotify.so.4 libgobject-2.0.so.0)

status=0
dynamic=$(readelf -d "$bin")

while read -r lib; do
  [ -n "$lib" ] || continue
  if ! printf '%s\n' "${allowed[@]}" | grep -qxF "$lib"; then
    echo "unexpected shared library: $lib" >&2
    status=1
  fi
done < <(sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' <<< "$dynamic")

if rpath=$(grep -E '\((RPATH|RUNPATH)\)' <<< "$dynamic"); then
  echo "unexpected RPATH/RUNPATH: $rpath" >&2
  status=1
fi

highest=$(objdump -T "$bin" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -n 1)
if [ -n "$highest" ] && [ "$(printf '%s\n%s\n' "$highest" "$max_glibc" | sort -V | tail -n 1)" != "$max_glibc" ]; then
  echo "needs glibc $highest, the limit is $max_glibc" >&2
  status=1
fi

echo "needed libraries:"
sed -n 's/.*(NEEDED).*\[\(.*\)\]/  \1/p' <<< "$dynamic"
echo "highest glibc symbol version: ${highest:-none}"
[ "$status" -eq 0 ] && echo "linux package check: ok" || echo "linux package check: FAILED" >&2
exit $status
