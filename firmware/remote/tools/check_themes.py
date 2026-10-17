#!/usr/bin/env python3
"""
Checks the contrast of every theme in components/ui/ui_themes.c
(docs/themes.md). Exits with 1 if any pair is below its minimum.

    python tools/check_themes.py
"""

import re
import sys
from pathlib import Path

THEMES = Path(__file__).resolve().parent.parent / "components" / "ui" / "ui_themes.c"

# (foreground role, background roles, minimum ratio). 4.5:1 is WCAG AA for
# small text; 3:1 for large text, icons and the faint third text level.
CHECKS = [
    ("text", ["bg", "surface1", "surface2", "surface3"], 4.5),
    ("text2", ["bg", "surface1", "surface2", "surface3"], 4.5),
    ("text3", ["surface1", "surface2"], 3.0),
    ("accent_text", ["surface1", "surface2", "surface3"], 3.0),
    ("on_accent", ["accent"], 4.5),
    ("on_ir", ["ir"], 4.5),
    ("ir", ["surface1", "surface2"], 3.0),
    ("bad", ["surface1", "surface2"], 3.0),
]


def luminance(rgb):
    c = [((rgb >> s) & 0xFF) / 255 for s in (16, 8, 0)]
    c = [x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def contrast(a, b):
    hi, lo = sorted([luminance(a), luminance(b)], reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def main():
    src = THEMES.read_text(encoding="utf-8")
    themes = re.findall(r'\.id = "(\w+)"(.*?)\n    \}', src, re.S)
    if not themes:
        sys.exit(f"no themes found in {THEMES}")
    failed = False
    for tid, body in themes:
        colors = {k: int(v, 16) for k, v in re.findall(r"\.(\w+) = C\(0x([0-9A-Fa-f]{6})\)", body)}
        problems = []
        for fg, bgs, need in CHECKS:
            for bg in bgs:
                if fg not in colors or bg not in colors:
                    problems.append(f"{fg} or {bg} missing")
                    continue
                ratio = contrast(colors[fg], colors[bg])
                if ratio < need:
                    problems.append(f"{fg} on {bg}: {ratio:.2f} (needs {need})")
        failed |= bool(problems)
        print(f"{tid:10s} {'OK' if not problems else '; '.join(problems)}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
