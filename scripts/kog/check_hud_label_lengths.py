#!/usr/bin/env python3
"""Report the HUD labels that do not fit their column at the HUD text size (12 units) in a language.

Unlike the start page the HUD never shrinks text to fit (that is the point of Hud::MIN_TEXT), so a label that does not fit is
cut with an ellipsis. This lists those cuts per language so that a long translation is noticed. The widths are a rough model
(0.56 em per character, 1 em for wide characters), good enough for German and Russian; the renderer measures with the font.
Usage: scripts/kog/check_hud_label_lengths.py
"""
import glob
import sys
import unicodedata

TEXT = 12.0
# source string, width of its column in units (kog_hud.h, scoreboard column 280 wide: score 50, name 100, clan 60, ping 28)
LABELS = [
    ("Score", 50.0),
    ("Time", 50.0),
    ("Name", 100.0),
    ("Clan", 60.0),
    ("Ping", 28.0),
    ("Spectators", 120.0),
    ("Game over", 150.0),
    ("Achievement unlocked", 200.0),
    ("Quest completed", 200.0),
    ("Team", 60.0),
    ("Solo", 60.0),
]


def parse(path):
    translations, source = {}, None
    for line in open(path, encoding="utf-8").read().split("\n"):
        if line.startswith("== "):
            if source is not None:
                translations.setdefault(source, line[3:])
            source = None
        elif line and not line.startswith(("#", "[")):
            source = line
    return translations


def width(text):
    return sum(1.0 if unicodedata.east_asian_width(c) in "WF" else 0.56 for c in text) * TEXT


cut = 0
for path in sorted(glob.glob("data/languages/*.txt")):
    if path.endswith("README.txt"):
        continue
    translations = parse(path)
    for label, available in LABELS:
        text = translations.get(label, label)
        if width(text) > available:
            cut += 1
            print(f"{path}: {label!r} -> {text!r} needs {width(text):.0f} of {available:.0f} units")
print(f"{cut} labels are cut")
