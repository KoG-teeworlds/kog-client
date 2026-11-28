# Linux

What the released Linux client needs, and how that is kept true. The release is `KoGClient-VERSION-linux_x86_64.tar.xz`;
unpack it and run `KoG-Client`.

## Supported systems

x86_64 with glibc 2.35 or newer. Every release is started in clean containers of these before it is published
(`scripts/kog/linux_package_distros.sh`):

| Distribution | Status |
| --- | --- |
| Ubuntu 22.04, 24.04 | tested on every pull request and release |
| Debian 12 | tested on every pull request and release |
| Fedora (the current release) | tested on every pull request and release |
| Others with glibc 2.35 or newer (Arch, openSUSE, Linux Mint, Pop!_OS, ...) | expected to work, not tested |
| Ubuntu 20.04, Debian 11 and other systems with older glibc | not supported |

The package is built on Ubuntu 22.04 on purpose: the binary needs the glibc of the machine it is built on or newer.
`scripts/kog/linux_package_check.sh` fails the build if it needs more than 2.35.

## Runtime packages

Needed, and normally already there on a desktop installation:

| Library | Debian, Ubuntu | Fedora |
| --- | --- | --- |
| SDL 2 | `libsdl2-2.0-0` | `SDL2` |
| libcurl | `libcurl4` | `libcurl` |
| FreeType | `libfreetype6` | `freetype` |
| OpenGL | `libgl1` | `mesa-libGL` |
| Vulkan loader | `libvulkan1` | `vulkan-loader` |
| libnotify | `libnotify4` | `libnotify` |

On Debian and Ubuntu: `sudo apt install libsdl2-2.0-0 libcurl4 libfreetype6 libgl1 libvulkan1 libnotify4`.
On Fedora: `sudo dnf install SDL2 libcurl freetype mesa-libGL vulkan-loader libnotify`.
You also need a graphics driver for OpenGL or Vulkan, as for any game.

**FFmpeg is not needed.** The video recorder (`start_video`) uses FFmpeg 7.0.1 and x264 that are linked into the
client. Nothing else, such as GLEW, libpng, Opus, WavPack or SQLite, is loaded from the system either.

## How this is kept true

- `kog-package.yml` and `kog-build.yml` build with `-DPREFER_BUNDLED_LIBS=ON`, which uses the static libraries of
  `ddnet-libs` (as DDNet does for its releases), and link the C++ runtime statically. The FFmpeg development packages are
  not installed, so the build cannot pick up the system FFmpeg.
- `scripts/kog/linux_package_check.sh` fails if the binary needs a shared library that is not in the table above (plus glibc and
  `libgobject`, which libnotify brings), has an RPATH, or needs glibc newer than 2.35. When you add a dependency on purpose, add it
  to the script, to the table and to `scripts/kog/linux_package_distro_inner.sh`.
- `scripts/kog/linux_package_distros.sh` starts the package in a clean container of each distribution with only these packages.

The problem this prevents: a canary build made on `ubuntu-latest` (Ubuntu 26.04) linked FFmpeg 8 dynamically and failed
on other systems with `libavformat.so.62: cannot open shared object file`; it also needed glibc 2.43.

## Licences of what is linked in

The client is zlib licensed. The FFmpeg in `ddnet-libs/ffmpeg` is a minimal build (`--disable-all`; only the libx264 and
AAC encoders, the MP4 and MOV muxers, libswscale and libswresample) made with `--enable-gpl` and x264, so the FFmpeg and x264 parts
are **GPL version 2 or later**, not LGPL. The zlib licence is compatible, so the whole binary may be passed on under the GPL
for those parts. The source for what is linked in:

- FFmpeg 7.0.1 and x264: [ffmpeg.org](https://ffmpeg.org/download.html), [videolan.org/developers/x264.html](https://www.videolan.org/developers/x264.html);
  the build recipe is in [ddnet-libs](https://github.com/ddnet/ddnet-libs) (`ffmpeg/`, with its configure line stored in the libraries)
- this client: the tag of the release in this repository

To have a client without these parts, build with `-DVIDEORECORDER=OFF`; it then has no video recorder.
