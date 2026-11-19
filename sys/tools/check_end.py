#!/usr/bin/env python3
"""The kernel image (with .bss) must end below NATIVE_BASE (native/as_api.h), or native programs (NetSurf, FroggyCode) cannot load."""
import re, subprocess, sys
base = int(re.findall(r"NATIVE_BASE\s+(0x[0-9a-fA-F]+)", open("native/as_api.h").read())[-1], 16)
end = int(next(l for l in subprocess.check_output(["nm", sys.argv[1]], text=True).splitlines() if l.endswith(" _end")).split()[0], 16)
print("kernel ends at %.1f MiB, native programs start at %.1f MiB" % (end / 2**20, base / 2**20))
if end > base - (2 << 20):
    sys.exit("ERROR: kernel image (.bss) overlaps the native area - shrink static buffers")
