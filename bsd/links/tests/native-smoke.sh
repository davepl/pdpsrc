#!/bin/sh
# Run from the port root. Optional argument is tests/serve.py's HTTP base URL.
if test ! -d native-tmp; then mkdir native-tmp || exit 1; fi
if test ! -d native-tmp/test-home; then mkdir native-tmp/test-home || exit 1; fi
HOME=`pwd`/native-tmp/test-home
export HOME
B=./upstream/links-pdp
$B -no-connect -dump tests/page.html > native-tmp/test-local.txt || exit 1
grep Minerva native-tmp/test-local.txt >/dev/null || exit 1
grep PDP-11/73 native-tmp/test-local.txt >/dev/null || exit 1
grep 'Next page' native-tmp/test-local.txt >/dev/null || exit 1
$B -no-connect -source tests/page.html > native-tmp/test-source.html || exit 1
cmp tests/page.html native-tmp/test-source.html || exit 1
echo 'PASS: native local HTML, table, links and byte-exact source'
if test -n "$1"; then
    $B -no-connect -dump "$1/page.html" > native-tmp/test-http.txt || exit 1
    cmp native-tmp/test-local.txt native-tmp/test-http.txt || exit 1
    $B -no-connect -dump "$1/redirect" > native-tmp/test-redirect.txt || exit 1
    cmp native-tmp/test-local.txt native-tmp/test-redirect.txt || exit 1
    $B -no-connect -dump "$1/chunked" > native-tmp/test-chunked.txt || exit 1
    grep 'Chunked works' native-tmp/test-chunked.txt >/dev/null || exit 1
    $B -no-connect -dump "$1/utf8" > native-tmp/test-utf8.txt || exit 1
    grep 'cafe - PDP-11' native-tmp/test-utf8.txt >/dev/null || exit 1
    echo 'PASS: native HTTP, redirect, chunked framing and UTF-8 transliteration'
fi

exit 0
