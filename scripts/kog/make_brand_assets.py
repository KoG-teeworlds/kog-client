#!/usr/bin/env python3
"""Generate the placeholder KoG icons and menu banner (original work, replace with the final brand)."""
import sys
from PIL import Image, ImageDraw, ImageFont

OUT_ICONS = "other/icons"
SIZES = (16, 32, 48, 256)


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
    draw.text((514, 150), "KoG Client", font=font, fill=(0, 0, 0, 110), anchor="mm")
    draw.text((510, 146), "KoG Client", font=font, fill=(255, 255, 255, 255), anchor="mm")
    return image


def main():
    for size in SIZES:
        icon(size).save(f"{OUT_ICONS}/KoG-Client_{size}x{size}x32.png")
    big = icon(256)
    big.save(f"{OUT_ICONS}/KoG-Client.ico", sizes=[(s, s) for s in SIZES])
    big.save(f"{OUT_ICONS}/KoG-Client.icns")
    banner().save("data/tclient/gui_logo.png")


if __name__ == "__main__":
    sys.exit(main())
