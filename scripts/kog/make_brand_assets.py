#!/usr/bin/env python3
"""Generate the placeholder KoG icons and menu banner (original work, replace with the final brand)."""
import re
import sys
from PIL import Image, ImageDraw, ImageFont

OUT_ICONS = "other/icons"
SIZES = (16, 32, 48, 256)
NAME = re.search(r'#define KOG_CLIENT_NAME "([^"]+)"', open("src/game/kog_version.h").read()).group(1)
ID = re.search(r'#define KOG_CLIENT_EXECUTABLE "([^"]+)"', open("src/game/kog_version.h").read()).group(1)


def icon(size):
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    radius = max(2, size // 5)
    for y in range(size):
        shade = int(30 + 40 * y / size)
        draw.line([(0, y), (size, y)], fill=(shade, shade + 20, 90 + shade // 2, 255))
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    image.putalpha(mask)
    font = ImageFont.load_default(size=int(size * 0.46))
    draw.text((size / 2, size / 2), "KoG", font=font, fill=(255, 255, 255, 255), anchor="mm")
    return image


def banner():
    image = Image.new("RGBA", (1024, 293), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default(size=190)
    draw.text((514, 150), NAME, font=font, fill=(0, 0, 0, 110), anchor="mm")
    draw.text((510, 146), NAME, font=font, fill=(255, 255, 255, 255), anchor="mm")
    return image


def main():
    for size in SIZES:
        icon(size).save(f"{OUT_ICONS}/{ID}_{size}x{size}x32.png")
    big = icon(256)
    big.save(f"{OUT_ICONS}/{ID}.ico", sizes=[(s, s) for s in SIZES])
    big.save(f"{OUT_ICONS}/{ID}.icns")
    banner().save("data/tclient/gui_logo.png")
    open(f"{OUT_ICONS}/{ID}.rc", "w").write(f'ID ICON "{ID}.ico"\n')
    open(f"other/{ID.lower()}.desktop", "w").write(
        f"[Desktop Entry]\nType=Application\nName={NAME}\nGenericName=Platform Game\nExec={ID} %u\n"
        f"Icon={ID.lower()}\nTerminal=false\nKeywords=game;multiplayer;teeworlds;kog;\nCategories=Game;ArcadeGame;\n"
        f"Comment=Launch {NAME}\n"
    )


if __name__ == "__main__":
    sys.exit(main())
