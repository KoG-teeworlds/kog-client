#!/usr/bin/env bash
# Runs inside the container of linux_package_distro.sh. The package lists are the runtime dependencies of docs/LINUX.md,
# plus what the test needs to start a client without a display or a GPU.
set -euo pipefail

if command -v apt-get >/dev/null; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq --no-install-recommends libsdl2-2.0-0 libcurl4 libfreetype6 libgl1 libvulkan1 libnotify4 \
    xvfb xauth libgl1-mesa-dri libglx-mesa0 ca-certificates >/dev/null
elif command -v dnf >/dev/null; then
  dnf install -y -q --setopt=install_weak_deps=False SDL2 libcurl freetype mesa-libGL vulkan-loader libnotify \
    xorg-x11-server-Xvfb xauth mesa-dri-drivers which >/dev/null
else
  echo "no supported package manager in this image" >&2
  exit 1
fi

cd /client
echo "--- $(. /etc/os-release && echo "$PRETTY_NAME")"
if ! libs=$(ldd "./$EXE" 2>&1) || grep -q 'not found' <<< "$libs"; then
  echo "$libs" >&2
  echo "linux package: the dynamic loader cannot resolve the libraries" >&2
  exit 1
fi
bash /scripts/smoke_test.sh /client "$EXE" "$SECONDS_TO_RUN"
