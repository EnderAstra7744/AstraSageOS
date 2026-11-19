#!/usr/bin/env python3
"""Writes grub/fonts/unicode.pf2 - a GNU GRUB font (PF2 format) made from the project's own AstraSage bitmap font
(font/font8x16.h: ASCII + Turkish letters) plus a few hand-drawn symbols (arrows, bullet), so the real GRUB menu is
set in the AstraSageOS typeface. The font is named "Unifont Regular 16" because that is how theme.txt refers to it.

    python3 sys/tools/mkpf2.py
"""
import os, re, struct
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
src = open(os.path.join(ROOT, "font", "font8x16.h")).read()

def arr(name):
    m = re.search(name + r"\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", src, re.S)
    return [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(1))] if m else []

first = int(re.search(r"AS_FONT_FIRST\s+(\d+)", src).group(1))
base = arr("as_font8x16")
extra = arr("as_font_extra")
extra_cp = arr("as_font_extra_cp")
glyphs = {}
for i in range(len(base) // 16):
    glyphs[first + i] = base[i * 16:(i + 1) * 16]
for i, cp in enumerate(extra_cp):
    glyphs[cp] = extra[i * 16:(i + 1) * 16]

def draw(rows):                       # rows: 16 strings of 8 chars
    return [int("".join("1" if c == "#" else "0" for c in r), 2) for r in rows]
glyphs[0x2191] = draw(["........", "...##...", "..####..", ".######.", "###..###", "...##...", "...##...", "...##...", "...##...", "...##...", "...##...", "........", "........", "........", "........", "........"])
glyphs[0x2193] = draw(["........", "........", "........", "...##...", "...##...", "...##...", "...##...", "...##...", "...##...", "###..###", ".######.", "..####..", "...##...", "........", "........", "........"])
glyphs[0x2022] = glyphs.get(0x2022) or draw(["........"] * 6 + ["..####..", ".######.", ".######.", "..####..", "........", "........", "........", "........", "........", "........"])
glyphs[0xA0] = [0] * 16
glyphs[0xFFFD] = glyphs[ord("?")]

def sec(tag, data): return tag + struct.pack(">I", len(data)) + data
name = b"Unifont Regular 16\0"
out = b"FILE" + struct.pack(">I", 4) + b"PFF2"
out += sec(b"NAME", name) + sec(b"FAMI", b"Unifont\0") + sec(b"WEIG", b"normal\0") + sec(b"SLAN", b"normal\0")
out += sec(b"PTSZ", struct.pack(">H", 16)) + sec(b"MAXW", struct.pack(">H", 8)) + sec(b"MAXH", struct.pack(">H", 16))
out += sec(b"ASCE", struct.pack(">H", 12)) + sec(b"DESC", struct.pack(">H", 4))
cps = sorted(glyphs)
chix_len = 9 * len(cps)
data_start = len(out) + 8 + chix_len + 8          # CHIX header, entries, DATA header
chix = b""; data = b""
for cp in cps:
    rows = glyphs[cp]
    bits = "".join(format(r & 255, "08b") for r in rows)           # 8 x 16 bits, row-major, MSB first
    bm = int(bits, 2).to_bytes(16, "big")
    chix += struct.pack(">IBI", cp, 0, data_start + len(data))
    data += struct.pack(">HHhhh", 8, 16, 0, -4, 8) + bm
out += b"CHIX" + struct.pack(">I", chix_len) + chix + b"DATA" + struct.pack(">I", 0xFFFFFFFF) + data
os.makedirs(os.path.join(ROOT, "grub", "fonts"), exist_ok=True)
open(os.path.join(ROOT, "grub", "fonts", "unicode.pf2"), "wb").write(out)
print("grub/fonts/unicode.pf2: %d glyphs, %d bytes" % (len(cps), len(out)))
