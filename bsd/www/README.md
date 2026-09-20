# PDP-11 cached webtop website

This is the website for the physical Mentec PDP-11/83 running 2.11BSD,
recovered from the September 19, 2026 deployment. The page includes the amber
TOP display and **VISITORS: n** masthead; there is no public telnet/guest
invitation. The main page omits the photograph and is minified to reduce
transfer size. The original photograph remains available to the archived
previous homepage. All source and assets needed for restoration are here.

## Edit and minify the homepage

Edit `site/index.source.html` on a modern Mac/Linux development machine,
then build the committed `site/index.html`:

```
npm ci --ignore-scripts
npm run build
npm test
```

The pinned minifier compresses HTML, inline CSS, and inline JavaScript;
the counter checks run against both source and generated output. Commit
both HTML files. The existing native installer and uploader use the
prebuilt `site/index.html`; no Node.js or minifier runs on the PDP.

For a homepage-only update on an already installed system, run
`python3 deploy-page.py 192.168.1.29`. It prompts for the existing FTP
password, saves the previous page outside the document root, updates the
on-machine source, and atomically publishes the prepared page with mode 644.
It checks the result over FTP and HTTP. It does not rebuild programs or
touch the visitor total, sampler, or HTTP server. `--password-stdin` is
available for a credential supplied securely by an existing automation.

## Restore from this repository

From a modern Mac/Linux machine with Python 3, in this directory:

```
python3 deploy.py 192.168.1.29
```

The uploader prompts for the existing root FTP password; it is never stored
in the repository or command line. FTP is the machine's existing LAN access.
It uploads one uncompressed source archive and prints the following commands
to run at the PDP's root console (or an authenticated LAN telnet session):

```
test -d /usr/src/local/webtop || mkdir -p /usr/src/local/webtop
cd /usr/src/local/webtop
tar xf /tmp/webtop-source.tar
make clean && make && make install
rm /tmp/webtop-source.tar
```

Compile on the PDP with its native headers and `/unix` symbols. `webtop`
requires `cc -O -i` for separate instruction/data space. The programs have
no curses or floating-point dependency. Do not substitute a Mac executable.
`make install` first makes a dated runtime backup, installs the two CGI
programs and the privileged sampler, warms the cache, then atomically
publishes the homepage. It preserves any existing visitor total. The scripts
use the native shell and utilities: no `set -e`, `set --`, formatted `date`,
or assumption that `mkdir -p` succeeds for an existing directory.

The restore assumes the original `/usr/libexec/httpd` is working with
`/home/www` as document root and inetd running HTTP as `www` (uid 80).
It does not change the kernel, networking, accounts, httpd, or inetd.
The sampler requires the existing root-owned sticky `/tmp` (mode 1777).
See `README.webtop` for implementation details and `README.visitors` for
the counter's approximate session semantics and limitations.

## Files that must survive a restore

| Purpose | Location on the PDP | Preserved here |
| --- | --- | --- |
| Sources and build/install steps | `/usr/src/local/webtop` | This directory |
| Homepage, photograph, previous page | `/home/www` | `site/` |
| Privileged sampler, root mode 4711 | `/usr/local/libexec/webtop` | Rebuild `webtop.c` |
| Ordinary CGI executables, root mode 755 | `/home/www/cgi-bin/webtop`, `/home/www/cgi-bin/visit` | Rebuild the other two C files |
| **Persistent visitor total** | `/home/www-visits/total` | Runtime backup, not Git |
| Visitor lock, www mode 600 | `/home/www-visits/lock` | Created once by installer |
| Static count URL | `/home/www/visits.txt` | Installer creates symlink to total |
| Regenerable TOP cache and lock | `/tmp/webtop.*` | No backup needed |
| Original HTTP server | `/usr/libexec/httpd` | Exact deployed source in `server/` |
| HTTP service wiring | `/etc/inetd.conf`, `/etc/services` | Relevant entries in `config/` |

The `server/` version is the original server from this physical PDP, including
its `/home/www/` root and unusual CGI behavior. The sibling `../httpd` is a
different version with a different root. Do not accidentally replace the
running server with that version. If `/usr/libexec/httpd` itself is lost,
build the preserved `server/` version on the PDP and follow its Makefile's
install target as root. Preserve the existing `www` account and writable
`/usr/adm/httpd.log`; merge the two HTTP configuration entries if needed.
Reload inetd only if its configuration was changed.

## Preserve runtime state off the PDP

```
cd /usr/src/local/webtop
make backup
```

This prints a private dated directory under `backups/`, containing
`runtime.tar` and the kernel identification. Copy that directory to another
machine before replacing a disk image. The archive includes the website,
HTTP/helper binaries, inetd/services files, and the private visitor count.
Backup paths are relative to `/`, so inspect the archive before restoring
selected files as root. Restore the total with owner `www`, mode 600, in a
`www`-owned mode-700 `/home/www-visits` directory, before running the installer.
The installer starts at zero only when that directory is entirely absent;
it refuses to silently recreate a missing total in an existing directory.

Git preserves source and assets, not future counter updates or the complete
2.11BSD operating system/kernel/disk image. Keep normal system/disk backups
as well. Never commit passwords, private runtime archives, or a mutable total.

## Check after restoration

On the PDP, `/usr/local/libexec/webtop` should print one TOP frame. On the LAN,
open `http://192.168.1.29/`; verify the amber frame and top-right visitor count,
then refresh and confirm the count does not increment. `/cgi-bin/webtop`
returns plain text and an `X-Snapshot-Age` header. Requests inside five seconds
share one frame. Hidden tabs stop polling; the page has a Pause button.

The old network stack has previously lost connectivity under public load.
The sampler cache reduces sampling work but does not remove inetd's process
per request or fix the network driver. Use a few serial checks, not a load
test against the physical machine. The historical concurrency test is
recorded in `VERIFICATION.md`.

For the browser's refresh/session-count logic, run on a modern host:

```
node tests/visitors.js
```

To disable sampling, `chmod 700 /usr/local/libexec/webtop`. To restore the
preinstallation homepage, copy `/home/www/index.html.bak` over `index.html`
and make it mode 644. The dated runtime backup preserves later versions too.
