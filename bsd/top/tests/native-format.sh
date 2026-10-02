#!/bin/sh
# Native 2.11BSD checks. Run from bsd/top; dirname and mktemp need not exist.
source_dir=`pwd`
if test -f "$source_dir/top.c" && test -f "$source_dir/format.h" && \
    test -f "$source_dir/tests/format-test.c" && \
    test -f "$source_dir/tests/row-test.c" && \
    test -f "$source_dir/tests/fixed-test.c"
then
    :
else
    echo "run this script from the bsd/top directory" >&2
    exit 1
fi

# A failed mkdir must stop us before any generated write or cleanup trap.
# Old /bin/sh can reset $? before the EXIT trap runs. Keep an explicit
# nonzero status until every check passes; cleanup must not hide failure.
testdir=/tmp/top-format.$$
umask 077
if mkdir "$testdir"
then
    :
else
    echo "cannot create private test directory $testdir" >&2
    exit 1
fi
test_status=1
trap '
    cd /
    rm -f "$testdir/format.h" "$testdir/row-block.h" \
        "$testdir/fixed-body.h" "$testdir/fixed-under-test.h" \
        "$testdir/format-test.c" "$testdir/format-test.o" "$testdir/format-test" \
        "$testdir/row-test.c" "$testdir/row-test.o" "$testdir/row-test" \
        "$testdir/fixed-test.c" "$testdir/fixed-test.o" "$testdir/fixed-test" \
        "$testdir/core"
    rmdir "$testdir"
    exit $test_status
' 0
trap 'exit 1' 1 2 3 15

# Use simple sed ranges accepted by the native tools. An empty extraction
# is an error; the test must never succeed without the production function.
sed -n '/^fixed(buf, value, scale, digits)/,/^}/p' \
    "$source_dir/top.c" > "$testdir/fixed-body.h" || exit 1
sed -n '/line_start(&row_text, line, sizeof(line));/,/line_finish(&row_text);/p' \
    "$source_dir/top.c" > "$testdir/row-block.h" || exit 1
if test -s "$testdir/fixed-body.h" && test -s "$testdir/row-block.h"
then
    :
else
    echo "cannot find the production formatting functions" >&2
    exit 1
fi
(
    echo 'static char *'
    cat "$testdir/fixed-body.h"
) > "$testdir/fixed-under-test.h" || exit 1
cp "$source_dir/format.h" "$testdir/format.h" || exit 1
cp "$source_dir/tests/format-test.c" "$testdir/format-test.c" || exit 1
cp "$source_dir/tests/row-test.c" "$testdir/row-test.c" || exit 1
cp "$source_dir/tests/fixed-test.c" "$testdir/fixed-test.c" || exit 1
cd "$testdir" || exit 1

# Each executable prints its own PASS marker. The final marker appears only
# after all three ran successfully, with native int/long/library behavior.
cc -O -i -DNATIVE -o format-test format-test.c || exit 1
cc -O -i -o row-test row-test.c || exit 1
cc -O -i -o fixed-test fixed-test.c || exit 1
./format-test || exit 1
./row-test || exit 1
./fixed-test || exit 1
echo "all native top formatting tests passed"
test_status=0
exit 0
