#!/bin/sh
# Run as root on the PDP. Keep a dated, private, portable runtime backup.
# Avoid set -e: this shell also exits on expected false if-tests.
umask 077 || exit 1
id | grep 'uid=0(' >/dev/null || exit 1
base=/usr/src/local/webtop/backups
if test ! -d "$base"; then
    mkdir -p "$base" || exit 1
fi
stamp=`date | tr ' :' '__'`
dest="$base/$stamp-$$"
mkdir "$dest" || exit 1
paths=""
for path in home/www home/www-visits usr/local/libexec/webtop usr/libexec/httpd etc/inetd.conf etc/services etc/rc
do
    if test -e "/$path"; then
        paths="$paths $path"
    fi
done
# The words come only from the fixed paths above. This Bourne shell has no
# 'set --', and this date implementation has no +format option.
(cd / && tar cf "$dest/runtime.tar" $paths) || exit 1
uname -a > "$dest/system.txt" || exit 1
echo "Backup: $dest"
echo 'Copy this directory off the PDP. The visitor total is runtime state, not Git source.'
