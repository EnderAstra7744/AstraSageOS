#!/bin/sh
# Builds the tcc (TinyCC) backend as an i386 cross compiler. Needs network access to GitHub and a C compiler.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D="$ROOT/build/tcc"; mkdir -p "$D"
[ -d "$D/src" ] || git clone --depth 1 https://github.com/TinyCC/tinycc.git "$D/src"
cd "$D/src" && ./configure --cpu=x86 --enable-cross --prefix="$D/inst" >/dev/null && make cross-i386 -j4 >/dev/null
cp i386-tcc "$D/i386-tcc"
echo "built $D/i386-tcc"
