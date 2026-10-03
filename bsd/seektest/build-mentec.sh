#!/bin/sh
# Reuse the private compiler passes already built for ksh on mentec.
# No system compiler or library is replaced.
passes=$PDP_COMPILER_PASSES
if test -z "$passes"; then
    passes=../ksh/native-tools
fi
if test ! -x "$passes/c0" || test ! -x "$passes/c1"; then
    echo "Missing private c0/c1: set PDP_COMPILER_PASSES to their directory." >&2
    exit 1
fi
passes=`cd "$passes" && pwd` || exit 1
if test ! -d compiler; then
    mkdir compiler || exit 1
fi
cp ../tools/cpp compiler/cpp || exit 1
chmod 755 compiler/cpp || exit 1
for pass in c0 c1; do
    rm -f compiler/$pass
    ln -s "$passes/$pass" compiler/$pass || exit 1
done
if test $# -eq 0; then
    exec make PDPFLAGS='-t01p -B./compiler/' all
fi
exec make PDPFLAGS='-t01p -B./compiler/' "$@"
