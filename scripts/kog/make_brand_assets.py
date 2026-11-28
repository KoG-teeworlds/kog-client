#!/usr/bin/env python3
"""Scale the official KoG logos in other/brand/ to the icon and banner files the client needs.

Needs rsvg-convert (librsvg 2.63.4 was used) and Pillow. The artwork itself is never edited, only scaled.
Icons: logo_short_black.svg (the fox carries its own outline, so it reads on light and dark).
Menu banner: logo_long_white.svg (the menu background is dark), logo_long_black.svg for the light theme.
"""
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
from PIL import Image

LOGO = "other/brand/logo"
OUT_ICONS = "other/icons"
SIZES = (16, 32, 48, 64, 128, 256, 512)
ICO_SIZES = (16, 32, 48, 256)
ICNS_SIZES = (16, 32, 64, 128, 256, 512, 1024)
BANNER_WIDTH = 768
NAME = re.search(r'#define KOG_CLIENT_NAME "([^"]+)"', open("src/game/kog_version.h").read()).group(1)
ID = re.search(r'#define KOG_CLIENT_EXECUTABLE "([^"]+)"', open("src/game/kog_version.h").read()).group(1)


def render(svg, width):
    png = subprocess.run(["rsvg-convert", "-w", str(width), f"{LOGO}/{svg}"], check=True, capture_output=True).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def write_icns(path):
    """Full standard iconset through iconutil on macOS, Pillow's reduced set elsewhere."""
    if shutil.which("iconutil"):
        with tempfile.TemporaryDirectory() as tmp:
            iconset = os.path.join(tmp, "KoG.iconset")
            os.mkdir(iconset)
            for base in (16, 32, 128, 256, 512):
                render("logo_short_black.svg", base).save(f"{iconset}/icon_{base}x{base}.png")
                render("logo_short_black.svg", base * 2).save(f"{iconset}/icon_{base}x{base}@2x.png")
            subprocess.run(["iconutil", "-c", "icns", iconset, "-o", path], check=True)
        return
    images = [render("logo_short_black.svg", s) for s in ICNS_SIZES]
    images[-1].save(path, append_images=images[:-1])


def main():
    for size in SIZES:
        render("logo_short_black.svg", size).save(f"{OUT_ICONS}/{ID}_{size}x{size}x32.png")
    ico = [render("logo_short_black.svg", s) for s in ICO_SIZES]
    ico[-1].save(f"{OUT_ICONS}/{ID}.ico", sizes=[(s, s) for s in ICO_SIZES], append_images=ico[:-1])
    write_icns(f"{OUT_ICONS}/{ID}.icns")
    render("logo_long_white.svg", BANNER_WIDTH).save("data/tclient/gui_logo.png", optimize=True)
    render("logo_long_black.svg", BANNER_WIDTH).save("data/tclient/gui_logo_light.png", optimize=True)
    open(f"{OUT_ICONS}/{ID}.rc", "w").write(f'ID ICON "{ID}.ico"\n')
    open(f"other/{ID.lower()}.desktop", "w").write(
        f"[Desktop Entry]\nType=Application\nName={NAME}\nGenericName=Platform Game\nExec={ID} %u\n"
        f"Icon={ID.lower()}\nTerminal=false\nKeywords=game;multiplayer;teeworlds;kog;\nCategories=Game;ArcadeGame;\n"
        f"Comment=Launch {NAME}\n"
    )


if __name__ == "__main__":
    sys.exit(main())
