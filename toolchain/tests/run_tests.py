#!/usr/bin/env python3
"""Toolchain tests: every stage is checked separately. Building is never confused with running (running: sys/tests/gui_test.py)."""
import os, subprocess, sys, tempfile, shutil
HERE = os.path.dirname(os.path.abspath(__file__)); TC = os.path.dirname(HERE); ROOT = os.path.dirname(TC)
CC = os.path.join(TC, "as-cc"); CHK = os.path.join(TC, "as-check.py")
tmp = tempfile.mkdtemp(); fails = 0
def check(name, ok, info=""):
    global fails
    print(("PASS: " if ok else "FAIL: ") + name)
    if not ok: fails += 1; print(info[-600:])
def run(*a): return subprocess.run(list(a), capture_output=True, text=True, cwd=tmp)
def w(name, text): p = os.path.join(tmp, name); open(p, "w").write(text); return p

hello = os.path.join(ROOT, "native", "hello.c")
backends = ["gcc", "clang"] + (["tcc"] if os.path.exists(os.path.join(ROOT, "build/tcc/i386-tcc")) else [])
for be in backends:
    out = os.path.join(tmp, "h_%s.elf" % be)
    r = run(CC, "--backend", be, "-Os", "-o", out, hello)
    check("%s: hello.c builds into a valid program" % be, r.returncode == 0 and "as-check: ok" in r.stdout and os.path.getsize(out) <= 4096, r.stdout + r.stderr)
    r = run(sys.executable, CHK, out); check("%s: as-check accepts it" % be, r.returncode == 0, r.stderr)

# stage: syntax error is reported with file and line, and the previous output survives
good = w("good.c", '#include "as_api.h"\nint as_main(const as_api *a, const char *s) { a->puts("v1\\n"); return 0; }\n')
out = os.path.join(tmp, "keep.elf")
r = run(CC, "-o", out, good); check("a small program builds", r.returncode == 0, r.stderr)
before = open(out, "rb").read()
bad = w("bad.c", '#include "as_api.h"\nint as_main(const as_api *a, const char *s) {\n  a->puts("x")\n  return 0;\n}\n')
for be in backends:
    r = run(CC, "--backend", be, "-o", out, bad)
    check("%s: a syntax error names the file and line, build fails" % be, r.returncode != 0 and "bad.c" in r.stderr and ("4" in r.stderr or "3" in r.stderr), r.stderr)
    check("%s: the previous output is not touched by a failed build" % be, open(out, "rb").read() == before)
# stage: link error (missing symbol)
nolink = w("nolink.c", '#include "as_api.h"\nextern int missing_fn(void);\nint as_main(const as_api *a, const char *s) { return missing_fn(); }\n')
r = run(CC, "-o", out, nolink); check("a link error is reported and keeps the old output", r.returncode != 0 and "missing_fn" in r.stderr and open(out, "rb").read() == before, r.stderr)
# stage: the ELF check refuses things the loader would refuse
import struct
bad_elf = os.path.join(tmp, "bad.elf"); data = bytearray(before); struct.pack_into("<H", data, 18, 62); open(bad_elf, "wb").write(data)
r = run(sys.executable, CHK, bad_elf); check("as-check refuses a non-i386 file", r.returncode == 1 and "not i386" in r.stderr, r.stderr)
open(bad_elf, "wb").write(b"hello"); r = run(sys.executable, CHK, bad_elf); check("as-check refuses a non-ELF file", r.returncode == 1, r.stderr)
# C++ (gcc, clang) - freestanding: no exceptions, no RTTI, global constructors run through .init_array
cpp = w("p.cpp", '#include "as_api.h"\nstruct Counter { int n; Counter() : n(7) {} int next() { return ++n; } };\nstatic Counter c;\nextern "C" int as_main(const as_api *a, const char *s) { return c.next() == 8 ? 0 : 1; }\n')
for be in ("gcc", "clang"):
    r = run(CC, "--backend", be, "-o", os.path.join(tmp, "p_%s.elf" % be), cpp); check("%s: C++ program builds" % be, r.returncode == 0, r.stderr)
r = run(CC, "--backend", "tcc", "-o", os.path.join(tmp, "p.elf"), cpp) if "tcc" in backends else None
if r: check("tcc refuses C++ clearly", r.returncode != 0 and "C only" in r.stderr, r.stderr)
# the AstraSageOS C library links (printf-style, malloc, string functions)
#, '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include "as_api.h"\nint main(void);\n')
libmain = w("lm.c", '#include <string.h>\n#include <stdlib.h>\nint strlen_test(void) { char *p = malloc(8); strcpy(p, "abc"); return (int)strlen(p); }\nint main(int argc, char **argv) { return strlen_test() == 3 ? 0 : 1; }\n')
r = run(CC, "--libc", "-o", os.path.join(tmp, "lm.elf"), libmain); check("--libc: a program with main() using malloc/strlen from the C library links", r.returncode == 0, r.stdout + r.stderr)
shutil.rmtree(tmp)
print("toolchain: %s" % ("all passed" if not fails else "%d failed" % fails)); sys.exit(1 if fails else 0)
