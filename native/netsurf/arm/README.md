# NetSurf for the ARM kernels (ARM64 / ARM32)

Rebuilds NetSurf (framebuffer front end) as a native AstraSageOS program for ARM. The tree is not part of this
repository: it is cloned from GitHub (versions.txt lists the commits that were used).

    mkdir -p /home/claude/ns && cd /home/claude/ns
    for r in netsurf buildsystem libcss libdom libhubbub libparserutils libwapcaplet libnsbmp libnsgif libnsfb libnsutils; do
        git clone --depth 1 https://github.com/netsurf-browser/$r; done
    (cd netsurf && git apply <repo>/native/netsurf/arm/netsurf.patch)
    (cd libnsfb && git apply <repo>/native/netsurf/arm/libnsfb.patch)
    cp overlay/astra.c overlay/astra.h netsurf/content/fetchers/     # http/https fetcher (api->http_fetch)
    cp overlay/nsfb_astra.c libnsfb/src/surface/astra.c               # window / input surface (api->win_*)
    cp overlay/Makefile.config netsurf/
    cp ascc64 ascc32 gperf buildlibs.sh mk.sh link.sh .   (and: chmod +x; gperf must be on PATH = ns/bin)
    ./buildlibs.sh 64 ; HOSTT=arm-linux-gnueabihf ./buildlibs.sh 32
    ./mk.sh 64 && ./link.sh 64      # -> netsurf64.elf  (ASPI name: netsurf-arm64.elf)
    ./mk.sh 32 && ./link.sh 32      # -> netsurf32.elf  (ASPI name: netsurf-arm32.elf)

Needs clang + ld.lld, perl, bison, a host libpng (the build converts the toolbar icons). `gperf` here is a small
stand-in (hubbub's element table only) because the real gperf is not installed in the build sandbox.

Limits: none of the earlier ones remain - SVG (libsvgtiny + expat), POST/multipart/cookies/auth go through api->http_request (API v6), PNG/JPEG (libpng, libjpeg-turbo), JavaScript (Duktape + nsgenbind bindings), POSIX regex (native/libc/regex.c), dynamic page buffer, Ctrl shortcuts. Not run on real ARM kernels (no emulator in the build sandbox).
Build order additions for SVG: build_expat.sh <64|32>, then libdom with WITH_EXPAT_BINDING=yes, then libsvgtiny (colors.gperf works with the gperf stand-in).
