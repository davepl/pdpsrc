#! /bin/sh
#	$NetBSD: siglist.sh,v 1.8 2008/10/25 22:18:15 apb Exp $
#
# Script to generate a sorted, complete list of signals, suitable
# for inclusion in trap.c as array initializer.
#
# The Ritchie and GCC versions of CPP differ in how they handle the
# macro expansion of the signal names. Specifically, GCC adds
# newlines, which this script joins back together.

set -e

: ${AWK=awk}
: ${SED=sed}

in=tmpi$$.c
out=tmpo$$.c
ecode=1
trapsigs='0 1 2 13 15'
trap 'rm -f $in $out; trap 0; exit $ecode' $trapsigs

CPP="${1-cc -E}"

(trap $trapsigs;
 echo '#include "sh.h"';
 echo '	{ QwErTy SIGNALS , "DUMMY" , "hook for number of signals" },';
 ${SED} -e '/^[	 ]*#/d' -e 's/^[	 ]*\([^ 	][^ 	]*\)[	 ][	 ]*\(.*[^ 	]\)[ 	]*$/#ifdef SIG\1\
	{ QwErTy .signal = SIG\1 , .name = "\1", .mess = "\2" },\
#endif/') > $in
$CPP $in  | ${SED} -e '/^[	 ]*#/d' -e '/^[ 	]*$/d' > $out
${SED} -n ': again
	/{ QwErTy.*}/s/{ QwErTy/{/p
	/{ QwErTy/ {
		N
		s/\n//
		b again
	}' < $out | ${AWK} '{print NR, $0}' | sort +0n | sort +4n |
    ${SED} 's/^[0-9]* //' |
    ${AWK} 'BEGIN { last=0; nsigs=0; }
	{
	    if ($4 ~ /^[0-9][0-9]*$/ && $5 == ",") {
		n = $4;
		if (n > 0 && n != last) {
		    while (++last < n) {
			printf "\t{ .signal = %d , .name = NULL, .mess = `Signal %d` } ,\n", last, last;
		    }
		    print;
		}
	    }
	}' |
    tr '`' '"' | grep -v '"DUMMY"' |
    ${SED} -e 's/.signal = //' -e 's/.name = //' -e 's/.mess = //' -e 's/  */ /g' -e 's/^[ 	]*{/    {/'
ecode=0
