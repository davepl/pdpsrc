#!/bin/ksh
# Frozen builds, identical PTY settings, and interleaved order.
set -e
: > final-warmup.log
for binary in /usr/ucb/top ./pr13/top ./final/top
do
    TOPBENCH_REFRESHES=300 ./topbench 300 $binary -s1 >> final-warmup.log
done
: > final-forced.log
for trial in 1 2 3 4 5
do
    echo trial=$trial >> final-forced.log
    case $trial in
    2|4) binaries="./final/top ./pr13/top /usr/ucb/top" ;;
    *) binaries="/usr/ucb/top ./pr13/top ./final/top" ;;
    esac
    for binary in $binaries
    do
        TOPBENCH_REFRESHES=3000 ./topbench 300 $binary -s1 >> final-forced.log
    done
    echo completed-forced-trial-$trial
done
: > final-default.log
for trial in 1 2 3 4 5
do
    echo trial=$trial >> final-default.log
    case $trial in
    2|4) binaries="./final/top /usr/ucb/top" ;;
    *) binaries="/usr/ucb/top ./final/top" ;;
    esac
    for binary in $binaries
    do
        ./topbench 3 $binary -s1 >> final-default.log
    done
    echo completed-startup-trial-$trial
done
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
