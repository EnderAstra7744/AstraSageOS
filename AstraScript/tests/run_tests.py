#!/usr/bin/env python3
"""Runs tests/t_*.as with build/as2_host; a test passes when its last output line is '<name> ok' (t_errors: expected text)."""
import glob, subprocess, os, sys
here = os.path.dirname(os.path.abspath(__file__)); host = os.path.join(here, "..", "build", "as2_host")
bad = 0; import tempfile; work = tempfile.mkdtemp()
for f in sorted(glob.glob(os.path.join(here, "t_*.as"))):
    out = subprocess.run([host, f], capture_output=True, text=True, timeout=30, cwd=work).stdout
    name = os.path.basename(f)[2:-3]
    if name == "errors":
        ok = "in\nbody\nout" in out and "caught2 boom" in out and "ZeroDivisionError: division by zero" in out
    else:
        ok = out.strip().endswith("ok") and "[Error]" not in out
    print(("PASS " if ok else "FAIL ") + name)
    if not ok: bad += 1; print(out[-400:])
print("AstraScript engine 2: %s" % ("all passed" if not bad else "%d failed" % bad)); sys.exit(1 if bad else 0)
