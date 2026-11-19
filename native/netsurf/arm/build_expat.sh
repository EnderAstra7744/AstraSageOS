#!/bin/sh
# build_expat.sh <64|32>
A=${1:-64}; if [ $A = 32 ]; then INST=/home/claude/ns/inst32; else INST=/home/claude/ns/inst; fi
S=/home/claude/ns/expat/expat-2.6.2/lib; T=/tmp/expat$A; rm -rf $T; mkdir -p $T
cat > $T/expat_config.h <<'EOC'
#define XML_CONTEXT_BYTES 1024
#define XML_DTD 1
#define XML_GE 1
#define XML_NS 1
#define BYTEORDER 1234
#define HAVE_MEMMOVE 1
#define XML_POOR_ENTROPY 1
EOC
for f in xmlparse xmlrole xmltok; do /home/claude/ns/bin/ascc$A -O2 -DHAVE_EXPAT_CONFIG_H -DXML_STATIC -I$T -I$S -c $S/$f.c -o $T/$f.o || exit 1; done
llvm-ar rcs $T/libexpat.a $T/*.o
mkdir -p $INST/lib $INST/include; cp $T/libexpat.a $INST/lib/; cp $S/expat.h $S/expat_external.h $INST/include/
cat > $INST/lib/pkgconfig/expat.pc <<EOP
prefix=$INST
Name: expat
Description: expat
Version: 2.6.2
Libs: -L$INST/lib -lexpat
Cflags: -I$INST/include
EOP
echo built $A
