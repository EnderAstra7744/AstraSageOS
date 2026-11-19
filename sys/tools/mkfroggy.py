#!/usr/bin/env python3
"""Builds assets/FroggyCode.aspi from packages/FroggyCode/ (manifest + src/*.as). AstraScript checks every file on its own,
so each file gets `vr` lines for the shared variables it uses (the main file gets all of them)."""
import os, re, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PK = os.path.join(ROOT, "packages", "FroggyCode")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "assets", "FroggyCode.aspi")
INT = dict(alive=1, k=0, mode=0, mouth=0, blink=0, look=0, tick=0, talk_t=0, think_t=0, queued=0, handled=0, pend=0, tpl=0,
           cells=0, hop_x=0, hop_y=0, hop_d=1, hop_t=0, fx=0, fy=0, fz=7, fa=7, mx=0, my=0, lang=1, h=0, g=0, n=0, m=0,
           lw=0, lc=0, ln=0, wi=0, joke_n=0)
STR = "inp msg low w1 w2 a1 a2 rest en tr rep tmp cur seg w out target text bub1 ob cb".split() + ["l%d" % i for i in range(11)]
SIZE = 4096


def decls(used):
    out = []
    for n, v in INT.items():
        if n in used: out.append("vr inter %s = %d;" % (n, v))
    for n in STR:
        if n in used: out.append('vr strt %s = "";' % n)
    return out


def used_names(src):
    body = re.sub(r'"(\\.|[^"\\])*"', '""', src)
    return set(re.findall(r"[A-Za-z_][A-Za-z_0-9]*", body))


files = {}
srcdir = os.path.join(PK, "src")
for f in sorted(os.listdir(srcdir)):
    t = open(os.path.join(srcdir, f), encoding="utf-8").read()
    names = set(INT) | set(STR) if f == "froggy.as" else used_names(t)
    d = decls(names)
    # the main file declares everything first; library files only what their functions use
    if f == "froggy.as":
        lines = t.split("\n")
        i = 0
        while i < len(lines) and lines[i].startswith("@"): i += 1
        t = "\n".join(lines[:i] + d + lines[i:])
    else:
        t = "\n".join(d) + "\n" + t
    if len(t.encode("utf-8")) > SIZE:
        sys.exit("%s is %d bytes (limit %d)" % (f, len(t.encode()), SIZE))
    for ln in t.split("\n"):
        if len(ln.encode("utf-8")) > 250: sys.exit("%s: a line is longer than 250 bytes" % f)
    files[f] = t
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
    for name in ("ASPIManifest.json", "README.md"):
        z.write(os.path.join(PK, name), name)
    for f, t in files.items():
        zi = zipfile.ZipInfo(f, (2026, 10, 25, 0, 0, 0)); zi.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(zi, t.encode("utf-8"))
print("wrote", OUT, os.path.getsize(OUT), "bytes;", ", ".join("%s %d" % (f, len(t.encode())) for f, t in files.items()))
