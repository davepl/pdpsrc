#! /bin/sh -
# Keep conditionals out of make's -e shell: the native shell exits on a
# false test even when that test is the condition of an if statement.
cc=$1
cflags=$2
pdpflags=$3
if test -n "$pdpflags"; then
    exec $cc $pdpflags $cflags -o seektest seektest.c
fi
if test -x compiler/c0 && test -x compiler/c1; then
    exec $cc -t01p -B./compiler/ $cflags -o seektest seektest.c
fi
if test -x ../ksh/native-tools/c0 && test -x ../ksh/native-tools/c1; then
    exec /bin/sh ./build-mentec.sh CC="$cc" CFLAGS="$cflags" all
fi
exec $cc -t1p -B../tools/ $cflags -o seektest seektest.c
