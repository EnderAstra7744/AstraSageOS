#!/usr/bin/env python3
"""Builds an ASPI archive: mkpkg.py packages/NAME out.aspi [program.elf ...]
   manifest + README + every file of src/ unchanged; a native program is added as bin/<its file name>
   (native.txt of the package names it, e.g. "bin/netsurf.elf")."""
import os, sys, zipfile
pk, out = sys.argv[1], sys.argv[2]
elfs = sys.argv[3:]
os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
def add(z, name, data):
    zi = zipfile.ZipInfo(name, (2026, 10, 28, 0, 0, 0)); zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = 0o644 << 16
    z.writestr(zi, data, compresslevel=9)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for n in ("ASPIManifest.json", "README.md"):
        z.write(os.path.join(pk, n), n)
    sd = os.path.join(pk, "src")
    for f in sorted(os.listdir(sd)):
        add(z, f, open(os.path.join(sd, f), "rb").read())
    for elf in elfs:
        add(z, "bin/" + os.path.basename(elf), open(elf, "rb").read())
print("wrote", out, os.path.getsize(out))
