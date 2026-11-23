#!/bin/sh
# Teach mount(8), umount(8) and /etc/rc about NFS filesystems (bsd/nfs).
# Run as root on the PDP-11 from bsd/nfs/sbin.  Originals are kept as
# *.pre-nfs.
#
#	sh install.sh
#
SRC=/usr/src/sbin
here=`pwd`

test -f mount.diff -a -f umount.diff -a -f rc.diff || {
	echo "run from bsd/nfs/sbin"; exit 1; }

cd $SRC || exit 1
for f in mount/mount.c umount/umount.c; do
	test -f $f.pre-nfs || cp -p $f $f.pre-nfs || exit 1
done
if grep -s mount_nfs mount/mount.c >/dev/null; then
	echo "mount.c already patched"
else
	patch -p0 < $here/mount.diff || exit 1
	grep -s mount_nfs mount/mount.c >/dev/null || {
		echo "mount.diff did not apply"; exit 1; }
fi
if grep -s nfsdevice umount/umount.c >/dev/null; then
	echo "umount.c already patched"
else
	patch -p0 < $here/umount.diff || exit 1
	grep -s nfsdevice umount/umount.c >/dev/null || {
		echo "umount.diff did not apply"; exit 1; }
fi
for p in mount umount; do
	(cd $p && make) || exit 1
	test -f /sbin/$p.pre-nfs || cp -p /sbin/$p /sbin/$p.pre-nfs || exit 1
	install -c -s -m 755 $p/$p /sbin/$p || exit 1
done

cd / || exit 1
test -f etc/rc.pre-nfs || cp -p etc/rc etc/rc.pre-nfs || exit 1
if grep -s "mount -a -t nfs" etc/rc >/dev/null; then
	echo "/etc/rc already patched"
else
	patch -p0 < $here/rc.diff || exit 1
	grep -s "mount -a -t nfs" etc/rc >/dev/null || {
		echo "rc.diff did not apply"; exit 1; }
fi
echo "installed mount, umount and /etc/rc; install mount_nfs with make install"
