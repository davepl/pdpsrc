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
cp $here/nfsdisk.c sys/nfsdisk.c || exit 1
cp $here/nfsdisk.h h/nfsdisk.h || exit 1

cd /dev || exit 1
for u in 0 1 2 3; do
	test -b nfs$u || mknod nfs$u b 13 $u
	test -c nfsd$u || mknod nfsd$u c 27 $u
done
chmod 600 nfs? nfsd?
echo "now: cd $SYS/$CONF; make; then install unix and netnix together"
