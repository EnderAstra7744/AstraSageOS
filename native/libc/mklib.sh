#!/bin/sh
# builds libasc.a (libc + zlib shim + posix leftovers) into <outdir>
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-/home/claude/nsb/inst/lib}
mkdir -p "$OUT" /tmp/asclib
for f in libc zshim posix; do /home/claude/nsb/asgcc -std=gnu11 -O2 -Wall -c "$HERE/$f.c" -o /tmp/asclib/$f.o; done
rm -f "$OUT/libasc.a"; ar rcs "$OUT/libasc.a" /tmp/asclib/*.o
