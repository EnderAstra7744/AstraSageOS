#!/bin/sh
# buildlibs.sh <arch> : builds the NetSurf libraries into inst-<arch>
A=${1:-64}
if [ $A = 32 ]; then INST=/home/claude/ns/inst32; else INST=/home/claude/ns/inst; fi
cd /home/claude/ns
for l in libwapcaplet libparserutils libnsutils libhubbub libcss libdom libnsbmp libnsgif; do
  (cd $l && rm -rf build-* && make install WITH_EXPAT_BINDING=no CC=/home/claude/ns/bin/ascc$A AR=llvm-ar HOST=${HOSTT:-aarch64-linux-gnu} BUILD=x86_64-linux-gnu PREFIX=$INST NSSHARED=/home/claude/ns/buildsystem COMPONENT_TYPE=lib-static DESTDIR= PATH=/home/claude/ns/bin:$PATH PKG_CONFIG_LIBDIR=$INST/lib/pkgconfig PKG_CONFIG_PATH=$INST/lib/pkgconfig >/tmp/lib_$l.log 2>&1 && echo "OK $l" || { echo "FAIL $l"; grep -m5 "error\|Error" /tmp/lib_$l.log; })
done
(cd libnsfb && rm -rf build-* && make install CC=/home/claude/ns/bin/ascc$A AR=llvm-ar HOST=${HOSTT:-aarch64-linux-gnu} BUILD=x86_64-linux-gnu PREFIX=$INST NSSHARED=/home/claude/ns/buildsystem COMPONENT_TYPE=lib-static DESTDIR= PATH=/home/claude/ns/bin:$PATH NSFB_SDL_AVAILABLE=no NSFB_XCB_AVAILABLE=no NSFB_VNC_AVAILABLE=no NSFB_WLD_AVAILABLE=no >/tmp/lib_libnsfb.log 2>&1 && echo "OK libnsfb" || { echo "FAIL libnsfb"; grep -m5 "error" /tmp/lib_libnsfb.log; })
