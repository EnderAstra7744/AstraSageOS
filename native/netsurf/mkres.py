#!/usr/bin/env python3
"""Packs the NetSurf resource files into a C file (__as_res table) that the libc serves as /res/..."""
import sys, os
src, out = sys.argv[1], sys.argv[2]
R = "frontends/framebuffer/res/"
files = [("/res/default.css", R + "default.css"), ("/res/quirks.css", R + "quirks.css"),
         ("/res/internal.css", R + "internal.css"), ("/res/adblock.css", R + "adblock.css"),
         ("/res/Messages", R + "Messages"),
         ("/res/welcome.html", R + "welcome.html"), ("/res/credits.html", R + "credits.html"),
         ("/res/licence.html", R + "licence.html"), ("/res/netsurf.png", R + "netsurf.png"),
         ("/res/favicon.png", R + "favicon.png")]
o = ["struct as_res { const char *name; const unsigned char *data; unsigned len; };\n"]
for i, (n, p) in enumerate(files):
    d = open(os.path.join(src, p), "rb").read()
    o.append("static const unsigned char r%d[] = {%s};\n" % (i, ",".join(str(b) for b in d) if d else "0"))
o.append("const struct as_res __as_res[] = {\n")
for i, (n, p) in enumerate(files):
    o.append('  { "%s", r%d, %d },\n' % (n, i, os.path.getsize(os.path.join(src, p))))
o.append("  { 0, 0, 0 }\n};\n")
open(out, "w").write("".join(o))
