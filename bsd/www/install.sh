#!/bin/sh
# Run from this source directory as root, after make. Native 2.11BSD sh.
# Installs the recovered site; does not replace httpd or edit inetd/the kernel.
# This old shell exits on false if-tests under set -e. Check commands explicitly.
umask 022 || exit 1
id | grep 'uid=0(' >/dev/null || exit 1
for file in webtop webtop-cgi visit-proxy visit-total-proxy site/index.html site/pdp-ai.html site/glass-tty-vt220-v1.woff2 site/gary-green-v2.jpg site/pdp-gary-preview-v2.jpg site/pdp-gary-favicon-v3.png site/pdp-gary-apple-touch-icon-v3.png site/index.previous.html site/pdp11.jpg site/pdp1183-web.jpg site/tmog-banner-v2.jpg site/pdp1173-tmog-preview-v1.jpg site/health.txt
do
    test -s "$file" || exit 1
done
test -x /usr/libexec/httpd || exit 1
id www >/dev/null || exit 1

# Refuse to zero an existing, incomplete counter installation.
if test -d /home/www-visits; then
    if test ! -s /home/www-visits/total; then
        echo 'Missing visitor total: restore it from backup before installing.' >&2
        exit 1
    fi
fi
sh backup.sh || exit 1

for dir in /home/www /home/www/cgi-bin /usr/local/libexec
do
    if test ! -d "$dir"; then
        mkdir -p "$dir" || exit 1
    fi
done
chown root /home/www /home/www/cgi-bin /usr/local/libexec || exit 1
chmod 755 /home/www /home/www/cgi-bin /usr/local/libexec || exit 1
if test -f /home/www/index.html; then
    if test ! -f /home/www/index.html.bak; then
        cp -p /home/www/index.html /home/www/index.html.bak || exit 1
    fi
fi
if test -f /home/www/pdp11.jpg; then
    if test ! -f /home/www/pdp11.jpg.bak; then
        cp -p /home/www/pdp11.jpg /home/www/pdp11.jpg.bak || exit 1
    fi
fi

# The shared counter service must already be active on caddy. This read-only
# check prevents publishing a counter proxy before its route exists.
./visit-total-proxy > /dev/null || exit 1
# Any former local total and lock remain untouched for rollback.

# Set ownership/permissions before atomic publication of each executable.
cp webtop /usr/local/libexec/webtop.new || exit 1
chown root /usr/local/libexec/webtop.new || exit 1
chmod 4711 /usr/local/libexec/webtop.new || exit 1
mv -f /usr/local/libexec/webtop.new /usr/local/libexec/webtop || exit 1
cp webtop-cgi /home/www/cgi-bin/webtop.new || exit 1
chown root /home/www/cgi-bin/webtop.new || exit 1
chmod 755 /home/www/cgi-bin/webtop.new || exit 1
mv /home/www/cgi-bin/webtop.new /home/www/cgi-bin/webtop || exit 1
cp visit-proxy /home/www/cgi-bin/visit.new || exit 1
chown root /home/www/cgi-bin/visit.new || exit 1
chmod 755 /home/www/cgi-bin/visit.new || exit 1
mv /home/www/cgi-bin/visit.new /home/www/cgi-bin/visit || exit 1
cp visit-total-proxy /home/www/cgi-bin/visit-total.new || exit 1
chown root /home/www/cgi-bin/visit-total.new || exit 1
chmod 755 /home/www/cgi-bin/visit-total.new || exit 1
mv /home/www/cgi-bin/visit-total.new /home/www/cgi-bin/visit-total || exit 1

for file in pdp11.jpg pdp1183-web.jpg tmog-banner-v2.jpg pdp1173-tmog-preview-v1.jpg index.previous.html health.txt gary-green-v2.jpg pdp-gary-preview-v2.jpg pdp-gary-favicon-v3.png pdp-gary-apple-touch-icon-v3.png glass-tty-vt220-v1.woff2 pdp-ai.html
do
    cp "site/$file" "/home/www/$file.new" || exit 1
    chown root "/home/www/$file.new" || exit 1
    chmod 644 "/home/www/$file.new" || exit 1
    mv "/home/www/$file.new" "/home/www/$file" || exit 1
done
cp README.webtop /home/www/README.webtop || exit 1
cp README.visitors /home/www/README.visitors || exit 1
chmod 644 /home/www/README.webtop /home/www/README.visitors || exit 1

# Warm/check the sampler before publishing the page that calls it.
/usr/local/libexec/webtop > /dev/null || exit 1
cp site/index.html /home/www/index.html.new || exit 1
chown root /home/www/index.html.new || exit 1
chmod 644 /home/www/index.html.new || exit 1
mv /home/www/index.html.new /home/www/index.html || exit 1
sync || exit 1
echo 'Installed webtop and shared visitor counter. Historical local count preserved.'
