#!/usr/bin/env python3
"""
Regenerate the LVGL 9 bitmap fonts, ui_fonts.h and ui_icons.h for the
Hubmote UI component.

Run with the venv next to this script (it has fontTools):

    venv\\Scripts\\python.exe build_fonts.py                # every height + compile check
    venv\\Scripts\\python.exe build_fonts.py --no-check     # build only
    venv\\Scripts\\python.exe build_fonts.py --height 800   # one height (a new board's)

Sizes below are for a 600 px high screen (1024x600). Each screen height has
its own folder, components/ui/fonts/h<N>/, with every font scaled by N/600
under the same names; the UI's build compiles the folder for its screen
(CONFIG_HM_SCREEN_H), and the layout scales the same way (UI_S() in
ui_size.h). Without --height it rebuilds every folder there.

First-time setup (only if venv/ or node_modules/ are missing):

    py -3.12 -m venv venv && venv\\Scripts\\python.exe -m pip install fonttools
    npm install          (versions are pinned in package.json)

Edit TEXT_FONTS / ICON_FONTS / TEXT_CODEPOINTS / ICONS below to change sizes or
glyph sets, then re-run.  To move to a newer Lucide, bump "lucide-static" in
package.json, run `npm install`, and re-run (the icon codepoints are re-read
from the package, so ui_icons.h follows automatically).

Pipeline
  1. Download Sora (variable), IBM Plex Mono and their OFL licenses from the
     google/fonts repo if not already cached in src/.
  2. Make static Sora instances (wght 400/500/600/700) with
     fontTools.varLib.instancer -> build/Sora-<wght>.ttf
     (lv_font_conv only reads the default instance of a variable font).
  3. Run lv_font_conv 1.5.3 for every font (4 bpp, uncompressed).  Codepoints
     the primary font lacks are taken from the fallback font listed in the
     spec (Sora has no U+2192 ->, which comes from IBM Plex Mono of the
     closest weight; its vertical position matches Sora's math axis).
  4. Post-process each .c for LVGL 9.6 (lv_font_conv 1.5.3 predates it):
       - add .cap_height / .x_height (new lv_font_t fields in 9.6, used by
         the text leading-trim feature), computed exactly like upstream
         lv_font_conv master does: round(OS/2 sCapHeight|sxHeight * size/upem).
         Icon fonts have no OS/2 cap/x height, so both are set to the
         ascent (line_height - base_line), i.e. "trim nothing".
       - add .static_bitmap = 1 (LVGL >= 9.3: bitmaps are const and
         uncompressed, so they can be drawn in place without a copy).
     Both are wrapped in the same version guards upstream uses, so the files
     still build against older LVGL versions.
  5. Write include/ui_fonts.h and include/ui_icons.h, copy license files.
  6. Compile every generated .c and a test .c that includes both headers with
     the ESP32-S3 GCC against LVGL 9.6 (-Wall -Werror).

preview_fonts.py (same folder) decodes the generated .c bitmaps into
build/preview.png for a visual check.
"""

import argparse
import json
import math
import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

# --------------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------------
HERE = Path(__file__).resolve().parent
SRC = HERE / "src"            # downloaded source fonts + licenses
BUILD = HERE / "build"        # static instances, raw lv_font_conv output, test objs
NODE_MODULES = HERE / "node_modules"
LV_FONT_CONV = NODE_MODULES / "lv_font_conv" / "lv_font_conv.js"
LUCIDE_DIR = NODE_MODULES / "lucide-static"

PROJECT = HERE.parent.parent  # firmware/remote
UI_DIR = PROJECT / "components" / "ui"
FONTS_DIR = UI_DIR / "fonts"         # licenses; one folder of fonts per screen height
FONTS_OUT = FONTS_DIR / "h600"       # set by --height
INCLUDE_OUT = UI_DIR / "include"
DESIGN_HEIGHT = 600                  # the sizes below are for this screen height
HEIGHT = DESIGN_HEIGHT


def scaled(size):
    """A design size (for 600 px high) at the screen height being built."""
    return max(8, int(size * HEIGHT / DESIGN_HEIGHT + 0.5))

# LVGL as downloaded by the firmware build (run a build once first).
LVGL_ROOT = PROJECT / "managed_components" / "lvgl__lvgl"
# The ESP-IDF 5.4.4 cross compiler, wherever its version folder is.
GCC = next(iter(sorted(Path(r"C:\Espressif\tools\xtensa-esp-elf").glob(
    "*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc.exe"), reverse=True)), None)

