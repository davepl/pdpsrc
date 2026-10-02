#!/bin/sh
# Native 2.11BSD formatter regression: use the old Bourne shell's syntax.
# Keep all generated files in a newly created private directory. A failed
# mkdir must stop us before any cleanup trap or generated-file write.

# Run from bsd/top; the native installation need not provide dirname.
source_dir=`pwd`
if test -f "$source_dir/top.c" && test -f "$source_dir/tests/fixed-test.c"
then
    :
else
    echo "run this script from the bsd/top directory" >&2
    exit 1
fi
testdir=/tmp/top-fixed.$$
umask 077
if mkdir "$testdir"
then
    :
else
    echo "cannot create private test directory $testdir" >&2
    exit 1
fi
# This old shell can reset $? before an EXIT trap. Track success explicitly
# so cleanup cannot turn a failed compile or test into a successful exit.
test_status=1
trap '
    cd /
    rm -f "$testdir/fixed-under-test.h" "$testdir/fixed-test.c" \
        "$testdir/fixed-test.o" "$testdir/fixed-test" "$testdir/core"
    rmdir "$testdir"
    exit $test_status
' 0
trap 'exit 1' 1 2 3 15

# Compile the production function, not a second implementation. The golden
# cases now execute with the PDP-11's 16-bit int, 32-bit long and native libc.
(
    echo 'static char *'
    sed -n '/^fixed(buf, value, scale, digits)/,/^}/p' "$source_dir/top.c"
) > "$testdir/fixed-under-test.h" || exit 1
cp "$source_dir/tests/fixed-test.c" "$testdir/fixed-test.c" || exit 1
cd "$testdir" || exit 1
cc -O -i -o fixed-test fixed-test.c || exit 1
./fixed-test || exit 1
echo "native top fixed-point tests passed"
test_status=0
exit 0
