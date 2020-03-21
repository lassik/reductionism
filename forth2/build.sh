#!/bin/sh
set -eu
cd "$(dirname "$0")"
echo "Entering directory $PWD"
CC=clang
CFLAGS="-Weverything -Werror -Wno-unused-function -pedantic -std=gnu99"
if [ -n "${DEBUG:-}" ]; then
    CFLAGS="$CFLAGS -g -fsanitize=address"
else
    CFLAGS="$CFLAGS -O2"
fi
LFLAGS="-lm"
set -x
cloc -q forth* *.4th || true
$CC $LFLAGS $CFLAGS -o forthc forthc.c
./forthc scheme.4th >scheme.h.new
mv -f scheme.h.new scheme.h
$CC $LFLAGS $CFLAGS -o scheme forth.c
