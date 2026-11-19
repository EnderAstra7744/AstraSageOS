#!/usr/bin/env python3
"""Builds the AstraSageOS boot ISO around the real GNU GRUB 2.12 (BIOS and UEFI). Needs only Python 3.

    python3 sys/tools/mkgrubiso.py build/as_kernel.elf astrasage.iso

What goes into the image (the GRUB parts are the prebuilt files in grub/bin/, see grub/BUILDING.txt):
  /boot/as_kernel.elf                    the kernel, loaded by GRUB's `multiboot` command (Multiboot 1)
  /boot/grub/grub.cfg, fonts/, themes/   menu, AstraSage font and the gfxmenu theme (project files)
  /boot/grub/i386-pc/                    GRUB modules for BIOS + eltorito.img (GRUB core with cdboot, El Torito BIOS entry)
  /boot/grub/x86_64-efi/, i386-efi/      GRUB modules for 64-bit / 32-bit UEFI
  /boot/grub/efi.img                     FAT12 EFI system partition image (EFI/BOOT/BOOTX64.EFI, BOOTIA32.EFI) = El Torito UEFI entry
ISO 9660 with Joliet (long, lower-case names, which GRUB's iso9660 module reads) and El Torito (BIOS + UEFI).
"""
import os, struct, sys, time

SECTOR = 2048
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
GRUB = os.path.join(ROOT, "grub")
VOLID = "ASTRASAGEOS"


def both16(v): return struct.pack("<H", v) + struct.pack(">H", v)
def both32(v): return struct.pack("<I", v) + struct.pack(">I", v)
def nsect(n): return (n + SECTOR - 1) // SECTOR


# ---------------------------------------------------------------- FAT12 image (the UEFI system partition)

