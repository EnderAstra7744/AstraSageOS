#!/bin/sh
# link.sh <64|32> : -> /home/claude/ns/netsurf<A>.elf
set -e
A=${1:-64}
cd /home/claude/ns
if [ $A = 32 ]; then INST=/home/claude/ns/inst32; LDS=app_arm32.ld; else INST=/home/claude/ns/inst; LDS=app_arm64.ld; fi
L=$INST/lib; N=/home/claude/AstraSageOS/native; T=/tmp/asc$A
rm -rf $T; mkdir -p $T
for f in libc posix regex; do bin/ascc$A -std=gnu11 -c $N/libc/$f.c -o $T/$f.o; done
[ $A = 32 ] && bin/ascc32 -c $N/libc/rt_arm32.c -o $T/rt.o
python3 $N/netsurf/mkres.py netsurf $T/nsres.c
bin/ascc$A -c $T/nsres.c -o $T/nsres.o
ld.lld -T $N/$LDS -nostdlib -z norelro --allow-multiple-definition -z noexecstack --build-id=none -o netsurf$A.full netsurf/build-$A/*.o $T/*.o \
  --whole-archive $L/libnsfb.a --no-whole-archive $L/libdom.a $L/libsvgtiny.a $L/libexpat.a $L/libhubbub.a $L/libcss.a $L/libparserutils.a $L/libwapcaplet.a $L/libnsbmp.a $L/libnsgif.a $L/libnsutils.a $L/libpng16.a $L/libjpeg.a $L/libz.a
llvm-strip -o netsurf$A.elf netsurf$A.full
ls -l netsurf$A.elf
