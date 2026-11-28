# macOS and Windows packages

What the packages need and which checks keep that true. The Linux package is in [LINUX.md](LINUX.md) (on `main` once that page is merged).

## macOS

The DMG holds `KoG-Client.app`. The libraries (SDL2, FFmpeg, FreeType, libpng) come from `ddnet-libs` and are copied into
`Contents/Frameworks`; the build uses `-DPREFER_BUNDLED_LIBS=ON`, so nothing is loaded from Homebrew. The app needs macOS 11 or newer
on Apple Silicon (the Mach-O files of the arm64 package say 11.0).

`scripts/kog/macos_package_check.py` reads the load commands of every Mach-O file in the app and fails on a library outside
`/usr/lib` and `/System/Library` that is not in the bundle, on an absolute RPATH, and on a minimum macOS above 12.0. It also runs on
Linux against a DMG unpacked with 7z. `scripts/kog/macos_package_check.sh` runs it on the DMG and then starts the app with a clean
environment; in the release workflow Homebrew is moved away first. The hosted runners have no GPU, so the start stops at the
graphics, and only a dyld failure counts.

The problem this prevents: the canary DMG of 2026-11-27 was linked against `/opt/homebrew/opt/ffmpeg/lib/libavcodec.63.dylib`,
`libfreetype`, `libpng`, `openssl@4` and `opus`, which exist only on a Mac with those Homebrew packages.

## Windows

The zip holds `KoG-Client.exe` and every DLL it needs. No Visual C++ Redistributable is needed: the files are built with MinGW and
import only Windows system DLLs, the DLLs in the zip and the `msvcrt.dll` that Windows has.

`scripts/kog/windows_package_check.py` reads the import tables (also the delay-load tables) of every `.exe` and `.dll` in the zip
and fails on a DLL that is not in the zip and not a known Windows DLL, and on any `vcruntime`, `msvcp` or `msvcr` DLL that is not in the zip.
Add a DLL to the list in the script only if it is part of Windows 10.
