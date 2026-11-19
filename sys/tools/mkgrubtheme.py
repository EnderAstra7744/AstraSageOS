#!/usr/bin/env python3
"""Generates the PNG files of the GRUB theme (grub/themes/astrasage/) from the project's own assets:
the wallpaper (darkened), the user's As logo (corners made transparent), the AstraSageOS wordmark (project font)
and the nine-piece menu / selection frames. Needs Pillow. The generated files are committed, so building the ISO
does not need this script; run it only after changing assets/.

    python3 sys/tools/mkgrubtheme.py
"""
import os
from PIL import Image, ImageDraw, ImageFont, ImageChops, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
OUT = os.path.join(ROOT, "grub", "themes", "astrasage")
os.makedirs(OUT, exist_ok=True)
GREEN = (155, 224, 28)

# 1. background: the desktop wallpaper, darkened so the menu stays readable
wp = Image.open(os.path.join(ROOT, "assets", "desktop_wallpaper.bmp")).convert("RGB")
dark = Image.eval(wp, lambda v: int(v * 0.42))
vig = Image.new("L", wp.size, 0)
ImageDraw.Draw(vig).ellipse((-400, -300, wp.width + 400, wp.height + 300), fill=255)
vig = vig.filter(ImageFilter.GaussianBlur(220))
dark = Image.composite(dark, Image.new("RGB", wp.size, (4, 6, 5)), vig)
# the wallpaper carries the big As logo in its centre; behind the menu it would clash with the real logo.
# The logo's box is filled row by row from the pixels just left and right of it, the seam is feathered.
px = dark.load()
X0, X1, Y0, Y1 = 752, 1168, 332, 748
for y in range(Y0, Y1):
    l = px[X0 - 6, y]; r = px[X1 + 6, y]
    for x in range(X0, X1):
        t = (x - X0) / (X1 - X0)
        px[x, y] = tuple(int(l[i] * (1 - t) + r[i] * t) for i in range(3))
seam = Image.new("L", wp.size, 0)
ImageDraw.Draw(seam).rectangle((X0 - 14, Y0 - 14, X1 + 14, Y1 + 14), fill=255)
seam = ImageChops.subtract(seam, Image.new("L", wp.size, 0).copy())
blur = dark.filter(ImageFilter.GaussianBlur(9))
edge = Image.new("L", wp.size, 0)
ImageDraw.Draw(edge).rectangle((X0 - 14, Y0 - 14, X1 + 14, Y1 + 14), outline=255, width=26)
edge = edge.filter(ImageFilter.GaussianBlur(8))
dark = Image.composite(blur, dark, edge)
dark = dark.resize((1280, 720), Image.LANCZOS)  # smaller = decodes faster in slow emulators; GRUB scales it to the screen
dark.save(os.path.join(OUT, "background.png"), optimize=True)

# 2. logo: the user's As logo; the black outside the rounded square becomes transparent
lg = Image.open(os.path.join(ROOT, "assets", "as_logo.bmp")).convert("RGBA")
mask = Image.new("L", lg.size, 255)
tmp = lg.copy()
for corner in ((0, 0), (lg.width - 1, 0), (0, lg.height - 1), (lg.width - 1, lg.height - 1)):
    ImageDraw.floodfill(tmp, corner, (255, 0, 255, 0), thresh=40)
alpha = tmp.getchannel("A")
lg.putalpha(alpha)
lg = lg.resize((176, 176), Image.LANCZOS)
lg.save(os.path.join(OUT, "logo.png"), optimize=True)

# 3. wordmark: "AstraSageOS" in the project font
font = ImageFont.truetype(os.path.join(ROOT, "font", "font.ttf"), 54)
txt = "AstraSageOS"
w = int(font.getlength(txt)) + 16
img = Image.new("RGBA", (w, 76), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.text((9, 9), txt, font=font, fill=(0, 0, 0, 160))
d.text((8, 6), txt, font=font, fill=(236, 247, 226, 255))
img.save(os.path.join(OUT, "wordmark.png"), optimize=True)

# 4. nine-piece frames
def frame(prefix, fill, border, r, size):
    big = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    bd = ImageDraw.Draw(big)
    bd.rounded_rectangle((0, 0, size - 1, size - 1), radius=r, fill=border)
    bd.rounded_rectangle((1, 1, size - 2, size - 2), radius=r - 1, fill=fill)
    # cut into pieces: corners r x r, edges 1 px wide, centre 1 x 1
    c = r
    def piece(box, name, sz=None):
        p = big.crop(box)
        if sz: p = p.resize(sz, Image.NEAREST)
        p.save(os.path.join(OUT, "%s_%s.png" % (prefix, name)), optimize=True)
    piece((0, 0, c, c), "nw"); piece((size - c, 0, size, c), "ne")
    piece((0, size - c, c, size), "sw"); piece((size - c, size - c, size, size), "se")
    m = size // 2
    piece((m, 0, m + 1, c), "n"); piece((m, size - c, m + 1, size), "s")
    piece((0, m, c, m + 1), "w"); piece((size - c, m, size, m + 1), "e")
    piece((m, m, m + 1, m + 1), "c")

frame("menu", (14, 20, 16, 205), (60, 96, 40, 230), 14, 64)
frame("select", (116, 190, 24, 255), (170, 240, 60, 255), 10, 64)
print("theme images written to", OUT)
