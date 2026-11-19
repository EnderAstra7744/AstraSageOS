#!/usr/bin/env python3
"""Rasterise font/font.ttf into an 8x16 VGA text-mode bitmap font (ASCII 32..126).

Writes:  font/ast8x16.fnt  (95 glyphs x 16 bytes, raw)
         font/font8x16.h (the same data as a C array)
"""
import sys
from PIL import Image, ImageDraw, ImageFont

import os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
TTF = "font/font.ttf"
W, H = 8, 16
FIRST, LAST = 32, 126
EXTRA = [0xE7, 0x11F, 0x131, 0xF6, 0x15F, 0xFC, 0xC7, 0x11E, 0x130, 0xD6, 0x15E, 0xDC, 0x2022, 0x2026]   # Turkish letters, bullet, ellipsis
EXTRA += [0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xF1, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xD1, 0xBF, 0xA1]               # Spanish
EXTRA += [0x401, 0x451] + list(range(0x410, 0x450))                                                      # Russian
DEJAVU = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"          # Cyrillic (the TTF above has none that renders)
NOTO_CJK = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"      # Chinese, 16x16 (SIL OFL)
CJK_INDEX = 2                                                           # the Simplified Chinese face of the collection
# Cyrillic letters that look exactly like a Latin one reuse the Latin glyph (same weight as the rest of the font)
LOOK = {0x410: "A", 0x412: "B", 0x415: "E", 0x41A: "K", 0x41C: "M", 0x41D: "H", 0x41E: "O", 0x420: "P", 0x421: "C", 0x422: "T", 0x425: "X",
        0x430: "a", 0x435: "e", 0x43E: "o", 0x440: "p", 0x441: "c", 0x445: "x", 0x443: "y"}

