#!/usr/bin/env python3
"""Runs the 32-bit test program in compatibility mode through arch/x86_64/compat.S (the bridge the 64-bit kernel uses)."""
import os, subprocess, sys
R = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
B = os.path.join(R, "build", "n64"); os.makedirs(B, exist_ok=True)
def run(*a):
    r = subprocess.run(a, cwd=R, capture_output=True, text=True)
    if r.returncode: print(r.stdout + r.stderr); sys.exit("run_native64: failed: " + " ".join(a[:3]))
    return r
cf = ["-m32", "-std=gnu11", "-O2", "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie", "-fno-stack-protector", "-fno-asynchronous-unwind-tables", "-mno-sse", "-mno-mmx"]
run("gcc", *cf, "-c", "sys/tests/native64_prog.c", "-o", B + "/prog.o")
run("ld", "-m", "elf_i386", "-T", "native/app.ld", "-nostdlib", "-z", "noexecstack", "--build-id=none", "-o", B + "/prog.elf", B + "/prog.o")
run("gcc", "-m64", "-DCS64=0x33", "-DCS32=0x23", "-DDS32=0x2b", "-c", "arch/x86_64/compat.S", "-o", B + "/compat.o")
run("gcc", "-m64", "-O1", "-no-pie", "-static", "sys/tests/native64_test.c", B + "/compat.o", "-o", B + "/test")
r = subprocess.run([B + "/test", B + "/prog.elf"], capture_output=True, text=True)
print(r.stdout.strip() or r.stderr.strip())
print("native64 (compatibility mode): " + ("OK" if r.returncode == 0 else "FAILED"))
sys.exit(r.returncode)