def make_fat12(files, size_kib):
    """files: {"EFI/BOOT/BOOTX64.EFI": bytes, ...} (8.3 upper-case names). Returns the image bytes."""
    SS = 512
    total = size_kib * 1024 // SS
    spc = 1
    while total // spc >= 4000: spc *= 2
    reserved, nfats, rootents = 1, 2, 512
    root_secs = rootents * 32 // SS
    fat_secs = ((total // spc) * 3 // 2 + SS - 1) // SS + 1
    data_start = reserved + nfats * fat_secs + root_secs
    nclusters = (total - data_start) // spc
    assert nclusters < 4085, "FAT12 image too large for FAT12"
    img = bytearray(total * SS)
    bs = bytearray(SS)
    bs[0:3] = b"\xEB\x3C\x90"
    bs[3:11] = b"ASTRAEFI"
    struct.pack_into("<HBHBHHBHHHII", bs, 11, SS, spc, reserved, nfats, rootents, total if total < 65536 else 0, 0xF8, fat_secs, 32, 64, 0, 0)
    bs[36] = 0x80; bs[38] = 0x29
    struct.pack_into("<I", bs, 39, 0xA57A5A6E)
    bs[43:54] = b"ASTRAEFI   "; bs[54:62] = b"FAT12   "
    bs[510:512] = b"\x55\xAA"
    img[0:SS] = bs
    fat = bytearray(fat_secs * SS)
    fat[0:3] = b"\xF8\xFF\xFF"
    nxt = [2]

    def setfat(c, v):
        o = c * 3 // 2
        if c & 1:
            fat[o] = (fat[o] & 0x0F) | ((v & 0x0F) << 4); fat[o + 1] = (v >> 4) & 0xFF
        else:
            fat[o] = v & 0xFF; fat[o + 1] = (fat[o + 1] & 0xF0) | ((v >> 8) & 0x0F)

    def alloc(data):
        n = max(1, (len(data) + SS * spc - 1) // (SS * spc))
        first = nxt[0]
        for i in range(n):
            c = first + i
            setfat(c, 0xFFF if i == n - 1 else c + 1)
            off = (data_start + (c - 2) * spc) * SS
            chunk = data[i * SS * spc:(i + 1) * SS * spc]
            img[off:off + len(chunk)] = chunk
        nxt[0] += n
        return first

    def dirent(name83, attr, cluster, size):
        n, _, e = name83.partition(".")
        raw = n.upper().ljust(8)[:8].encode() + e.upper().ljust(3)[:3].encode()
        rec = bytearray(32)
        rec[0:11] = raw
        rec[11] = attr
        struct.pack_into("<HHHHHHHI", rec, 14, 0x0000, 0x5A21, 0x5A21, 0, 0x0000, 0x5A21, cluster & 0xFFFF, size)
        return bytes(rec)

    tree = {}
    for path, data in files.items():
        parts = path.split("/")
        d = tree
        for p in parts[:-1]:
            d = d.setdefault(p, {})
        d[parts[-1]] = data

    def build_dir(d):
        return [(name, 0x10 if isinstance(v, dict) else 0x20, v) for name, v in sorted(d.items())]

    # a sub directory gets its cluster first, then it is filled
    def fill(d, cluster, parent):
        recs = b""
        if cluster:
            recs += dirent(".", 0x10, cluster, 0) + dirent("..", 0x10, parent, 0)
        for name, attr, v in build_dir(d):
            if attr == 0x10:
                c = alloc(b"\0" * SS)
                fill(v, c, cluster)
                recs += dirent(name, attr, c, 0)
            else:
                recs += dirent(name, attr, alloc(v), len(v))
        if cluster:
            assert len(recs) <= SS * spc
            off = (data_start + (cluster - 2) * spc) * SS
            img[off:off + len(recs)] = recs
        return recs

    rootrecs = fill(tree, 0, 0)
    assert len(rootrecs) <= root_secs * SS
    ro = (reserved + nfats * fat_secs) * SS
    img[ro:ro + len(rootrecs)] = rootrecs
    for i in range(nfats):
        o = (reserved + i * fat_secs) * SS
        img[o:o + len(fat)] = fat
    return bytes(img)


# ---------------------------------------------------------------- ISO 9660 + Joliet + El Torito

def d9660_name(name, taken, is_dir):
    base, dot, ext = name.upper().rpartition(".") if "." in name and not is_dir else (name.upper(), "", "")
    if not dot: base, ext = name.upper(), ""
    clean = lambda s: "".join(c if (c.isalnum() and c.isascii()) or c == "_" else "_" for c in s)
    base, ext = clean(base)[:8] or "_", clean(ext)[:3]
    cand = base + ("." + ext if ext else "")
    i = 0
    while cand in taken:
        i += 1
        suffix = "~%d" % i
        cand = base[:8 - len(suffix)] + suffix + ("." + ext if ext else "")
    taken.add(cand)
    return cand if is_dir else cand + ";1"


def dirrec(extent, size, ident, is_dir, when):
    n = 33 + len(ident)
    pad = n % 2
    rec = struct.pack("<BB", n + pad, 0) + both32(extent) + both32(size) + when
    rec += struct.pack("<BBB", 2 if is_dir else 0, 0, 0) + both16(1) + struct.pack("<B", len(ident)) + ident
    return rec + b"\0" * pad


class Node:
    def __init__(self, name, data=None):
        self.name = name
        self.data = data            # bytes for a file, None for a directory
        self.kids = {} if data is None else None
        self.extent = 0
        self.size = 0
        self.jextent = 0
        self.jsize = 0
        self.parent = None


def build_iso(files, out):
    now = time.gmtime(int(os.environ.get("SOURCE_DATE_EPOCH", "1792281600")))
    when = bytes([now.tm_year - 1900, now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, 0])
    d17 = ("%04d%02d%02d%02d%02d%02d00" % (now.tm_year, now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec)).encode() + b"\0"

    root = Node("")
    for path, data in sorted(files.items()):
        n = root
        parts = path.strip("/").split("/")
        for p in parts[:-1]:
            if p not in n.kids:
                k = Node(p); k.parent = n; n.kids[p] = k
            n = n.kids[p]
        f = Node(parts[-1], data); f.parent = n; n.kids[parts[-1]] = f
    root.parent = root

    dirs = []
    def collect(n):
        dirs.append(n)
        for k in sorted(n.kids.values(), key=lambda x: x.name):
            if k.kids is not None: collect(k)
    collect(root)

    # names
    for n in dirs:
        taken = set()
        n.iso_names = {}
        for k in sorted(n.kids.values(), key=lambda x: x.name):
            n.iso_names[k.name] = d9660_name(k.name, taken, k.kids is not None)

    def jname(s): return s.encode("utf-16-be")

    # sizes of directory extents
    def dir_bytes(n, joliet, ext_of, size_of):
        def rec_for(k, ident):
            return dirrec(ext_of(k), size_of(k), ident, k.kids is not None, when)
        items = [(dirrec(ext_of(n), size_of(n), b"\0", True, when)), (dirrec(ext_of(n.parent), size_of(n.parent), b"\1", True, when))]
        kids = sorted(n.kids.values(), key=lambda x: (jname(x.name) if joliet else n.iso_names[x.name].encode()))
        for k in kids:
            ident = jname(k.name) if joliet else n.iso_names[k.name].encode()
            items.append(rec_for(k, ident))
        # records never cross a sector boundary
        out = b""; cur = b""
        for it in items:
            if len(cur) + len(it) > SECTOR:
                out += cur.ljust(SECTOR, b"\0"); cur = b""
            cur += it
        out += cur.ljust(SECTOR, b"\0")
        return out

    # layout
    L_PVD, L_BRVD, L_SVD, L_TERM = 16, 17, 18, 19
    lba = 20
    def path_table_size(joliet):
        s = 0
        for n in dirs:
            nm = jname(n.name) if (joliet and n is not root) else (b"\0" if n is root else n.parent.iso_names[n.name].encode() if not joliet else jname(n.name))
            s += 8 + len(nm) + (len(nm) % 2)
        return s
    pt_size = path_table_size(False)
    jpt_size = path_table_size(True)
    L_PT = lba; lba += 2 * nsect(pt_size)      # L and M
    L_JPT = lba; lba += 2 * nsect(jpt_size)
    # directory extents: provisional sizes (record sizes do not depend on extents)
    for n in dirs:
        n.extent = 0; n.size = SECTOR
    def total_dir_size(joliet):
        for n in dirs:
            b = dir_bytes(n, joliet, lambda k: 0, lambda k: 0)
            if joliet: n.jsize = len(b)
            else: n.size = len(b)
    total_dir_size(False); total_dir_size(True)
    for n in dirs:
        n.extent = lba; lba += n.size // SECTOR
    for n in dirs:
        n.jextent = lba; lba += n.jsize // SECTOR
    L_CAT = lba; lba += 1
    # files
    allfiles = []
    def collect_files(n):
        for k in sorted(n.kids.values(), key=lambda x: x.name):
            if k.kids is None: allfiles.append(k)
            else: collect_files(k)
    collect_files(root)
    for f in allfiles:
        f.extent = lba; f.size = len(f.data); f.jextent = f.extent; f.jsize = f.size
        lba += max(1, nsect(f.size)) if f.size else 0
    total = lba
    img = bytearray(total * SECTOR)
    def put(at, data): img[at * SECTOR: at * SECTOR + len(data)] = data
    for n in dirs:
        put(n.extent, dir_bytes(n, False, lambda k: k.extent, lambda k: k.size))
        put(n.jextent, dir_bytes(n, True, lambda k: k.jextent if k.kids is not None else k.extent, lambda k: k.jsize if k.kids is not None else k.size))
    for f in allfiles:
        if f.size: put(f.extent, f.data)

    def path_tables(joliet):
        order = dirs
        num = {id(n): i + 1 for i, n in enumerate(order)}
        lt = b""; mt = b""
        for n in order:
            if n is root: nm = b"\0"
            elif joliet: nm = jname(n.name)
            else: nm = n.parent.iso_names[n.name].encode()
            ext = n.jextent if joliet else n.extent
            par = num[id(n.parent)] if n is not root else 1
            pad = b"\0" * (len(nm) % 2)
            lt += struct.pack("<BBIH", len(nm), 0, ext, par) + nm + pad
            mt += struct.pack(">BBIH", len(nm), 0, ext, par) + nm + pad
        return lt, mt
    lt, mt = path_tables(False); put(L_PT, lt); put(L_PT + nsect(pt_size), mt)
    jlt, jmt = path_tables(True); put(L_JPT, jlt); put(L_JPT + nsect(jpt_size), jmt)

    def volume(kind, joliet):
        v = bytearray(SECTOR)
        v[0] = kind; v[1:6] = b"CD001"; v[6] = 1
        ident = VOLID.encode() if not joliet else VOLID.encode("utf-16-be")
        v[8:40] = b" " * 32
        v[40:72] = ident.ljust(32, b"\0" if joliet else b" ")
        v[80:88] = both32(total)
        if joliet: v[88:91] = b"%/E"
        v[120:124] = both16(1); v[124:128] = both16(1); v[128:132] = both16(SECTOR)
        ptsz = jpt_size if joliet else pt_size
        v[132:140] = both32(ptsz)
        lp = L_JPT if joliet else L_PT
        v[140:144] = struct.pack("<I", lp); v[148:152] = struct.pack(">I", lp + nsect(ptsz))
        v[156:190] = dirrec(root.jextent if joliet else root.extent, root.jsize if joliet else root.size, b"\0", True, when)
        blank = b" " * 128 if not joliet else "".join(" " for _ in range(64)).encode("utf-16-be")
        for off in (190, 318, 446, 574):
            v[off:off + 128] = blank
        pub = "AstraSageOS".encode() if not joliet else "AstraSageOS".encode("utf-16-be")
        v[318:318 + len(pub)] = pub
        v[702:813] = b" " * 111
        for off in (813, 830, 864):
            v[off:off + 17] = d17
        v[847:864] = b"0000000000000000\0"
        v[881] = 1
        return v
    put(L_PVD, volume(1, False))
    put(L_SVD, volume(2, True))
    br = bytearray(SECTOR)
    br[0] = 0; br[1:6] = b"CD001"; br[6] = 1
    br[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    br[71:75] = struct.pack("<I", L_CAT)
    put(L_BRVD, br)
    term = bytearray(SECTOR); term[0] = 255; term[1:6] = b"CD001"; term[6] = 1
    put(L_TERM, term)

    # El Torito catalog: BIOS (no emulation, 4 x 512 bytes loaded; the boot info table is patched in the image)
    bios = next(f for f in allfiles if f.name == "eltorito.img")
    effi = next((f for f in allfiles if f.name == "efi.img"), None)
    cat = bytearray(SECTOR)
    cat[0:4] = b"\x01\x00\x00\x00"
    cat[4:28] = VOLID.encode().ljust(24, b"\0")
    cat[30:32] = b"\x55\xAA"
    s = sum(struct.unpack_from("<16H", cat, 0)) & 0xFFFF
    struct.pack_into("<H", cat, 28, (-s) & 0xFFFF)
    cat[32:64] = struct.pack("<BBHBBHI", 0x88, 0, 0, 0, 0, 4, bios.extent) + b"\0" * 20
    if effi:
        cat[64:96] = struct.pack("<BBH", 0x91, 0xEF, 1) + b"\0" * 28
        cat[96:128] = struct.pack("<BBHBBHI", 0x88, 0, 0, 0, 0, min(0xFFFF, effi.size // 512), effi.extent) + b"\0" * 20
    put(L_CAT, cat)
    # boot info table of the BIOS image (offset 8): PVD LBA, file LBA, length, 32-bit word checksum from byte 64
    data = bytearray(img[bios.extent * SECTOR: bios.extent * SECTOR + bios.size])
    csum = sum(struct.unpack_from("<I", data, o)[0] for o in range(64, len(data) - len(data) % 4, 4)) & 0xFFFFFFFF
    struct.pack_into("<IIII", data, 8, L_PVD, bios.extent, bios.size, csum)
    img[bios.extent * SECTOR: bios.extent * SECTOR + len(data)] = data
    open(out, "wb").write(img)
    return total, bios.extent, effi.extent if effi else 0


def collect_tree(base, prefix, files, skip=()):
    for dp, dn, fn in os.walk(base):
        dn.sort()
        for f in sorted(fn):
            if f in skip: continue
            full = os.path.join(dp, f)
            rel = os.path.relpath(full, base).replace(os.sep, "/")
            files[prefix + "/" + rel] = open(full, "rb").read()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    kernel, out = sys.argv[1], sys.argv[2]
    binroot = os.path.join(GRUB, "bin")
    need = [os.path.join(binroot, p) for p in ("i386-pc/eltorito.img", "efi/BOOTX64.EFI")]
    for p in need:
        if not os.path.exists(p):
            sys.exit("mkgrubiso: %s missing - the prebuilt GRUB files are not there (see grub/BUILDING.txt)" % p)
    files = {}
    files["boot/as_kernel.elf"] = open(kernel, "rb").read()
    x64 = os.path.join(ROOT, "build", "x64", "as_kernel_x64.elf32")
    if os.path.exists(x64):
        files["boot/as_kernel_x64.elf"] = open(x64, "rb").read()        # 64-bit kernel (Multiboot 1, enters long mode itself)
    # ARM kernels: ELF files (QEMU loads them directly; the raw .bin was rejected by some QEMU builds). Not bootable from this CD itself.
    for arch, qemu, cpu, d in (("arm64", "qemu-system-aarch64", "cortex-a72", "a64"), ("arm32", "qemu-system-arm", "cortex-a15", "a32")):
        full = os.path.join(ROOT, "build", d, "as_%s_gui.elf" % arch)
        if not os.path.exists(full):
            continue
        import subprocess, tempfile
        elf = os.path.join(tempfile.gettempdir(), "as_%s_gui.stripped.%d.elf" % (arch, os.getpid()))
        subprocess.check_call(["llvm-strip", "-o", elf, full])          # always from the fresh build (an old stripped copy used to be picked up here)
        name = "as_%s_gui.elf" % arch
        files[arch + "/" + name] = open(elf, "rb").read()
        os.remove(elf)
        binf = os.path.join(ROOT, "build", d, "as_%s_gui.bin" % arch)
        if os.path.exists(binf):
            files[arch + "/as_%s_gui.bin" % arch] = open(binf, "rb").read()     # the flat image (Limbo's "kernel" field)
        cmd = (qemu + " -M virt -cpu " + cpu + " -m 512 -kernel " + name + " -device ramfb -device virtio-keyboard-device -device virtio-tablet-device"
               " -drive file=disk.img,if=none,format=raw,id=d -device virtio-blk-device,drive=d -netdev user,id=n -device virtio-net-device,netdev=n"
               " -display none -vnc :0 -serial stdio")
        files[arch + "/README.TXT"] = ("AstraSageOS kernel for " + arch.upper() + " (QEMU virt machine, experimental).\r\nCopy " + name +
            " next to disk.img (truncate -s 64M disk.img) and run:\r\n\r\n" + cmd + "\r\n\r\nScreen: VNC viewer -> localhost:5900.\r\n"
            "Optional second disk (ASPI repository): -drive file=repo.img,if=none,format=raw,id=r -device virtio-blk-device,drive=r\r\n").encode()
        files[arch + "/run.sh"] = ("#!/bin/sh\n" + cmd + "\n").encode()
    files["boot/grub/grub.cfg"] = open(os.environ.get("ASTRA_GRUB_CFG", os.path.join(GRUB, "grub.cfg")), "rb").read()   # override: tests only
    collect_tree(os.path.join(GRUB, "fonts"), "boot/grub/fonts", files)
    collect_tree(os.path.join(GRUB, "themes"), "boot/grub/themes", files)
    for plat in ("i386-pc", "x86_64-efi", "i386-efi"):
        d = os.path.join(binroot, plat)
        if os.path.isdir(d):
            collect_tree(d, "boot/grub/" + plat, files)
    efi_files = {}
    for name, dst in (("BOOTX64.EFI", "EFI/BOOT/BOOTX64.EFI"), ("BOOTIA32.EFI", "EFI/BOOT/BOOTIA32.EFI")):
        p = os.path.join(binroot, "efi", name)
        if os.path.exists(p):
            efi_files[dst] = open(p, "rb").read()
    # ARM64 UEFI: BOOTAA64.EFI (our own loader, no GRUB) + the flat kernel, so the CD boots by itself on ARM machines with UEFI
    a64efi = os.path.join(ROOT, "build", "a64", "BOOTAA64.EFI")
    a64bin = os.path.join(ROOT, "build", "a64", "as_arm64_gui.bin")
    if os.path.exists(a64efi) and os.path.exists(a64bin):
        efi_files["EFI/BOOT/BOOTAA64.EFI"] = open(a64efi, "rb").read()
        efi_files["AS_ARM64.BIN"] = open(a64bin, "rb").read()
    a32efi = os.path.join(ROOT, "build", "a32", "BOOTARM.EFI")
    a32bin = os.path.join(ROOT, "build", "a32", "as_arm32_gui.bin")
    if os.path.exists(a32efi) and os.path.exists(a32bin):
        efi_files["EFI/BOOT/BOOTARM.EFI"] = open(a32efi, "rb").read()
        efi_files["AS_ARM32.BIN"] = open(a32bin, "rb").read()
    need_kib = (sum(len(v) for v in efi_files.values()) // 1024) + 256
    esp = make_fat12(efi_files, max(1440, ((need_kib + 63) // 64) * 64 + 1024))
    files["boot/grub/efi.img"] = esp
    for name in efi_files:
        files[name] = efi_files[name]
    files["README.TXT"] = (b"AstraSageOS boot CD (GNU GRUB 2.12)\r\n"
                           b"BIOS and UEFI (x86): GRUB menu -> multiboot /boot/as_kernel.elf -> AstraSageOS.\r\n"
                           b"UEFI on ARM64/ARM32: EFI/BOOT/BOOTAA64.EFI and BOOTARM.EFI (own loaders, experimental) load AS_ARM64.BIN / AS_ARM32.BIN.\r\n"
                           b"GRUB is free software (GPLv3+); source: https://github.com/rhboot/grub2 tag grub-2.12, see boot/grub/COPYING.GRUB\r\n")
    cp = os.path.join(GRUB, "COPYING.GRUB")
    if os.path.exists(cp):
        files["boot/grub/COPYING.GRUB"] = open(cp, "rb").read()
    total, bios_lba, efi_lba = build_iso(files, out)
    print("wrote %s: %d sectors (%d KiB); BIOS El Torito image at sector %d, UEFI image at sector %d; %d files"
          % (out, total, total * SECTOR // 1024, bios_lba, efi_lba, len(files)))


main()
