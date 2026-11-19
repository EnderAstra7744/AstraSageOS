#!/usr/bin/env python3
"""Copies the GRUB 2.12 pieces the ISO needs from a GRUB build (see grub/BUILDING.txt) into grub/bin/:

    python3 sys/tools/grubstage.py <grub-build-root> <install-root-pc> <install-root-efi64> [<install-root-efi32>]

<grub-build-root> holds a built `grub-mkimage` (any of the builds); the install roots are `make install` prefixes.
Produces  grub/bin/i386-pc/eltorito.img + modules, grub/bin/efi/BOOTX64.EFI (BOOTIA32.EFI), grub/bin/<platform>/ modules.
Only needed when GRUB itself is rebuilt: the results are committed, `make iso` does not need GRUB tools.
"""
import os, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "grub", "bin")
mkimage_for = lambda root: os.path.join(root, "grub-mkimage")

EARLY = """# AstraSageOS: the UEFI image of GRUB only holds the loader. Find the CD (the volume that has /boot/grub/grub.cfg),
# then continue exactly like the BIOS boot does.
search --no-floppy --file --set=root /boot/grub/grub.cfg
set prefix=($root)/boot/grub
insmod normal
normal
"""

def modules(inst, plat, skip_tests=True):
    src = os.path.join(inst, "lib", "grub", plat)
    dst = os.path.join(OUT, plat)
    shutil.rmtree(dst, ignore_errors=True); os.makedirs(dst)
    n = 0
    for f in sorted(os.listdir(src)):
        if f.endswith(".lst") or f.endswith(".mod"):
            if skip_tests and (f.endswith("_test.mod") or f in ("functional_test.mod", "exfctest.mod", "testload.mod", "testspeed.mod")):
                continue
            shutil.copy(os.path.join(src, f), dst); n += 1
    return os.path.join(src), n

def run(cmd): subprocess.check_call(cmd)

def main():
    if len(sys.argv) < 4: sys.exit(__doc__)
    pc_build, pc_inst, e64_inst = sys.argv[1], sys.argv[2], sys.argv[3]
    e32_inst = sys.argv[4] if len(sys.argv) > 4 else None
    os.makedirs(os.path.join(OUT, "efi"), exist_ok=True)
    early = tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False); early.write(EARLY); early.close()
    # BIOS: core image with the El Torito boot code (cdboot) in front; modules are read from /boot/grub/i386-pc at run time
    src, n = modules(pc_inst, "i386-pc")
    run([mkimage_for(pc_build), "-O", "i386-pc-eltorito", "-d", src, "-p", "/boot/grub", "-o", os.path.join(OUT, "i386-pc", "eltorito.img"),
         "biosdisk", "iso9660", "part_msdos", "part_gpt", "normal", "configfile", "multiboot", "search", "search_fs_file"])
    print("i386-pc: %d files + eltorito.img" % n)
    for inst, plat, name, build in ((e64_inst, "x86_64-efi", "BOOTX64.EFI", e64_inst), (e32_inst, "i386-efi", "BOOTIA32.EFI", e32_inst)):
        if not inst: continue
        src, n = modules(inst, plat)
        mk = os.path.join(os.path.dirname(os.path.abspath(inst)), "b-" + ("efi64" if plat == "x86_64-efi" else "efi32"), "grub-mkimage")
        run([mk, "-O", plat, "-d", src, "-p", "/boot/grub", "-c", early.name, "-o", os.path.join(OUT, "efi", name),
             "part_gpt", "part_msdos", "iso9660", "fat", "normal", "configfile", "multiboot", "search", "search_fs_file", "search_fs_uuid", "gzio", "all_video", "gfxterm", "png"])
        print("%s: %d files + %s" % (plat, n, name))
    os.unlink(early.name)

main()