GF = "https://raw.githubusercontent.com/google/fonts/main/ofl"
DOWNLOADS = {
    "Sora[wght].ttf": f"{GF}/sora/Sora%5Bwght%5D.ttf",
    "Sora-OFL.txt": f"{GF}/sora/OFL.txt",
    "IBMPlexMono-Regular.ttf": f"{GF}/ibmplexmono/IBMPlexMono-Regular.ttf",
    "IBMPlexMono-Medium.ttf": f"{GF}/ibmplexmono/IBMPlexMono-Medium.ttf",
    "IBMPlexMono-SemiBold.ttf": f"{GF}/ibmplexmono/IBMPlexMono-SemiBold.ttf",
    "IBMPlexMono-Bold.ttf": f"{GF}/ibmplexmono/IBMPlexMono-Bold.ttf",
    "IBMPlexMono-OFL.txt": f"{GF}/ibmplexmono/OFL.txt",
}

# License files copied next to the generated fonts: (source, name in fonts/)
LICENSES = [
    (SRC / "Sora-OFL.txt", "OFL-Sora.txt"),
    (SRC / "IBMPlexMono-OFL.txt", "OFL-IBMPlexMono.txt"),
    (LUCIDE_DIR / "LICENSE", "LICENSE-Lucide.txt"),
]

# --------------------------------------------------------------------------
# Font specs
# --------------------------------------------------------------------------
SORA_WEIGHTS = (400, 500, 600, 700)

# IBM Plex Mono weight used for glyphs Sora doesn't have (currently only
# U+2192).  Chosen so the stroke thickness is closest to Sora's at that weight.
PLEX_MONO_FOR_SORA = {
    400: "IBMPlexMono-Regular.ttf",
    500: "IBMPlexMono-Medium.ttf",
    600: "IBMPlexMono-SemiBold.ttf",
    700: "IBMPlexMono-Bold.ttf",
}

TEXT_CODEPOINTS = list(range(0x20, 0x7F)) + [
    0x00B0,  # degree sign
    0x00B7,  # middle dot
    0x00D7,  # multiplication sign
    0x2013,  # en dash
    0x2014,  # em dash
    0x2019,  # right single quote / apostrophe
    0x201C,  # left double quote
    0x201D,  # right double quote
    0x2026,  # ellipsis
    0x2192,  # rightwards arrow
    0x2212,  # minus sign
]

# (symbol, primary font, fallback font or None, px size, description)
def _sora(w):
    return BUILD / f"Sora-{w}.ttf"

TEXT_FONTS = [
    ("font_sora_11",   _sora(400), SRC / PLEX_MONO_FOR_SORA[400], 11, "Sora 400"),
    ("font_sora_12",   _sora(400), SRC / PLEX_MONO_FOR_SORA[400], 12, "Sora 400"),
    ("font_sora_13m",  _sora(500), SRC / PLEX_MONO_FOR_SORA[500], 13, "Sora 500"),
    ("font_sora_15m",  _sora(500), SRC / PLEX_MONO_FOR_SORA[500], 15, "Sora 500"),
    ("font_sora_16sb", _sora(600), SRC / PLEX_MONO_FOR_SORA[600], 16, "Sora 600"),
    ("font_sora_18b",  _sora(700), SRC / PLEX_MONO_FOR_SORA[700], 18, "Sora 700"),
    ("font_sora_20sb", _sora(600), SRC / PLEX_MONO_FOR_SORA[600], 20, "Sora 600"),
    ("font_sora_22m",  _sora(500), SRC / PLEX_MONO_FOR_SORA[500], 22, "Sora 500"),
    ("font_sora_24m",  _sora(500), SRC / PLEX_MONO_FOR_SORA[500], 24, "Sora 500"),
    ("font_sora_26m",  _sora(500), SRC / PLEX_MONO_FOR_SORA[500], 26, "Sora 500"),
    ("font_mono_11",   SRC / "IBMPlexMono-Regular.ttf", None, 11, "IBM Plex Mono 400"),
    ("font_mono_12",   SRC / "IBMPlexMono-Regular.ttf", None, 12, "IBM Plex Mono 400"),
]

