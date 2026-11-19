#!/bin/sh
# mk.sh <64|32> : builds the NetSurf objects for that CPU into netsurf/build-<A>
A=${1:-64}
if [ $A = 32 ]; then INST=/home/claude/ns/inst32; else INST=/home/claude/ns/inst; fi
cd /home/claude/ns/netsurf
rm -rf build/Linux-framebuffer
PKG_CONFIG_LIBDIR=$INST/lib/pkgconfig PATH=/home/claude/ns/bin:$PATH make -k -j4 TARGET=framebuffer CC=/home/claude/ns/bin/ascc$A HOST_CC=gcc NETSURF_FB_RESPATH=/res PREFIX=$INST NSSHARED=/home/claude/ns/buildsystem > /tmp/ns_make_$A.log 2>&1
grep "error:" /tmp/ns_make_$A.log | grep -v "ld.lld\|clang: error" | sort | uniq | head -${2:-30}
rm -rf build-$A; cp -r build/Linux-framebuffer build-$A; echo "objects: $(ls build-$A | wc -l)"
