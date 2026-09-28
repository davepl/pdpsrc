#!/bin/sh
# Run on a modern development host; no terminal or extra libraries needed.
set -eu
cd "$(dirname "$0")"
testdir=$(mktemp -d "${TMPDIR:-/tmp}/pdpsrc-top-tests.XXXXXX")
trap 'rm -rf "$testdir"' 0
trap 'exit 1' HUP INT TERM
${CC:-cc} -std=gnu89 -Wall -Wextra -Wno-unused-parameter \
    -Wno-deprecated-non-prototype -Iinclude screen-test.c -o "$testdir/screen-test"
"$testdir/screen-test"

# fixed() has no kernel dependencies. Extract its production body rather
# than maintaining a second implementation just for the host tests.
awk '
    /^fixed\(buf, value, scale, digits\)/ {
        found = 1
        print "static char *"
    }
    found { print }
    found && /^}$/ { closed = 1; exit }
    END { if (!closed) exit 1 }
' ../top.c > "$testdir/fixed-under-test.h"
${CC:-cc} -std=gnu89 -Wall -Wextra -Wno-deprecated-non-prototype \
    -I"$testdir" fixed-test.c -o "$testdir/fixed-test"
"$testdir/fixed-test"
