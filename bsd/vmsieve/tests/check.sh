#!/bin/sh
# Cross a page, the eight-page resident cache, and a 512-page backing space.
for pair in '2 1' '16385 1900' '131071 12251' '8388617 564164' '50000000 3001134'
do
    set $pair
    output=`./vmsieve -1 -l "$1"` || exit 1
    count=`echo "$output" | awk '/^Count of primes found/ { print $NF }'`
    if test "$count" != "$2"; then
        echo "limit $1: expected $2, got $count" >&2
        exit 1
    fi
done

primes=`./vmsieve -1 -q -p -l 49` || exit 1
test "$primes" = "2 3 5 7 11 13 17 19 23 29 31 37 41 43 47 " || exit 1
output=`./vmsieve -s 1 -l 1000` || exit 1
passes=`echo "$output" | awk '/^Number of passes/ { print $NF }'`
test "$passes" -gt 1 || exit 1
echo "$output" | grep '^Prime validator .*PASS$' >/dev/null || exit 1
if ./vmsieve -q -1 -l 50000001 >/dev/null 2>&1; then exit 1; fi
echo "vmsieve checks passed"
