#!/bin/sh
# builds a native program for ARM: build_arm.sh <arm64|arm32> <out.elf> <sources...>  (clang + ld.lld, own libc headers)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
A=$1; OUT=$2; shift 2
RES=$(clang -print-resource-dir)/include
case $A in
  arm64) T="--target=aarch64-none-elf -mstrict-align"; LDS=app_arm64.ld ;;
  arm32) T="--target=armv7a-none-eabi -mcpu=cortex-a15 -mfpu=vfpv4-d16 -mfloat-abi=hard -marm -mno-unaligned-access"; LDS=app_arm32.ld ;;
  *) echo "arch?"; exit 1 ;;
esac
CF="$T -std=gnu11 -O2 -Wall -Wno-unused-function -ffreestanding -fno-builtin -fno-builtin-memcpy -fno-builtin-memset -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdinc -isystem $HERE/include -isystem $RES -I$HERE/.. ${EXTRA_CF:-}"
D=/tmp/ncb_$A; mkdir -p $D
OBJS=""
for s in "$@" "$HERE/libc.c"; do o=$D/$(basename "$s" .c).o; clang $CF -c "$s" -o "$o"; OBJS="$OBJS $o"; done
if [ $A = arm32 ]; then clang $CF -I"$HERE/../../kernel" -c "$HERE/rt_arm32.c" -o $D/rt.o; OBJS="$OBJS $D/rt.o"; fi
ld.lld -T "$HERE/../$LDS" -nostdlib -z noexecstack -z norelro --build-id=none -o "$OUT" $OBJS
