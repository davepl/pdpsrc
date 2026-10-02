#!/bin/ksh
set -e

: > final-regular.log
for trial in 1 2 3
do
    echo trial=$trial >> final-regular.log
    case $trial in
    2) binaries="./final/top /usr/ucb/top" ;;
    *) binaries="/usr/ucb/top ./final/top" ;;
    esac
    for binary in $binaries
    do
        ./topbench 120 $binary -s1 >> final-regular.log
        echo completed-regular-trial-$trial-$binary
    done
done
echo final-series-complete
