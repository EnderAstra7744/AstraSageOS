#!/usr/bin/env python3
"""Turns the wallpaper pictures in assets/wallpapers_src/ into the kernel's .wpz files:
    assets/wp_N.wpz   the desktop picture (up to 960x540)  - magic "WPZ1", u16 w, u16 h, raw deflate of PNG-filtered RGB rows
    assets/wpt_N.wpz  its 320x180 thumbnail (picker, Settings)
N is the wallpaper number (picture number = theme number for 0..8).  The pictures are the user's own; the only edits are a crop
(to 16:9, and away from a site watermark), a smooth enlargement for small pictures and a light sharpening of the enlargement."""
import os, struct, sys, zlib
import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "assets", "wallpapers_src")
OUT = os.path.join(ROOT, "assets")

# number, file, crop box (left, top, right, bottom) in source pixels or None, target width
PICS = [
    (0, "cherry_lake.jpg", None, 1280),
    (1, "event_horizon.jpg", None, 1280),
    (2, "green_waves.bmp", None, 1280),
    (3, "aurora.jpg", None, 1280),
    (4, "golden_valley.jpg", None, 1280),
    (5, "crimson_forest.jpg", None, 1280),
    (6, "blue_ridge.jpg", None, 1280),
    (7, "purple_dusk.jpg", (107, 0, 1920, 1020), 1280),      # the site mark in the bottom right corner is cropped away
    (8, "auro_galaxy.jpg", None, 1280),
    (9, "crimson_waves.jpg", None, 1280),
    (10, "emerald_layers.jpg", None, 1280),
]

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return np.where((pa <= pb) & (pa <= pc), a, np.where(pb <= pc, b, c))

def encode(img):
    """PNG-style filtering, the cheapest filter per row (by the sum of absolute values), raw deflate."""
    a = np.asarray(img.convert("RGB"), dtype=np.int16)
    h, w, _ = a.shape
    rows = a.reshape(h, w * 3)
    out = bytearray()
    prev = np.zeros(w * 3, np.int16)
    for y in range(h):
        cur = rows[y]
        left = np.concatenate([np.zeros(3, np.int16), cur[:-3]])
        ul = np.concatenate([np.zeros(3, np.int16), prev[:-3]])
        cand = [cur, cur - left, cur - prev, cur - ((left + prev) >> 1), cur - paeth(left, prev, ul)]
        cand = [(c & 255).astype(np.uint8) for c in cand]
        best = min(range(5), key=lambda i: int(np.abs(cand[i].astype(np.int8)).sum()))
        out.append(best)
        out += cand[best].tobytes()
        prev = cur
    z = zlib.compressobj(9, zlib.DEFLATED, -15)
    return z.compress(bytes(out)) + z.flush()

def prepare(path, crop, width):
    """Crop to 16:9; small JPEG sources go through Real-ESRGAN x4 first (sys/tools/esrgan_numpy.py, results cached in
    assets/wallpapers_src/sr/), then are scaled to the target size with an area filter and a very light sharpening."""
    import cv2
    name = os.path.splitext(os.path.basename(path))[0]
    cached = os.path.join(SRC, "sr", name + ".png")
    im = cv2.imread(cached if os.path.exists(cached) else path)
    if crop and not os.path.exists(cached): im = im[crop[1]:crop[3], crop[0]:crop[2]]
    h0, w0 = im.shape[:2]
    if w0 * 9 > h0 * 16:
        nw = h0 * 16 // 9; x = (w0 - nw) // 2; im = im[:, x:x + nw]
    elif w0 * 9 < h0 * 16:
        nh = w0 * 9 // 16; y = (h0 - nh) // 2; im = im[y:y + nh, :]
    h = int(round(width * 9 / 16))
    im = cv2.resize(im, (width, h), interpolation=cv2.INTER_AREA if im.shape[1] >= width else cv2.INTER_CUBIC)
    blur = cv2.GaussianBlur(im, (0, 0), 1.0)
    im = cv2.addWeighted(im, 1.25, blur, -0.25, 0)
    return Image.fromarray(cv2.cvtColor(im, cv2.COLOR_BGR2RGB))

def write(path, im):
    data = encode(im)
    with open(path, "wb") as f:
        f.write(b"WPZ1" + struct.pack("<HH", im.size[0], im.size[1]) + data)
    return len(data)

if __name__ == "__main__":
    total = 0
    for n, name, crop, width in PICS:
        im = prepare(os.path.join(SRC, name), crop, width)
        a = write(os.path.join(OUT, "wp_%d.wpz" % n), im)
        t = write(os.path.join(OUT, "wpt_%d.wpz" % n), im.resize((320, 180), Image.LANCZOS))
        total += a + t
        print("wp_%d.wpz  %dx%d  %d KiB  (+ thumbnail %d KiB)  %s" % (n, im.size[0], im.size[1], a // 1024, t // 1024, name))
    print("total %d KiB" % (total // 1024))