def main():
    size = int(sys.argv[1]) if len(sys.argv) > 1 else 16
    font = ImageFont.truetype(TTF, size)
    ascent, descent = font.getmetrics()
    top = (H - (ascent + descent)) // 2
    data = bytearray()
    sheet = Image.new("L", (W * 32, H * 3), 0)
    for code in range(FIRST, LAST + 1):
        img = Image.new("1", (W, H), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"                      # no anti-aliasing
        d.text((0, top), chr(code), font=font, fill=1)
        for y in range(H):
            row = 0
            for x in range(W):
                if img.getpixel((x, y)):
                    row |= 0x80 >> x
            data.append(row)
        i = code - FIRST
        sheet.paste(img.convert("L").point(lambda p: 255 if p else 0), ((i % 32) * W, (i // 32) * H))
    # The extra letters are built from the plain ASCII glyphs plus hand-placed marks: the TTF's own accented
    # glyphs come out wrong at 8x16 (hinting), so they are not used.
    def base(ch):
        i = (ord(ch) - FIRST) * 16
        return list(data[i:i + 16])
    def with_rows(g, rows):
        g = list(g)
        for y, v in rows.items():
            g[y] |= v
        return g
    CED = {13: 0x10, 14: 0x08, 15: 0x30}
    BREVE_L = {3: 0x24, 4: 0x18}; DIA_L = {3: 0x24, 4: 0x24}
    BREVE_U = {1: 0x24, 2: 0x18}; DIA_U = {1: 0x24, 2: 0x24}; DOT_U = {1: 0x18, 2: 0x18}
    inoi = base("i"); inoi[:6] = [0] * 6                       # i without its dot
    bullet = [0] * 16
    for y, v in ((6, 0x18), (7, 0x3C), (8, 0x3C), (9, 0x18)): bullet[y] = v
    ell = [0] * 16; ell[12] = 0x54
    ACUTE_L = {3: 0x0C, 4: 0x18}; ACUTE_U = {1: 0x0C, 2: 0x18}
    TILDE_L = {3: 0x32, 4: 0x4C}; TILDE_U = {1: 0x32, 2: 0x4C}
    def flip(ch):                                            # upside-down ! and ?: reverse the rows the glyph uses
        g = base(ch); ys = [y for y in range(16) if g[y]]
        a, b = ys[0], ys[-1]
        g[a:b + 1] = g[a:b + 1][::-1]
        return g
    glyphs = {
        0xE1: with_rows(base("a"), ACUTE_L), 0xE9: with_rows(base("e"), ACUTE_L), 0xED: with_rows(inoi, ACUTE_L),
        0xF3: with_rows(base("o"), ACUTE_L), 0xFA: with_rows(base("u"), ACUTE_L), 0xF1: with_rows(base("n"), TILDE_L),
        0xC1: with_rows(base("A"), ACUTE_U), 0xC9: with_rows(base("E"), ACUTE_U), 0xCD: with_rows(base("I"), ACUTE_U),
        0xD3: with_rows(base("O"), ACUTE_U), 0xDA: with_rows(base("U"), ACUTE_U), 0xD1: with_rows(base("N"), TILDE_U),
        0xBF: flip("?"), 0xA1: flip("!"),
        0xE7: with_rows(base("c"), CED), 0x11F: with_rows(base("g"), BREVE_L), 0x131: inoi,
        0xF6: with_rows(base("o"), DIA_L), 0x15F: with_rows(base("s"), CED), 0xFC: with_rows(base("u"), DIA_L),
        0xC7: with_rows(base("C"), CED), 0x11E: with_rows(base("G"), BREVE_U), 0x130: with_rows(base("I"), DOT_U),
        0xD6: with_rows(base("O"), DIA_U), 0x15E: with_rows(base("S"), CED), 0xDC: with_rows(base("U"), DIA_U),
        0x2022: bullet, 0x2026: ell,
    }
    # Cyrillic: DejaVu Sans Mono 13 px put on the same baseline as the Latin letters
    dj = ImageFont.truetype(DEJAVU, 13)
    def bottom(ft, ch, dy):
        im = Image.new("1", (W, 24), 0); ImageDraw.Draw(im).text((0, dy), ch, font=ft, fill=1); return im.getbbox()
    ref = Image.new("1", (W, H), 0); ImageDraw.Draw(ref).text((0, top), "H", font=font, fill=1)
    want_bottom = ref.getbbox()[3]
    dy = 0
    while bottom(dj, "Н", dy) and bottom(dj, "Н", dy)[3] < want_bottom: dy += 1
    while bottom(dj, "Н", dy) and bottom(dj, "Н", dy)[3] > want_bottom: dy -= 1
    for cp in range(0x400, 0x460):
        if cp not in EXTRA: continue
        if cp in LOOK:
            glyphs[cp] = base(LOOK[cp]); continue
        img = Image.new("1", (W, H), 0); d = ImageDraw.Draw(img); d.fontmode = "1"
        d.text((0, dy), chr(cp), font=dj, fill=1)
        g = []
        for y in range(H):
            row = 0
            for x in range(W):
                if img.getpixel((x, y)): row |= 0x80 >> x
            g.append(row)
        glyphs[cp] = g
    extra = bytearray()
    for cp in EXTRA:
        extra += bytes(glyphs[cp])
    # Chinese: every wide character used by sys/lang/*.tsv, 16x16 (left half rows, then right half rows)
    import glob
    wide_cps = set()
    for f in glob.glob("sys/lang/*.tsv"):
        for line in open(f, encoding="utf-8"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 4:
                for ch in parts[3]:
                    c = ord(ch)
                    if c >= 0x2E80 and c not in EXTRA: wide_cps.add(c)
    wide_cps = sorted(wide_cps)
    cj = ImageFont.truetype(NOTO_CJK, 16, index=CJK_INDEX)
    asc = cj.getmetrics()[0]
    wide = bytearray()
    for cp in wide_cps:
        img = Image.new("1", (16, 16), 0); d = ImageDraw.Draw(img); d.fontmode = "1"
        d.text((0, 13 - asc), chr(cp), font=cj, fill=1)
        for half in (0, 8):
            for y in range(16):
                row = 0
                for x in range(8):
                    if img.getpixel((half + x, y)): row |= 0x80 >> x
                wide.append(row)
    open("font/ast8x16.fnt", "wb").write(data)
    with open("font/font8x16.h", "w") as f:
        f.write("/* Generated by font/ttf2fnt.py from font/font.ttf - do not edit. */\n")
        f.write("#ifndef AS_FONT8X16_H\n#define AS_FONT8X16_H\n\n")
        f.write("#define AS_FONT_FIRST %d\n#define AS_FONT_COUNT %d\n\n" % (FIRST, LAST - FIRST + 1))
        f.write("static const unsigned char as_font8x16[AS_FONT_COUNT * 16] = {\n")
        for i in range(0, len(data), 16):
            f.write("    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",\n")
        f.write("};\n\n")
        f.write("/* extra glyphs: UTF-8 code points (Turkish letters, bullet, ellipsis) */\n")
        f.write("#define AS_FONT_NEXTRA %d\n" % len(EXTRA))
        f.write("static const unsigned short as_font_extra_cp[AS_FONT_NEXTRA] = { " + ", ".join("0x%04X" % c for c in EXTRA) + " };\n")
        f.write("static const unsigned char as_font_extra[AS_FONT_NEXTRA * 16] = {\n")
        for i in range(0, len(extra), 16):
            f.write("    " + ", ".join("0x%02X" % b for b in extra[i:i + 16]) + ",\n")
        f.write("};\n\n")
        f.write("/* wide (Chinese) glyphs: 16 columns = 2 cells; 32 bytes each = 16 left rows then 16 right rows, sorted by code point */\n")
        f.write("#define AS_FONT_NWIDE %d\n" % len(wide_cps))
        f.write("static const unsigned short as_font_wide_cp[AS_FONT_NWIDE] = {\n")
        for i in range(0, len(wide_cps), 12):
            f.write("    " + ", ".join("0x%04X" % c for c in wide_cps[i:i + 12]) + ",\n")
        f.write("};\nstatic const unsigned char as_font_wide[AS_FONT_NWIDE * 32] = {\n")
        for i in range(0, len(wide), 16):
            f.write("    " + ", ".join("0x%02X" % b for b in wide[i:i + 16]) + ",\n")
        f.write("};\n\n#endif\n")
    sheet = sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST)
    sheet.save("/tmp/font_preview.png")
    print("ascent/descent", ascent, descent, "top", top, "bytes", len(data), "extra", len(EXTRA), "wide", len(wide_cps))

main()
