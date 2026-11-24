# Branding

## Where the KoG logos come from

The official logos are in `other/brand/`, unmodified, from the public repository
[brand_and_media](https://github.com/KoG-teeworlds/brand_and_media) at a pinned commit (see
`other/brand/README.md`). They are under CC BY-NC-ND 4.0, not under the zlib licence of the code. Forks that are
not KoG must replace them. The logo does not make a build an official release.

To update: change the commit in `scripts/kog/sync_brand.sh` and the hashes in `other/brand/SHA256SUMS`, run
`scripts/kog/sync_brand.sh`, then `scripts/kog/make_brand_assets.py`.

## What is used where

| Use | File | Why |
| --- | --- | --- |
| Windows icon, macOS icon, Linux icons (16 to 512) | `logo_short_black.svg` (the fox) | The fox has its own outline and reads on light and dark backgrounds, and the full logo is unreadable at 16 px |
| Menu logo `data/tclient/gui_logo.png` | `logo_long_white.svg` (full logo with name) | The menu background is dark; the logo is drawn at its own aspect ratio and not stretched |

The artwork is only scaled and converted, never edited. Generated files: `other/icons/KoG-Client*` (ICO with 16, 32,
48 and 256 px; ICNS with the standard iconset; PNG 16, 32, 48, 64, 128, 256, 512) and `data/tclient/gui_logo.png`
(768 x 448).

## Regenerating

`scripts/kog/make_brand_assets.py` from the repository root. It needs `rsvg-convert` (librsvg 2.63.4 was used,
`brew install librsvg` or `apt install librsvg2-bin`) and Pillow. On macOS the ICNS is built with `iconutil`; elsewhere
Pillow writes a reduced iconset, so regenerate on macOS before committing.
