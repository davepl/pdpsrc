#!/bin/sh
# Modern development host: no terminal or extra libraries are needed.
set -eu
cd "$(dirname "$0")"
testdir=$(mktemp -d "${TMPDIR:-/tmp}/pdpsrc-top-tests.XXXXXX")
trap 'test_status=$?; rm -rf "$testdir"; exit "$test_status"' 0
trap 'exit 1' HUP INT TERM

# Compile actual source against fake external effects, not a second renderer.
${CC:-cc} -std=gnu89 -Wall -Wextra -Wno-unused-parameter \
    -Wno-deprecated-non-prototype -Iinclude screen-test.c -o "$testdir/screen-test"
"$testdir/screen-test"

# These helpers have no kernel dependencies. Extract their current bodies so
# implementation changes cannot silently leave a copied test version behind.
extract_helper()
{
    awk -v name="$1" '
        $0 ~ ("^" name "\\(") {
            found = 1
            print "static char *"
        }
        found { print }
        found && /^}$/ { closed = 1; exit }
        END { if (!closed) exit 1 }
    ' ../top.c > "$testdir/$2"
}
extract_helper fixed fixed-under-test.h
extract_helper header_text header-under-test.h
awk '
    /line_start\(&row_text, line, sizeof\(line\)\);/ { found = 1 }
    found { print }
    found && /line_finish\(&row_text\);/ { closed = 1; exit }
    END { if (!closed) exit 1 }
' ../top.c > "$testdir/row-block.h"

for test in fixed format row header
do
    ${CC:-cc} -std=gnu89 -Wall -Wextra -Wno-unused-parameter \
        -Wno-deprecated-non-prototype -I.. -I"$testdir" \
        "$test-test.c" -o "$testdir/$test-test"
    "$testdir/$test-test"
done
echo "all host top tests passed"
