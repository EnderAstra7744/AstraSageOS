#!/usr/bin/env python3
"""as-check: is this file a program the AstraSageOS loader (kernel/native.c) accepts?  Prints the facts, exit 1 on a hard error."""
import struct, sys
NATIVE_BASE = 0x05000000
FS_FILE_MAX = 4096            # kernel/astra.h: files inside the OS cannot be larger
def main(path):
    d = open(path, "rb").read()
    err = []
    if d[:4] != b"\x7fELF": err.append("not an ELF file")
    else:
        if d[4] != 1 or d[5] != 1: err.append("not ELF32 little endian")
        etype, mach, _, entry, phoff, _, _, _, phentsize, phnum = struct.unpack_from("<HHIIIIIHHH", d, 16)
        if etype != 2: err.append("not an executable (ET_EXEC)")
        if mach != 3: err.append("not i386 (machine %d)" % mach)
        lo, hi, loads = 1 << 32, 0, 0
        for i in range(phnum):
            t, off, va, pa, fsz, msz, fl, al = struct.unpack_from("<IIIIIIII", d, phoff + i * phentsize)
            if t in (3, 2): err.append("dynamic linking (PT_INTERP/PT_DYNAMIC) is not supported")
            if t == 1:
                loads += 1; lo = min(lo, va); hi = max(hi, va + msz)
                if va < NATIVE_BASE: err.append("segment below NATIVE_BASE (0x%x)" % va)
                if off + fsz > len(d): err.append("segment is cut off")
        if not loads: err.append("no loadable segment")
        elif not (lo <= entry < hi): err.append("entry point 0x%x is outside the program" % entry)
        if not err:
            print("as-check: ok  ELF32 i386 EXEC, entry 0x%x, memory 0x%x-0x%x (%d KiB), file %d bytes" % (entry, lo, hi, (hi - lo + 1023) // 1024, len(d)))
            if len(d) > FS_FILE_MAX:
                print("as-check: note: larger than %d bytes - it cannot be stored as a file inside AstraSageOS (nrun FILE); it can only ship embedded in an image or ASPI package" % FS_FILE_MAX)
            return 0
    for e in err: print("as-check: error: " + e, file=sys.stderr)
    return 1
sys.exit(main(sys.argv[1]))
