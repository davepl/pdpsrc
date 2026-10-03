#!/bin/sh
# Run from the source directory on 2.11BSD as an ordinary user.
# No set -e: the historical 2.11BSD shell mishandles false conditions.
if test ! -f Makefile.mentec; then
    echo 'Run this script from the ksh source directory.'
    exit 1
fi
# mkdep updates Makefile, so select the Mentec variant before generating it.
if test -f Makefile; then
    if cmp -s Makefile Makefile.mentec; then
        :
    else
        echo 'Move the existing Makefile aside before selecting Makefile.mentec.'
        exit 1
    fi
else
    ln -s Makefile.mentec Makefile || exit 1
fi
make clean > build.log 2>&1 || { cat build.log; exit 1; }
(cd native-tools && make clean && make) >> build.log 2>&1 || { cat build.log; exit 1; }
make depend > depend.log 2>&1 || { cat depend.log; exit 1; }
make >> build.log 2>&1 || { cat build.log; exit 1; }
./ksh mentec-tests.ksh > tests.log 2>&1
result=$?
cat tests.log
exit $result