# Big numbers (temperatures): digits, minus signs, degree sign and space only,
# so a large size stays small in flash.
NUMBER_CODEPOINTS = [0x20, 0x2D, *range(0x30, 0x3A), 0x00B0, 0x2212]
NUMBER_FONTS = [
    ("font_sora_88n", _sora(600), 88, "Sora 600, digits and degree sign only"),
]

ICON_SIZES = (16, 20, 24, 28)
ICON_FONTS = [(f"font_icons_{s}", s) for s in ICON_SIZES]

# (ICON_ name, Lucide icon name, note) -- order is the order in ui_icons.h
ICONS = [
    ("POWER",            "power",              ""),
    ("BACK",             "undo-2",             "back / return"),
    ("HOME",             "house",              ""),
    ("PLAY",             "play",               ""),
    ("PAUSE",            "pause",              ""),
    ("PLUS",             "plus",               ""),
    ("MINUS",            "minus",              ""),
    ("MUTE",             "volume-x",           ""),
    ("VOLUME",           "volume-2",           ""),
    ("SEARCH",           "search",             ""),
    ("CAPTIONS",         "captions",           ""),
    ("WIFI",             "wifi",               ""),
    ("WIFI_OFF",         "wifi-off",           ""),
    ("BATTERY",          "battery-medium",     ""),
    ("BATTERY_LOW",      "battery-low",        ""),
    ("BATTERY_CHARGING", "battery-charging",   ""),
    ("CHEVRON_UP",       "chevron-up",         ""),
    ("CHEVRON_DOWN",     "chevron-down",       ""),
    ("CHEVRON_LEFT",     "chevron-left",       ""),
    ("CHEVRON_RIGHT",    "chevron-right",      ""),
    ("REWIND",           "rotate-ccw",         "back 10 s"),
    ("FORWARD",          "rotate-cw",          "forward 10 s"),
    ("SKIP_BACK",        "skip-back",          ""),
    ("SKIP_FORWARD",     "skip-forward",       ""),
    ("SETTINGS",         "sliders-horizontal", ""),
    ("INPUTS",           "monitor",            ""),
    ("REMOTE",           "calculator",         "handheld remote: Lucide has no remote icon; tall body + keypad grid"),
    ("TV",               "tv",                 ""),
    ("STREAMER",         "cast",               "streaming box"),
    ("HDMI_SWITCH",      "hdmi-port",          "HDMI switch box"),
    ("CHECK",            "check",              ""),
    ("BACKSPACE",        "delete",             ""),
    ("SHIFT",            "arrow-big-up",       ""),
    ("KEYBOARD",         "keyboard",           ""),
    ("KEYBOARD_HIDE",    "keyboard-off",       ""),
    ("IR",               "radio",              "infrared signal"),
    ("EXIT",             "log-out",            ""),
    ("ADD",              "circle-plus",        ""),
    ("CLOSE",            "x",                  ""),
    ("REFRESH",          "refresh-cw",         ""),
    ("LOCK",             "lock",               ""),
    ("INFO",             "info",               ""),
    ("LINK",             "link",               ""),
    ("ALERT",            "triangle-alert",     ""),
    ("GEAR",             "settings",           "the remote's own settings"),
    ("PALETTE",          "palette",            "Settings > Appearance (themes)"),
    ("GRID",             "layout-grid",        "Home tab: modules"),
    ("MAP_PIN",          "map-pin",            "location"),
    ("CLOCK",            "clock",              "time zone"),
    # Weather (WMO codes map to these in modules/weather)
    ("W_SUN",            "sun",                ""),
    ("W_MOON",           "moon",               ""),
    ("W_CLOUD",          "cloud",              ""),
    ("W_CLOUDY",         "cloudy",             "overcast"),
    ("W_CLOUD_SUN",      "cloud-sun",          "partly cloudy, day"),
    ("W_CLOUD_MOON",     "cloud-moon",         "partly cloudy, night"),
    ("W_DRIZZLE",        "cloud-drizzle",      ""),
    ("W_RAIN",           "cloud-rain",         ""),
    ("W_HEAVY_RAIN",     "cloud-rain-wind",    ""),
    ("W_SUN_RAIN",       "cloud-sun-rain",     "showers, day"),
    ("W_MOON_RAIN",      "cloud-moon-rain",    "showers, night"),
    ("W_SNOW",           "cloud-snow",         ""),
    ("W_HAIL",           "cloud-hail",         ""),
    ("W_STORM",          "cloud-lightning",    ""),
    ("W_FOG",            "cloud-fog",          ""),
    ("W_SNOWFLAKE",      "snowflake",          ""),
    ("W_WIND",           "wind",               ""),
    ("W_DROPLET",        "droplet",            "humidity"),
    ("W_SUNRISE",        "sunrise",            ""),
    ("W_SUNSET",         "sunset",             ""),
    ("W_UMBRELLA",       "umbrella",           "chance of rain"),
]

