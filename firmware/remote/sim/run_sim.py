#!/usr/bin/env python3
"""
Builds the UI simulator for each screen size, draws every screen in each
theme, and saves them as PNGs under sim/out/<W>x<H>/<theme>/ (sim/README.md).

    python sim/run_sim.py                              # every board's size, Midnight and Daylight
    python sim/run_sim.py --size 800x480 --theme dusk
    python sim/run_sim.py --cjson DIR --lvgl DIR       # sources outside the firmware tree

Needs CMake, Ninja and a C compiler (gcc or clang). LVGL comes from the
firmware's managed_components (build the firmware once) or --lvgl; cJSON
from --cjson (CI clones it) or ESP-IDF ($IDF_PATH).
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REMOTE = HERE.parent
THEMES = ["midnight", "daylight"]


def board_sizes() -> list:
    """Every board's screen size, from the HM_SCREEN_W/H defaults in
    main/Kconfig.projbuild, largest first."""
    kconfig = (REMOTE / "main" / "Kconfig.projbuild").read_text(encoding="utf-8")
    dims = {}
    for axis in ("W", "H"):
        block = re.search(rf"config HM_SCREEN_{axis}\n(.*?)\n\n", kconfig, re.S).group(1)
        for value, boards in re.findall(r"default (\d+) if (.+)", block):
            for board in re.findall(r"HM_BOARD_\w+", boards):
                dims.setdefault(board, {})[axis] = int(value)
    sizes = {(d["W"], d["H"]) for d in dims.values()}
    return [f"{w}x{h}" for w, h in sorted(sizes, reverse=True)]


def bmp_to_png(bmp: Path, png: Path):
    data = bmp.read_bytes()
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    row = (w * 3 + 3) & ~3
    raw = bytearray()
    for y in range(h):
        src = off + (h - 1 - y) * row  # BMP rows run bottom-up
        line = data[src:src + w * 3]
        rgb = bytearray(w * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = line[2::3], line[1::3], line[0::3]  # BGR -> RGB
        raw.append(0)  # PNG filter: none
        raw += rgb

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png.write_bytes(b"\x89PNG\r\n\x1a\n"
                    + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
                    + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--size", action="append", help="WxH (repeatable); default: every board's")
    ap.add_argument("--theme", action="append", help="theme id (repeatable); default: " + ", ".join(THEMES))
    ap.add_argument("--lvgl", default=str(REMOTE / "managed_components" / "lvgl__lvgl"))
    ap.add_argument("--cjson", help="cJSON sources; default: $IDF_PATH/components/json/cJSON")
    ap.add_argument("--out", default=str(HERE / "out"))
    ap.add_argument("--build", default=str(REMOTE / "build-sim"))
    ap.add_argument("--keep-bmp", action="store_true", help="keep the BMPs (exact pixels) next to the PNGs")
    args = ap.parse_args()

    cjson = args.cjson or (Path(os.environ["IDF_PATH"]) / "components" / "json" / "cJSON"
                           if os.environ.get("IDF_PATH") else None)
    if not cjson or not (Path(cjson) / "cJSON.c").is_file():
        sys.exit("cJSON not found: pass --cjson DIR, or set IDF_PATH")

    # A module made from tools/module_template, as new_module.py makes it:
    # compiled with the simulator (not linked in), so a change to the module
    # API can't leave the template behind.
    template = Path(args.build) / "template"
    if template.exists():
        shutil.rmtree(template)
    subprocess.run([sys.executable, str(REMOTE / "tools" / "new_module.py"), "tmpl_check", "Template check",
                    "--into", str(template), "--quiet"], check=True)

    for size in args.size or board_sizes():
        w, h = size.lower().split("x")
        build = Path(args.build) / size
        subprocess.run(["cmake", "-S", str(HERE), "-B", str(build), "-G", "Ninja",
                        f"-DLVGL_DIR={args.lvgl}", f"-DCJSON_DIR={cjson}", f"-DSIM_W={w}", f"-DSIM_H={h}",
                        f"-DSIM_TEMPLATE_C={(template / 'tmpl_check' / 'tmpl_check.c').as_posix()}"],
                       check=True, stdout=subprocess.DEVNULL)
        built = subprocess.run(["cmake", "--build", str(build)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace")
        if built.returncode:
            sys.exit(built.stdout + f"\nthe {size} build failed")
        exe = build / ("hubmote_sim.exe" if os.name == "nt" else "hubmote_sim")
        for theme in args.theme or THEMES:
            out = Path(args.out) / size / theme
            if out.exists():
                shutil.rmtree(out)
            out.mkdir(parents=True)
            subprocess.run([str(exe), str(out), theme], check=True)
            for bmp in sorted(out.glob("*.bmp")):
                bmp_to_png(bmp, bmp.with_suffix(".png"))
                if not args.keep_bmp:
                    bmp.unlink()
            print(f"{size} {theme}: {out}")


if __name__ == "__main__":
    main()
