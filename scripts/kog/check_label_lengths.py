#!/usr/bin/env python3
"""Report the start page labels that would shrink below the smallest allowed size (12 units) in a language.

The widths are a rough model (0.56 em per character, 1 em for wide characters), good enough to catch the German and Russian
cases; the real renderer measures with the font. Usage: scripts/kog/check_label_lengths.py [available width in units]
"""
import glob
import sys
import unicodedata

FONT = 14.0
SMALLEST = 12.0
AVAILABLE = float(sys.argv[1]) if len(sys.argv) > 1 else 132.0  # a half column button minus padding and icon
LABELS = ["Demos", "Editor", "Run server", "Stop server", "Settings", "Quit", "News", "Website", "Tutorial", "Not logged in"]


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


def width(text, size):
    return sum(1.0 if unicodedata.east_asian_width(c) in "WF" else 0.56 for c in text) * size


bad = 0
for path in sorted(glob.glob("data/languages/*.txt")):
    if path.endswith("README.txt"):
        continue
    translations = parse(path)
    for label in LABELS:
        text = translations.get(label, label)
        size = min(FONT, FONT * AVAILABLE / max(width(text, FONT), 1.0))
        if size < SMALLEST:
            bad += 1
            print(f"{path}: {label!r} -> {text!r} shrinks to {size:.1f}")
print(f"{bad} labels below {SMALLEST:g} units at {AVAILABLE:g} units")
sys.exit(1 if bad else 0)
