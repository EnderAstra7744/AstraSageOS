#!/usr/bin/env python3
"""Pre-flight checks for the GRUB ISO (what can be checked without GRUB itself).

    python3 sys/tools/grubcheck.py build/as_kernel.elf grub

1. The kernel as GRUB's `multiboot` command sees it: header found in the first 8192 bytes on a 4-byte boundary,
   checksum, no flag bit GRUB rejects (GRUB: flags & 0x0000fff8), video request fields, 32-bit ELF with load
   segments (this mirrors grub-core/loader/i386/multiboot_mbi.c).
2. grub.cfg: balanced braces and quotes, every `multiboot` path and the theme exist, the six required menu entries.
3. theme.txt: every file it names exists, the nine pieces of each pixmap style exist.
Exit code 0 = all fine. This is NOT a substitute for `grub-script-check` and for booting the ISO.
"""
import os, re, struct, sys

fails = 0
def check(name, ok, why=""):
    global fails
    print(("PASS: " if ok else "FAIL: ") + name + ("" if ok else "  " + why))
    if not ok: fails += 1

kernel, gdir = sys.argv[1], sys.argv[2]
d = open(kernel, "rb").read()

# ---- 1. kernel
head = d[:8192]
pos = -1
for o in range(0, len(head) - 11, 4):
    m, f, c = struct.unpack_from("<III", head, o)
    if m == 0x1BADB002 and (m + f + c) & 0xFFFFFFFF == 0:
        pos = o; break
check("multiboot header inside the first 8192 bytes, 4-byte aligned, checksum 0", pos >= 0)
if pos >= 0:
    flags = struct.unpack_from("<I", head, pos + 4)[0]
    check("no flag bit that GRUB rejects (flags=0x%x)" % flags, flags & 0x0000FFF8 == 0)
    check("no a.out kludge (bit 16): ELF kernel", flags & 0x10000 == 0)
    if flags & 4:
        mt, w, h, dep = struct.unpack_from("<IIII", head, pos + 32)
        check("video request: linear graphics %dx%dx%d" % (w, h, dep), mt == 0 and w >= 640 and h >= 480 and dep in (24, 32))
    else:
        check("video request present (without it GRUB starts the kernel in VGA text mode)", False)
check("32-bit little-endian ELF executable", d[:4] == b"\x7fELF" and d[4] == 1 and d[5] == 1 and struct.unpack_from("<H", d, 16)[0] == 2 and struct.unpack_from("<H", d, 18)[0] == 3)
phoff = struct.unpack_from("<I", d, 28)[0]; phn = struct.unpack_from("<H", d, 44)[0]
segs = [struct.unpack_from("<8I", d, phoff + 32 * i) for i in range(phn)]
loads = [s for s in segs if s[0] == 1 and s[5]]
check("load segments at 1 MiB or above", loads and min(s[3] for s in loads) >= 0x100000)

# ---- 2. grub.cfg
cfg = open(os.path.join(gdir, "grub.cfg"), encoding="utf-8").read()
code = "\n".join(l.split("#")[0] if not re.search(r'["\']', l.split("#")[0]) else l for l in cfg.splitlines())
check("grub.cfg: braces balanced", code.count("{") == code.count("}"), "%d { vs %d }" % (code.count("{"), code.count("}")))
check("grub.cfg: double quotes balanced", cfg.replace('\\"', "").count('"') % 2 == 0)
for pat in re.findall(r"^\s*multiboot\s+(/\S+)", cfg, re.M):
    check("grub.cfg: kernel path %s is what the ISO script stages" % pat, pat in ("/boot/as_kernel.elf", "/boot/as_kernel_x64.elf"))
need = ["astrasage", "advanced", "recovery", "sysinfo", "reboot", "poweroff"]
for i in need:
    check("grub.cfg: entry id '%s'" % i, re.search(r"--id\s+%s\b" % i, cfg) is not None)
check("grub.cfg: first entry is Start AstraSageOS and default=0", cfg.index("Start AstraSageOS") < cfg.index("Advanced") and re.search(r"set default=0", cfg) is not None)
check("grub.cfg: countdown configured", re.search(r"set timeout=\d+", cfg) is not None and "timeout_style=menu" in cfg)
check("grub.cfg: no linux / linuxefi / chainloader (AstraSageOS is not Linux)", not re.search(r"^\s*(linux|linuxefi|linux16|initrd|chainloader)\b", cfg, re.M))
check("grub.cfg: the theme file is named", "themes/astrasage/theme.txt" in cfg)

# ---- 3. theme
tdir = os.path.join(gdir, "themes", "astrasage")
th = open(os.path.join(tdir, "theme.txt"), encoding="utf-8").read()
for f in set(re.findall(r'(?:file|desktop-image)\s*[:=]\s*"([^"]+)"', th)):
    check("theme: %s exists" % f, os.path.isfile(os.path.join(tdir, f)))
for sty in re.findall(r'pixmap_style\s*=\s*"([^"]+)"', th):
    for p in ("c", "n", "ne", "e", "se", "s", "sw", "w", "nw"):
        f = sty.replace("*", p)
        if not os.path.isfile(os.path.join(tdir, f)): check("theme: %s exists" % f, False)
check("theme: style pieces present", True)
check("theme: braces balanced", th.count("{") == th.count("}"))
for l in ("logo.png",):
    check("theme: the user's As logo is used (%s)" % l, os.path.isfile(os.path.join(tdir, l)))
print("\n%s" % ("all checks passed" if not fails else "%d check(s) FAILED" % fails))
sys.exit(1 if fails else 0)
