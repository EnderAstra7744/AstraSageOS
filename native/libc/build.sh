#!/bin/sh
# builds native programs: build.sh <out.elf> <sources...>   (freestanding, 32-bit, own libc headers)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$1; shift
GCCINC=$(gcc -print-file-name=include)
CF="-m32 -std=gnu11 -O2 -Wall -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -nostdinc -isystem $HERE/include -isystem $GCCINC -I$HERE/.. ${EXTRA_CF:-}"
OBJS=""
for s in "$@"; do o=/tmp/ncb_$(basename "$s" .c).o; gcc $CF -c "$s" -o "$o"; OBJS="$OBJS $o"; done
gcc $CF -c "$HERE/libc.c" -o /tmp/ncb_libc.o
ld -m elf_i386 -T "$HERE/../app.ld" -nostdlib -z noexecstack --build-id=none -o "$OUT" /tmp/ncb_libc.o $OBJS