# One large icon font with only the weather conditions (the Weather hero icon).
BIG_ICON_FONTS = [
    ("font_weather_96", 96, ["W_SUN", "W_MOON", "W_CLOUD", "W_CLOUDY", "W_CLOUD_SUN", "W_CLOUD_MOON",
                             "W_DRIZZLE", "W_RAIN", "W_HEAVY_RAIN", "W_SUN_RAIN", "W_MOON_RAIN",
                             "W_SNOW", "W_HAIL", "W_STORM", "W_FOG"]),
]

BPP = 4

# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------
def log(msg):
    print(msg, flush=True)


def js_round(x):
    """Math.round() semantics (round half up), as used by lv_font_conv."""
    return int(math.floor(x + 0.5))


def run(cmd, cwd=None):
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"command failed ({r.returncode}): {' '.join(map(str, cmd))}")
    return r.stdout + r.stderr


def ensure_sources():
    SRC.mkdir(exist_ok=True)
    for name, url in DOWNLOADS.items():
        dst = SRC / name
        if not dst.exists():
            log(f"download {url}")
            urllib.request.urlretrieve(url, dst)
    if not LV_FONT_CONV.exists() or not (LUCIDE_DIR / "font" / "lucide.ttf").exists():
        log("npm install")
        run(["npm.cmd" if sys.platform == "win32" else "npm", "install", "--no-fund", "--no-audit"], cwd=HERE)


def make_sora_instances():
    BUILD.mkdir(exist_ok=True)
    var = SRC / "Sora[wght].ttf"
    for w in SORA_WEIGHTS:
        dst = _sora(w)
        if dst.exists() and dst.stat().st_mtime > var.stat().st_mtime:
            continue
        log(f"instance Sora wght={w}")
        vf = TTFont(var)
        inst = instancer.instantiateVariableFont(vf, {"wght": w}, updateFontNames=False)
        inst.save(dst)


def cmap_of(path):
    return TTFont(path, lazy=True).getBestCmap()


def ranges_arg(cps):
    """Compact list of codepoints -> lv_font_conv -r argument."""
    cps = sorted(set(cps))
    parts, i = [], 0
    while i < len(cps):
        j = i
        while j + 1 < len(cps) and cps[j + 1] == cps[j] + 1:
            j += 1
        parts.append(f"0x{cps[i]:X}" if i == j else f"0x{cps[i]:X}-0x{cps[j]:X}")
        i = j + 1
    return ",".join(parts)


def lv_font_conv(name, size, font_ranges):
    """font_ranges: list of (font path, [codepoints]). Runs in the font's
    directory per font so the 'Opts:' comment carries no absolute paths."""
    out_dir = BUILD / "raw"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{name}.c"
    # all fonts are staged in one dir and referenced by bare file name, so the
    # "Opts:" comment lv_font_conv writes into the .c has no absolute paths
    stage = BUILD / "stage"
    stage.mkdir(exist_ok=True)
    cmd = ["node", LV_FONT_CONV]
    for font, cps in font_ranges:
        dst = stage / font.name
        if not dst.exists() or dst.stat().st_mtime < font.stat().st_mtime:
            shutil.copy2(font, dst)
        cmd += ["--font", font.name, "-r", ranges_arg(cps)]
    cmd += ["--size", size, "--bpp", BPP, "--format", "lvgl", "--no-compress",
            "--lv-include", "lvgl.h", "--lv-font-name", name,
            "-o", f"{name}.c"]
    if out.exists():
        out.unlink()
    run(cmd, cwd=stage)
    shutil.move(stage / f"{name}.c", out)
    return out


def font_metrics(path, size):
    f = TTFont(path, lazy=True)
    upem = f["head"].unitsPerEm
    os2 = f["OS/2"]
    cap = js_round(getattr(os2, "sCapHeight", 0) * size / upem)
    xh = js_round(getattr(os2, "sxHeight", 0) * size / upem)
    return cap, xh


