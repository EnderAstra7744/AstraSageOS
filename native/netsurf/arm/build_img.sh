#!/bin/sh
# build_img.sh <64|32> : zlib + libpng + libjpeg(-turbo) as static libs into inst[32]
A=${1:-64}
if [ $A = 32 ]; then INST=/home/claude/ns/inst32; SZ=4; else INST=/home/claude/ns/inst; SZ=8; fi
CC=/home/claude/ns/bin/ascc$A; cd /home/claude/ns; W=/tmp/img$A; rm -rf $W; mkdir -p $W/z $W/p $W/j
for f in gzclose gzlib gzread gzwrite adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil; do $CC -DHAVE_STDDEF_H -DHAVE_STDARG_H -DHAVE_UNISTD_H -include unistd.h -include fcntl.h -Izlib -c zlib/$f.c -o $W/z/$f.o || exit 1; done
llvm-ar rcs $INST/lib/libz.a $W/z/*.o; cp zlib/zlib.h zlib/zconf.h $INST/include/
cp libpng/pnglibconf.h.prebuilt $W/pnglibconf.h
for f in pngsimd png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil; do $CC -DPNG_ARM_NEON_OPT=0 -DPNG_ARM_NEON_IMPLEMENTATION=0 -I$W -Ilibpng -c libpng/$f.c -o $W/p/$f.o || exit 1; done
llvm-ar rcs $INST/lib/libpng16.a $W/p/*.o; cp libpng/png.h libpng/pngconf.h $W/pnglibconf.h $INST/include/
cp $W/pnglibconf.h $INST/include/; 
cp /tmp/jb/jconfig.h /tmp/jb/jconfigint.h /tmp/jb/jversion.h $W/j/; sed -i "s/#define SIZEOF_SIZE_T  8/#define SIZEOF_SIZE_T  $SZ/" $W/j/jconfigint.h
for s in $(cat /home/claude/ns/jsrc.txt) src/jaricom.c src/jcarith.c src/jdarith.c; do $CC -I$W/j -Ilibjpeg-turbo/src -c libjpeg-turbo/$s -o $W/j/$(echo $s | tr / _).o 2>&1 | grep error | head -2; done
ls libjpeg-turbo/src/jsimd* 2>/dev/null
llvm-ar rcs $INST/lib/libjpeg.a $W/j/*.o; cp libjpeg-turbo/src/jpeglib.h libjpeg-turbo/src/jmorecfg.h libjpeg-turbo/src/jerror.h $W/j/jconfig.h $INST/include/
ls -l $INST/lib/libz.a $INST/lib/libpng16.a $INST/lib/libjpeg.a
