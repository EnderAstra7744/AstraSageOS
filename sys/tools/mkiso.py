#!/usr/bin/env python3
"""Build a bootable AstraSageOS CD image (ISO 9660 + El Torito) without GRUB.

    python3 sys/tools/mkiso.py build/as_kernel.elf astrasage.iso

Needs only Python 3. boot/isoboot.bin (assembled from boot/isoboot.asm with nasm) is the
boot sector; this script patches its parameter block with where the kernel image is and
writes the image. The kernel is stored as a flat binary (its ELF load segments laid out at
their physical addresses) that the boot sector copies to 1 MiB.
"""
import struct, sys, os

SECTOR = 2048
HERE = os.path.dirname(os.path.abspath(__file__))
BOOTBIN = os.path.join(HERE, "..", "..", "boot", "isoboot.bin")
LOW_LIMIT = 12 << 20                      # the boot sector copies pieces with INT 15h/87h (24-bit addresses: below 16 MiB)

def read_elf(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1:
        sys.exit("mkiso: %s is not a 32-bit ELF" % path)
    entry = struct.unpack_from("<I", d, 24)[0]
    phoff = struct.unpack_from("<I", d, 28)[0]
    phentsize, phnum = struct.unpack_from("<HH", d, 42)
    segs = []
    for i in range(phnum):
        t, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from("<8I", d, phoff + i * phentsize)
        if t == 1 and memsz:
            segs.append((paddr, d[off:off + filesz], memsz))
    if not segs:
        sys.exit("mkiso: no loadable segments")
    base = min(s[0] for s in segs)
    end_file = max(s[0] + len(s[1]) for s in segs)
    end_mem = max(s[0] + s[2] for s in segs)
    flat = bytearray(end_file - base)
    for paddr, data, _ in segs:
        flat[paddr - base: paddr - base + len(data)] = data
    return bytes(flat), base, end_mem - base, entry

def both16(v): return struct.pack("<H", v) + struct.pack(">H", v)
def both32(v): return struct.pack("<I", v) + struct.pack(">I", v)

def dirrec(extent, size, name, is_dir=False):
    ident = name if isinstance(name, bytes) else name.encode()
    n = 33 + len(ident)
    pad = n % 2
    date = bytes([126, 10, 12, 0, 0, 0, 0])                 # 2026-10-12 00:00:00 UTC
    rec = struct.pack("<BB", n + pad, 0) + both32(extent) + both32(size) + date
    rec += struct.pack("<BBB", 2 if is_dir else 0, 0, 0) + both16(1) + struct.pack("<B", len(ident)) + ident
    return rec + b"\0" * pad

def pad_to(b, n=SECTOR):
    return b + b"\0" * (-len(b) % n)

def sect(n): return (n + SECTOR - 1) // SECTOR

def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    elf, out = sys.argv[1], sys.argv[2]
    flat, load, memsz, entry = read_elf(elf)
    if len(flat) > LOW_LIMIT:
        sys.exit("mkiso: kernel image too large for the boot sector (%d bytes)" % len(flat))
    boot = bytearray(open(BOOTBIN, "rb").read())
    if len(boot) != SECTOR:
        sys.exit("mkiso: boot/isoboot.bin must be 2048 bytes")

    readme = (b"AstraSageOS 0.9.29 boot CD\r\n"
              b"ASKERNEL.BIN is the flat as_kernel image loaded by the El Torito boot sector.\r\n")

    # layout (in 2048-byte sectors)
    L_PVD, L_BRVD, L_TERM, L_PT_L, L_PT_M, L_ROOT, L_CAT, L_BOOT = 16, 17, 18, 19, 20, 21, 22, 23
    L_README = 24
    L_KERNEL = L_README + 1
    ksect = sect(len(flat))
    total = L_KERNEL + ksect

    struct.pack_into("<IIIIII", boot, 3, L_KERNEL, ksect, len(flat), memsz, entry, load)

    img = bytearray(total * SECTOR)
    def put(lba, data): img[lba * SECTOR: lba * SECTOR + len(data)] = data

    root = (dirrec(L_ROOT, SECTOR, b"\0", True) + dirrec(L_ROOT, SECTOR, b"\1", True) +
            dirrec(L_BOOT, SECTOR, "BOOT.IMG;1") + dirrec(L_README, len(readme), "README.TXT;1") +
            dirrec(L_KERNEL, len(flat), "ASKERNEL.BIN;1"))
    assert len(root) < SECTOR
    ptl = struct.pack("<BBIH", 1, 0, L_ROOT, 1) + b"\0\0"
    ptm = struct.pack("<BB", 1, 0) + struct.pack(">IH", L_ROOT, 1) + b"\0\0"

    def date17():
        return b"2026101200000000" + b"\0"
    pvd = bytearray(SECTOR)
    pvd[0:8] = b"\x01CD001\x01\x00"
    pvd[8:40] = b"ASTRASAGEOS".ljust(32)
    pvd[40:72] = b"ASTRASAGEOS".ljust(32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(len(ptl))
    pvd[140:144] = struct.pack("<I", L_PT_L)
    pvd[148:152] = struct.pack(">I", L_PT_M)
    pvd[156:190] = dirrec(L_ROOT, SECTOR, b"\0", True)
    for off in (190, 318, 446, 574):
        pvd[off:off + 128] = b" " * 128
    pvd[318:318 + 11] = b"AstraSageOS"
    pvd[702:813] = b" " * 111
    for off in (813, 830, 864):
        pvd[off:off + 17] = date17()
    pvd[847:864] = b"0000000000000000\0"
    pvd[881] = 1
    put(L_PVD, pvd)

    br = bytearray(SECTOR)
    br[0:7] = b"\x00CD001\x01"
    br[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    br[71:75] = struct.pack("<I", L_CAT)
    put(L_BRVD, br)
    put(L_TERM, b"\xffCD001\x01")
    put(L_PT_L, ptl); put(L_PT_M, ptm); put(L_ROOT, root)

    cat = bytearray(SECTOR)
    cat[0:4] = b"\x01\x00\x00\x00"
    cat[4:28] = b"ASTRASAGEOS".ljust(24, b"\0")
    cat[30:32] = b"\x55\xAA"
    s = sum(struct.unpack_from("<16H", cat, 0)) & 0xFFFF
    struct.pack_into("<H", cat, 28, (-s) & 0xFFFF)
    cat[32:64] = struct.pack("<BBHBBHI", 0x88, 0, 0, 0, 0, 4, L_BOOT) + b"\0" * 20
    put(L_CAT, cat)
    put(L_BOOT, boot)
    put(L_README, readme)
    put(L_KERNEL, flat)
    open(out, "wb").write(img)
    print("wrote %s: %d sectors (%d KiB), kernel image %d bytes at sector %d, load 0x%X entry 0x%X"
          % (out, total, len(img) // 1024, len(flat), L_KERNEL, load, entry))

main()