POST_TAG = "Post-processed by build_fonts.py for LVGL 9.6"


def postprocess(src_c, dst_c, cap, xh):
    """Add LVGL 9.3+/9.6 lv_font_t fields lv_font_conv 1.5.3 doesn't emit."""
    t = src_c.read_text(encoding="utf-8")
    if POST_TAG in t:
        raise SystemExit(f"{src_c} already post-processed?")

    m_lh = re.search(r"^\s*\.line_height = (\d+),", t, re.M)
    m_bl = re.search(r"^(\s*)\.base_line = (-?\d+),[^\n]*\n", t, re.M)
    m_dsc = re.search(r"^\s*\.dsc = &font_dsc,[^\n]*\n", t, re.M)
    if not (m_lh and m_bl and m_dsc):
        raise SystemExit(f"{src_c}: unexpected lv_font_conv output layout")
    line_height, base_line = int(m_lh.group(1)), int(m_bl.group(2))
    if cap <= 0 or xh <= 0:  # icon font: no meaningful cap/x height -> no trim
        cap = xh = line_height - base_line

    ind = m_bl.group(1)
    cap_block = (
        "#if LV_VERSION_CHECK(9, 6, 0) || LVGL_VERSION_MAJOR >= 10\n"
        f"{ind}.cap_height = {cap},           /*Cap height of the font*/\n"
        f"{ind}.x_height = {xh},               /*x-height of the font*/\n"
        "#endif\n"
    )
    static_block = (
        "#if LV_VERSION_CHECK(9, 3, 0) || LVGL_VERSION_MAJOR >= 10\n"
        f"{ind}.static_bitmap = 1,    /*Bitmaps are stored as const so they are always static if not compressed */\n"
        "#endif\n"
    )
    # insert from the end so earlier offsets stay valid
    t = t[:m_dsc.start()] + static_block + t[m_dsc.start():]
    t = t[:m_bl.end()] + cap_block + t[m_bl.end():]
    # note in the header comment
    t = re.sub(r"^( \* Opts: [^\n]*\n)",
               lambda m: m.group(1) + f" * {POST_TAG} (added .cap_height, .x_height, .static_bitmap)\n",
               t, count=1, flags=re.M)
    dst_c.write_text(t, encoding="utf-8", newline="\n")
    return line_height, base_line, cap, xh


# --------------------------------------------------------------------------
# Lucide
# --------------------------------------------------------------------------
def lucide_info():
    version = json.loads((LUCIDE_DIR / "package.json").read_text(encoding="utf-8"))["version"]
    codepoints = json.loads((LUCIDE_DIR / "font" / "codepoints.json").read_text(encoding="utf-8"))
    ttf = LUCIDE_DIR / "font" / "lucide.ttf"
    cmap = cmap_of(ttf)
    resolved = []
    for icon, lucide, note in ICONS:
        if lucide not in codepoints:
            raise SystemExit(f"Lucide {version} has no icon '{lucide}' (ICON_{icon})")
        cp = codepoints[lucide]
        if cp not in cmap:
            raise SystemExit(f"'{lucide}' U+{cp:04X} is not in lucide.ttf")
        resolved.append((icon, lucide, note, cp))
    return version, ttf, resolved


def utf8_escaped(cp):
    return "".join(f"\\x{b:02X}" for b in chr(cp).encode("utf-8"))


def write_ui_icons_h(version, resolved):
    w = max(len(i) for i, *_ in resolved)
    lines = [
        "/*",
        " * Icon strings for the font_icons_16/20/24/28 fonts (see ui_fonts.h);",
        " * the ICON_W_* conditions also work with font_weather_96.",
        f" * Icon set: Lucide, npm lucide-static {version} (https://lucide.dev), ISC license,",
        " * see fonts/LICENSE-Lucide.txt.",
        " *",
        " * Each macro is the UTF-8 encoding of the icon's codepoint in the Lucide",
        " * icon font, e.g. lv_label_set_text(label, ICON_POWER) with the label's",
        " * text font set to one of the font_icons_* fonts.",
        " *",
        " * Generated by build_fonts.py -- do not edit by hand.",
        " */",
        "#pragma once",
        "",
    ]
    for icon, lucide, note, cp in resolved:
        comment = f"lucide: {lucide}" + (f" ({note})" if note else "")
        macro = f"ICON_{icon}".ljust(w + 5)
        lines.append(f'#define {macro} "{utf8_escaped(cp)}"  /* U+{cp:04X} {comment} */')
    lines.append("")
    text = "\n".join(lines)
    text.encode("ascii")  # must stay pure ASCII
    (INCLUDE_OUT / "ui_icons.h").write_text(text, encoding="ascii", newline="\n")


