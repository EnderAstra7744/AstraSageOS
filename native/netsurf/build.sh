#!/bin/sh
# Builds NetSurf for AstraSageOS: native/netsurf/build.sh [out.elf]
# Needs the unpacked netsurf-all-3.11 tree in /home/claude/ns_src/netsurf-all-3.11 and the libraries built into
# /home/claude/nsb/inst (see native/netsurf/README.md).
set -e
SRC=/home/claude/ns_src/netsurf-all-3.11
P=/home/claude/nsb/inst
OUT=${1:-/home/claude/nsb/netsurf.elf}
HERE=$(cd "$(dirname "$0")" && pwd)
export PKG_CONFIG_PATH=$P/lib/pkgconfig PATH=/home/claude/nsb/bin:$PATH
sh "$HERE/../libc/mklib.sh" $P/lib
cp "$HERE/../as_api.h" $P/include/as_api.h
python3 "$HERE/mkres.py" $SRC/netsurf /tmp/nsres.c
/home/claude/nsb/asgcc -std=gnu11 -O2 -c /tmp/nsres.c -o /tmp/nsres.o
(cd $SRC/libnsfb && make install CC=/home/claude/nsb/asgcc AR=ar HOST=i686-linux-gnu BUILD=x86_64-linux-gnu PREFIX=$P NSSHARED=$SRC/buildsystem COMPONENT_TYPE=lib-static DESTDIR= WARNFLAGS="-Wall -W -Wno-error" >/tmp/nsfb_make.log 2>&1)
cd $SRC/netsurf; rm -rf build/Linux-framebuffer
make -k TARGET=framebuffer CC=/home/claude/nsb/asgcc HOST_CC=gcc PREFIX=$P NSSHARED=$SRC/buildsystem -j8 >/tmp/ns_make.log 2>&1 || true
L=$P/lib
ld -m elf_i386 -T "$HERE/../app.ld" -nostdlib --allow-multiple-definition -z noexecstack --build-id=none -o $OUT.full build/Linux-framebuffer/*.o /tmp/nsres.o \
   --whole-archive $L/libnsfb.a --no-whole-archive $L/libdom.a $L/libhubbub.a $L/libcss.a $L/libparserutils.a $L/libwapcaplet.a \
   $L/libnsbmp.a $L/libnsgif.a $L/libnspsl.a $L/libnsutils.a $L/libutf8proc.a $L/libpng.a $L/libjpeg.a $L/libz.a $L/libasc.a 2>&1 | grep -v RWX || true
strip -o $OUT $OUT.full
ls -la $OUT
