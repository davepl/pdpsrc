#!/bin/sh
# Output/argument regression checks; run in the build directory.
p=./pdpfetch
base=/tmp/pdpfetch-test.$$
umask 077
mkdir $base || exit 1
trap 'status=$?; trap 0; rm -rf $base; exit $status' 0
trap 'exit 1' 1 2 3 15
$p -p > $base/plain || exit 1
$p -n > $base/logo || exit 1
$p --version > $base/version || exit 1
$p --help > $base/help || exit 1
if $p --not-an-option > $base/bad 2>&1; then exit 1; fi
TERM=xterm
export TERM
$p > $base/redirected || exit 1
NO_COLOR=1
export NO_COLOR
$p > $base/nocolor || exit 1
$p -c > $base/color || exit 1
$p -p -c > $base/plaincolor || exit 1
TERM='bad
terminal'
export TERM
$p -p > $base/sanitized || exit 1
# AWK inspects raw output without depending on a modern grep or printf.
awk '
BEGIN { esc = sprintf("%c", 27); bad = 0 }
index($0, esc) > 0 { bad = 1 }
END { if (bad) exit 1; exit 0 }
' $base/plain $base/logo $base/redirected $base/nocolor $base/plaincolor || exit 1
awk 'BEGIN { esc = sprintf("%c",27) } index($0,esc) > 0 { found=1 } END { if (found) exit 0; exit 1 }' $base/color || exit 1
awk 'length($0)>79 { bad=1 } END { if (bad) exit 1; exit 0 }' $base/logo || exit 1
grep 'bad?terminal' $base/sanitized > /dev/null || exit 1
grep 'pdpfetch 1.0' $base/version > /dev/null || exit 1
grep 'Memory' $base/plain > /dev/null || exit 1
echo 'pdpfetch output checks passed'
