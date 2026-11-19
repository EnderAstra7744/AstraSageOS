#!/usr/bin/env python3
"""Converts the original images in assets/src to the uncompressed 24-bit BMPs the kernel embeds.
v0.8: the wallpaper is used at its own size (1920x1080) - the screen mode follows the picture;
as_logo.bmp is the green "As" logo (welcome app icon, cropped to the logo square)."""
import os
from PIL import Image, ImageChops
here = os.path.dirname(os.path.abspath(__file__))
src = lambda n: os.path.join(here, "src", n)
for name, size in (("ast_icon", (256, 256)), ("boot_logo", (256, 256))):
    im = Image.open(src(name + ".jpg")).convert("RGB")
    im.resize(size, Image.LANCZOS).save(os.path.join(here, name + ".bmp"), "BMP")
    print(name, im.size, "->", size)
w = Image.open(src("desktop_wallpaper_1920.png")).convert("RGB")
w.save(os.path.join(here, "desktop_wallpaper.bmp"), "BMP")
print("desktop_wallpaper", w.size, "(native)")
l = Image.open(src("as_logo.png")).convert("RGB")
bb = ImageChops.difference(l, Image.new("RGB", l.size, (0, 0, 0))).point(lambda p: 255 if p > 40 else 0).getbbox()
l = l.crop(bb)
side = max(l.size)
sq = Image.new("RGB", (side, side), (0, 0, 0)); sq.paste(l, ((side - l.width) // 2, (side - l.height) // 2))
sq.resize((256, 256), Image.LANCZOS).save(os.path.join(here, "as_logo.bmp"), "BMP")
print("as_logo", l.size, "-> 256x256")
