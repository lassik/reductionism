#!/bin/sh
set -eu
cd "$(dirname "$0")"
echo "Entering directory $PWD"
set -x
CC=clang
CFLAGS="-Weverything -Wno-unused-function -fsanitize=address -O2"
LFLAGS="-lm"
gsi forthc.scm
#$CC $LFLAGS $CFLAGS -o forthc forthc.c
#./forthc scheme.4th >scheme.h
$CC $LFLAGS $CFLAGS -o scheme forth.c
