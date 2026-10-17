r"""Render every glyph of the generated LVGL .c fonts to a PNG contact sheet
(decodes the uncompressed 4 bpp bitmaps straight from the C source), so the
output of build_fonts.py can be eyeballed.  Usage: venv\Scripts\python.exe preview_fonts.py"""
import re
from pathlib import Path
from PIL import Image, ImageDraw

FONTS = Path(__file__).resolve().parent.parent.parent / "components" / "ui" / "fonts"
OUT = Path(__file__).resolve().parent / "build" / "preview.png"
SCALE = 2

def load(c):
    t = c.read_text(encoding="utf-8")
    bm = t[t.index("glyph_bitmap[] = {"):]
    bm = bm[:bm.index("};")]
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{1,2})", bm))
    gl = re.findall(r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}", t)
    lh = int(re.search(r"\.line_height = (\d+)", t).group(1))
    bl = int(re.search(r"\.base_line = (\d+)", t).group(1))
    return data, [tuple(map(int, g)) for g in gl[1:]], lh, bl

def render(c, wrap=560):
    data, glyphs, lh, bl = load(c)
    step = lambda g: max(g[1] // 16, g[2] + max(g[4], 0)) + 2
    lines, cur, w = [], [], 2
    for g in glyphs:
        if w + step(g) > wrap and cur:
            lines.append(cur); cur, w = [], 2
        cur.append(g); w += step(g)
    lines.append(cur)
    img = Image.new("L", (wrap, (lh + 4) * len(lines)), 255)
    for li, line in enumerate(lines):
        x, y0 = 2, li * (lh + 4)
        for g in line:
            bi, adv, bw, bh, ox, oy = g
            top = y0 + 2 + (lh - bl) - (bh + oy)
            for i in range(bw * bh):
                byte = data[bi + i // 2]
                v = (byte >> 4) if i % 2 == 0 else (byte & 0xF)
                if v:
                    img.putpixel((x + ox + i % bw, top + i // bw), 255 - v * 17)
            x += step(g)
    return img

rows = [(c.stem, render(c)) for c in sorted(FONTS.glob("*.c"))]
W = max(160 + r.width * SCALE for _, r in rows)
H = sum(r.height * SCALE + 6 for _, r in rows)
sheet = Image.new("L", (W, H), 255)
d = ImageDraw.Draw(sheet)
y = 0
for name, r in rows:
    d.text((4, y + 4), name, fill=0)
    sheet.paste(r.resize((r.width * SCALE, r.height * SCALE), Image.NEAREST), (160, y))
    y += r.height * SCALE + 6
OUT.parent.mkdir(exist_ok=True)
sheet.save(OUT)
print(OUT, sheet.size)
