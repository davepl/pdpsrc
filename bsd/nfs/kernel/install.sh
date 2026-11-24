#!/bin/sh
# Install the nfsdisk driver into a 2.11BSD kernel tree (default MINERVA).
# Run as root on the PDP-11 from bsd/nfs/kernel.  Originals are kept as
# *.pre-nfs; the running kernel is not replaced (do that with
# "make install" in the configuration directory after checking the build).
#
#	sh install.sh [CONFIG]
#
SYS=/usr/src/sys
CONF=${1-MINERVA}
here=`pwd`

test -f nfsdisk.c -a -f nfsdisk.h -a -f kernel.diff || {
	echo "run from bsd/nfs/kernel"; exit 1; }
test -d $SYS/$CONF || { echo "no $SYS/$CONF"; exit 1; }

cd $SYS || exit 1
for f in pdp/conf.c sys/ufs_alloc.c $CONF/Makefile $CONF/Make.sys; do
	test -f $f.pre-nfs || cp -p $f $f.pre-nfs || exit 1
done
if grep -s NFSDISK pdp/conf.c >/dev/null; then
	echo "conf.c already patched"
else
	# kernel.diff was made against MINERVA; adjust for another config.
	sed "s;MINERVA/;$CONF/;g" $here/kernel.diff > /tmp/nfskern.diff
	(cd /usr/src && patch -p0 < /tmp/nfskern.diff) || exit 1
	rm -f /tmp/nfskern.diff
	grep -s NFSDISK pdp/conf.c >/dev/null &&
	    grep -s NFSDISK sys/ufs_alloc.c >/dev/null &&
	    grep -s nfsdisk $CONF/Make.sys >/dev/null || {
		echo "kernel.diff did not apply"; exit 1; }
fi
# The config's Makefile differs from machine to machine, so it is edited
# in place: -DNFSDISK on the OPTS line, and the driver (about 2 KB of
# text) in the first overlay the config does not use yet.
if grep -s nfsdisk $CONF/Makefile >/dev/null; then
	echo "$CONF/Makefile already has the driver"
else
	ov=
	for n in 10 11 12 13 14 15; do
		if grep -s "^OV$n=" $CONF/Makefile >/dev/null; then :; else
			ov=$n; break
		fi
	done
	test -n "$ov" || { echo "no free overlay in $CONF/Makefile"; exit 1; }
	sed -e "s/^OPTS=.*/& -DNFSDISK/" -e "/^KERNOBJ=/i\\
OV$ov=	nfsdisk.o" $CONF/Makefile.pre-nfs > $CONF/Makefile || exit 1
	grep -s "DNFSDISK" $CONF/Makefile >/dev/null &&
	    grep -s "^OV$ov=	nfsdisk.o" $CONF/Makefile >/dev/null || {
		echo "could not edit $CONF/Makefile"; exit 1; }
	echo "driver placed in overlay OV$ov"
fi
cp $here/nfsdisk.c sys/nfsdisk.c || exit 1
cp $here/nfsdisk.h h/nfsdisk.h || exit 1

cd /dev || exit 1
for u in 0 1 2 3; do
	test -b nfs$u || mknod nfs$u b 13 $u
	test -c nfsd$u || mknod nfsd$u c 27 $u
done
chmod 600 nfs? nfsd?
echo "now: cd $SYS/$CONF; make; then install unix and netnix together"