def write_ui_fonts_h(entries):
    w = max(len(n) for n, _ in entries)
    extra = ", ".join(f"U+{c:04X}" for c in TEXT_CODEPOINTS if c > 0x7E)
    lines = [
        "/*",
        f" * LVGL bitmap fonts for the remote UI ({BPP} bpp, sources in ../fonts/h<height>/).",
        " * Sizes below are for a 600 px high screen; the build uses the folder for",
        " * its screen height (CONFIG_HM_SCREEN_H), where every font is scaled alike.",
        " * Text fonts: U+0020-U+007E plus",
        f" *   {extra}.",
        " * Icon fonts: only the Lucide glyphs listed in ui_icons.h.",
        " * font_sora_88n: digits, '-', U+2212, U+00B0 and space only.",
        " *",
        " * Generated by build_fonts.py -- do not edit by hand.",
        " */",
    ]
    lines += [
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    for name, desc in entries:
        lines.append(f"LV_FONT_DECLARE({name});".ljust(w + 19) + f"/* {desc} */")
    lines += ["", "#ifdef __cplusplus", "}", "#endif", ""]
    text = "\n".join(lines)
    text.encode("ascii")
    (INCLUDE_OUT / "ui_fonts.h").write_text(text, encoding="ascii", newline="\n")


# --------------------------------------------------------------------------
# Compile check
# --------------------------------------------------------------------------
def compile_check(c_files, resolved):
    if GCC is None or not LVGL_ROOT.exists():
        sys.exit("compile check needs the ESP32-S3 GCC under C:\\Espressif\\tools and LVGL in "
                 f"{LVGL_ROOT} (build the firmware once); or pass --no-check")
    objdir = BUILD / "obj"
    objdir.mkdir(parents=True, exist_ok=True)
    base = [GCC, "-c", "-DLV_CONF_SKIP", "-DLV_LVGL_H_INCLUDE_SIMPLE",
            "-I", LVGL_ROOT, "-I", LVGL_ROOT / "include",
            "-Wall", "-Werror", "-O2"]

    test_c = BUILD / "ui_headers_test.c"
    body = ['#include "ui_fonts.h"', '#include "ui_icons.h"', "",
            "const lv_font_t * const all_fonts[] = {"]
    body += [f"    &{p.stem}," for p in c_files]
    body += ["};", "", "const char * const all_icons[] = {"]
    body += [f"    ICON_{icon}," for icon, *_ in resolved]
    body += ["};", "",
             "/* LVGL 9.6 font struct fields must be populated by the generated fonts */",
             "int check_fields(void) {",
             "    return font_sora_12.cap_height + font_sora_12.x_height + font_sora_12.static_bitmap;",
             "}", ""]
    for icon, _l, _n, cp in resolved:
        enc = chr(cp).encode("utf-8")
        body.append(f'_Static_assert(sizeof(ICON_{icon}) == {len(enc) + 1}, "ICON_{icon}");')
    body.append("")
    test_c.write_text("\n".join(body), encoding="ascii", newline="\n")

    results = []
    for src in list(c_files) + [test_c]:
        obj = objdir / (src.stem + ".o")
        extra = ["-I", INCLUDE_OUT] if src == test_c else []
        r = subprocess.run([str(x) for x in base + extra + [src, "-o", obj]],
                           capture_output=True, text=True)
        ok = r.returncode == 0 and not r.stderr.strip()
        results.append((src.name, ok, (r.stdout + r.stderr).strip()))
    return results


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-check", action="store_true", help="skip the GCC compile check")
    ap.add_argument("--height", type=int, action="append",
                    help="a screen height to build fonts for (repeatable); default: every fonts/h<N> folder")
    args = ap.parse_args()
    heights = args.height or sorted(int(d.name[1:]) for d in FONTS_DIR.glob("h*") if d.name[1:].isdigit())

    ensure_sources()
    make_sora_instances()
    INCLUDE_OUT.mkdir(parents=True, exist_ok=True)
    lvconv_ver = run(["node", LV_FONT_CONV, "--version"]).strip()
    lucide_ver, lucide_ttf, resolved = lucide_info()
    log(f"lv_font_conv {lvconv_ver}, lucide-static {lucide_ver}")

    for height in heights or [DESIGN_HEIGHT]:
        build_height(height, lucide_ver, lucide_ttf, resolved, not args.no_check)


def build_height(height, lucide_ver, lucide_ttf, resolved, check):
    """Every font for one screen height, into fonts/h<height>/."""
    global HEIGHT, FONTS_OUT
    HEIGHT = height
    FONTS_OUT = FONTS_DIR / f"h{HEIGHT}"
    FONTS_OUT.mkdir(parents=True, exist_ok=True)
    log(f"fonts for {HEIGHT} px high screens: {FONTS_OUT}")

    written, header_entries, summary = [], [], []

    for name, primary, fallback, size, desc in TEXT_FONTS:
        pcmap = cmap_of(primary)
        have = [c for c in TEXT_CODEPOINTS if c in pcmap]
        missing = [c for c in TEXT_CODEPOINTS if c not in pcmap]
        font_ranges = [(primary, have)]
        if missing:
            if fallback is None or any(c not in cmap_of(fallback) for c in missing):
                raise SystemExit(f"{name}: no glyph for {[hex(c) for c in missing]}")
            font_ranges.append((fallback, missing))
        raw = lv_font_conv(name, scaled(size), font_ranges)
        cap, xh = font_metrics(primary, scaled(size))
        dst = FONTS_OUT / f"{name}.c"
        lh, bl, cap, xh = postprocess(raw, dst, cap, xh)
        fb = f" (+{', '.join(f'U+{c:04X}' for c in missing)} from {fallback.stem})" if missing else ""
        header_entries.append((name, f"{desc}, {size} px{fb}"))
        summary.append(f"{name}: line_height={lh} base_line={bl} cap={cap} x={xh}{fb}")
        written.append(dst)

    for name, primary, size, desc in NUMBER_FONTS:
        pcmap = cmap_of(primary)
        missing = [c for c in NUMBER_CODEPOINTS if c not in pcmap]
        if missing:
            raise SystemExit(f"{name}: no glyph for {[hex(c) for c in missing]}")
        raw = lv_font_conv(name, scaled(size), [(primary, NUMBER_CODEPOINTS)])
        cap, xh = font_metrics(primary, scaled(size))
        dst = FONTS_OUT / f"{name}.c"
        lh, bl, cap, xh = postprocess(raw, dst, cap, xh)
        header_entries.append((name, f"{desc}, {size} px"))
        summary.append(f"{name}: line_height={lh} base_line={bl} cap={cap} x={xh}")
        written.append(dst)

    icon_cps = [cp for *_, cp in resolved]
    for name, size in ICON_FONTS:
        raw = lv_font_conv(name, scaled(size), [(lucide_ttf, icon_cps)])
        dst = FONTS_OUT / f"{name}.c"
        lh, bl, cap, xh = postprocess(raw, dst, 0, 0)
        header_entries.append((name, f"Lucide icons, {size} px"))
        summary.append(f"{name}: line_height={lh} base_line={bl}")
        written.append(dst)

    by_name = {icon: cp for icon, _l, _n, cp in resolved}
    for name, size, icons in BIG_ICON_FONTS:
        raw = lv_font_conv(name, scaled(size), [(lucide_ttf, [by_name[i] for i in icons])])
        dst = FONTS_OUT / f"{name}.c"
        lh, bl, cap, xh = postprocess(raw, dst, 0, 0)
        header_entries.append((name, f"Lucide weather icons, {size} px"))
        summary.append(f"{name}: line_height={lh} base_line={bl}")
        written.append(dst)

    write_ui_fonts_h(header_entries)
    write_ui_icons_h(lucide_ver, resolved)
    for src, dst_name in LICENSES:
        shutil.copyfile(src, FONTS_DIR / dst_name)

    for s in summary:
        log("  " + s)

    if check:
        results = compile_check(written, resolved)
        bad = [r for r in results if not r[1]]
        for n, ok, out in results:
            log(f"  compile {'OK  ' if ok else 'FAIL'} {n}" + (f"\n{out}" if out else ""))
        if bad:
            raise SystemExit(f"{len(bad)} file(s) failed the compile check")
        log(f"compile check passed ({len(results)} files, LVGL at {LVGL_ROOT})")


if __name__ == "__main__":
    main()
