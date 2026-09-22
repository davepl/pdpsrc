# PDP-11 TMOG-11 website

This is the website for Dave's Mentec PDP-11/83 collection, served by 2.11BSD.
The original red-and-gold layout has been restored, with the amber TMOG-11 display
above the main content and **VISITORS: n** in the masthead. A responsive TMOG
banner below the virtual console links to `https://tmog.org/`. Its 1440×480 JPEG
is 167,696 bytes; the original PNG is preserved in `archive/`.
There is no public telnet/guest invitation. The HTML, CSS, inline panel artwork, and JavaScript are minified together. The restored
640×360 photograph is 101,511 bytes, below the requested 105,000-byte limit,
and loads lazily. Lossless JPEG optimization preserved its decoded pixels.
Both previous designs and the untouched original photograph are retained in
[`archive/`](archive/README.md). All source and assets needed for restoration
are here. The complete application is deployed at both `192.168.1.26` and
`192.168.1.29`. Both hosts use one persistent visitor total on the proxy at `.45`. Automatic
failover prefers `.29`, uses `.26` when needed, and returns to `.29` on recovery. The September 21
installation on `.29` is recorded in
[`deployments/2026-09-21-192.168.1.29.md`](deployments/2026-09-21-192.168.1.29.md).

The live process table fits within 118 columns, retaining 16-character command
names. CPU% shows recent measured CPU use; TTY identifies the controlling
terminal; M identifies core/RAM (C) or swapped (S) residency. Memory is in KiB
and TIME is cumulative CPU time. See [`README.webtop`](README.webtop) for
sampling details, abbreviations, native build instructions and field tests.
PPID, AGE, FD, block I/O rates, I/D separation and the current overlay add
process relationships, lifetime, resources and PDP-specific execution details.
After page or sampler deployments, `python3 tests/site-integrity.py ORIGIN...`
compares the homepage and public images byte-for-byte with this checkout.
For example, use `http://192.168.1.26/` and `https://pdp1173.com/` as origins.

## Virtual PDP-11/70 console

The decorative panel sits directly below TMOG-11 and shares its container width.
It adapts Paul Nankervis' panel layout, scoped CSS, SVG logo, and bit-mask lamp
updates from James Hagerman's mirror. Provenance and the unchanged upstream SVG
are in `archive/virtual-panel/`. The logo is inline because the native HTTP
server serves only HTML, JPEG and ICO with specific MIME types.

This is explicitly labeled simulated. It follows the 2.11BSD `rdisply=0377`
idle sequence (CLC/ROL/BPL/BIS), advancing at a nominal 30 steps/sec. Random
activity runs for 0.65–2.2 seconds, separated by 8–20 seconds of idle animation.
It does not sample registers, load an emulator or disk images, or issue network
requests. Switches and selectors are artwork, not machine controls.

The Pause animation button is independent of TOP's Pause updates. Hidden tabs
and offscreen panels stop animating. Reduced-motion preferences start the panel
paused; visitors can explicitly resume it. The entire 720×304 console scales
within the same-width responsive section. `npm test` checks the source and
minified-page animation, pause/resume, reduced motion and existing visitor logic.
The regular page uploader preserves the old homepage for rollback and includes
the panel's restore source. No PDP executable, installer change or new public
asset is needed for the virtual panel itself.

## Shared-link previews

The static HTML head declares Open Graph metadata for Apple Messages and other
link-preview clients, plus a Twitter large-image card. The preferred image is
`https://pdp1173.com/pdp1173-tmog-preview-v1.jpg`, a 2108×1610 JPEG of Dave's
supplied TMOG-11 screenshot. Its original PNG is retained at
`archive/link-preview/tmog.original.png`. The image is a fixed screenshot;
the page's TOP display continues to update normally.

JPEG preserves compatibility with the native HTTP server's supported image
types. The page uploader and full restore installer publish this image before
the HTML. Keep preview tags in the HTML source because Apple's preview fetcher
does not run JavaScript. Use a new image filename for future replacements and
refresh the proxy's homepage cache after updating both PDPs. Messaging apps
control their final presentation and may retain previews of previously shared
links.

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
both HTML files and preserve the images in `site/`. The native installer and uploader use the
prebuilt `site/index.html`; no Node.js or minifier runs on the PDP.

For a homepage/image update on an already installed system, run
`python3 deploy-page.py 192.168.1.26`. It prompts for the existing FTP
password, saves the previous page (and any replaced images) outside the document
root, and updates the on-machine source and design archives. It publishes the
images before atomically publishing the prepared page, all with mode 644.
It checks the page and images over FTP and HTTP. It does not rebuild programs or
touch the visitor total, sampler, or HTTP server. `--password-stdin` is
available for a credential supplied securely by an existing automation.

## Restore from this repository

From a modern Mac/Linux machine with Python 3, in this directory:

```
python3 deploy.py 192.168.1.26
```

The uploader prompts for the existing root FTP password; it is never stored
in the repository or command line. FTP is the machine's existing LAN access.
It uploads one uncompressed source archive and prints the following commands
to run at the PDP's root console (or an authenticated LAN telnet session):

```
test -d /usr/src/local/webtop || mkdir -p /usr/src/local/webtop
cd /usr/src/local/webtop
tar xf /usr/src/local/webtop-source.tar
make clean && make && sh install.sh
rm /usr/src/local/webtop-source.tar
```

Compile on the PDP with its native headers and `/unix` symbols. `webtop`
requires `cc -O -i` for separate instruction/data space. The programs have
no curses or floating-point dependency. Do not substitute a Mac executable.
Start the shared counter on caddy first (see `config/README.varnish.md`).
`sh install.sh` first makes a dated runtime backup, installs the three CGI
programs and the privileged sampler, warms the cache, then atomically
publishes the homepage. It preserves any existing visitor total. The scripts
use the native shell and utilities: no `set -e`, `set --`, formatted `date`,
or assumption that `mkdir -p` succeeds for an existing directory.
Source archives normalize ownership to root:wheel and readable file modes,
instead of importing the development Mac's numeric user and group IDs.

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
| Homepage, photograph, TMOG banner, previous page | `/home/www` | `site/` |
| Privileged sampler, root mode 4711 | `/usr/local/libexec/webtop` | Rebuild `webtop.c` |
| Ordinary CGI executables, root mode 755 | `/home/www/cgi-bin/webtop`, `visit`, `visit-total` | Rebuild native helper sources |
| **Persistent visitor total (caddy)** | `/var/lib/pdp-visitors/visitors.sqlite` | Online SQLite backup, not Git |
| Historical local total (PDP) | `/home/www-visits/total` | Preserved for rollback |
| Health probe target (PDP) | `/home/www/health.txt` | `site/health.txt` |
| Regenerable TMOG-11 cache and lock | `/tmp/webtop.*` | No backup needed |
| Original HTTP server | `/usr/libexec/httpd` | Exact deployed source in `server/` |
| HTTP service wiring | `/etc/inetd.conf`, `/etc/services` | Relevant entries in `config/` |
| inetd startup rate | `/etc/rc` | Merge `config/rc.inetd`; see `config/README.inetd.md` |

The `server/` version is the original server from this physical PDP, including
its `/home/www/` root and unusual CGI behavior. The sibling `../httpd` is a
different version with a different root. Do not accidentally replace the
running server with that version. If `/usr/libexec/httpd` itself is lost,
build the preserved `server/` version on the PDP and follow its Makefile's
install target as root. Preserve the existing `www` account and writable
`/usr/adm/httpd.log`; merge the two HTTP configuration entries if needed.
Reload inetd only if its configuration was changed.

The `.26` host also requires the explicit inetd startup rate preserved in
[`config/README.inetd.md`](config/README.inetd.md). Its implicit default was
disabling HTTP under ordinary TMOG-11 polling. The website installer does not
change `/etc/rc`; merge that documented startup command when restoring.

### First installation on a different 2.11BSD host

The September 20 deployment to `192.168.1.26` needed these additional steps:
that patch-481 host had no `www` account, and its existing server used
`/var/www` with inetd running it as `nobody`. Back up the existing runtime
with `sh backup.sh` first, and separately archive `/var/www` if present;
the regular backup script covers `/home/www`, not another server's root.

Check that the `www` name and uid 80 are unused, group `staff` has gid 10,
and `/bin/false` exists before creating the locked service account. On the
tested native system the account tool is `/bin/chpass`:

```
/bin/chpass -a 'www:*:80:10::0:0:Web server:/home/www:/bin/false'
id www
```

Wait for the password database rebuild to finish if `id` does not yet find
the account. Preserve an existing `www` account instead of recreating it.
Build and install the website normally, then build `server/`. Publish its
binary by installing to `/usr/libexec/httpd.webtop` with `install -s -m 755`
and renaming it to `/usr/libexec/httpd`; this avoids overwriting an executable
that an existing request is still using. Ensure `/usr/adm/httpd.log` exists,
is owned by `www:staff`, and has mode 644, as in the server Makefile.

Merge `config/inetd.http` into the existing HTTP entry, preserving other
services, and confirm `http 80/tcp` exists in `/etc/services`. Reload the
actual running inetd with SIGHUP. Verify the homepage, both CGI programs,
shared counter reads, and browser reload behavior before taking and copying
off a new runtime backup. No kernel or operating-system upgrade is required
for this installation.

## Preserve runtime state off the PDP

```
cd /usr/src/local/webtop
make backup
```

This prints a private dated directory under `backups/`, containing
`runtime.tar` and the kernel identification. Copy that directory to another
machine before replacing a disk image. The archive includes the website,
HTTP/helper binaries, inetd/services files, and the historical local visitor count. Back up the live SQLite count on caddy separately.
Backup paths are relative to `/`, so inspect the archive before restoring
selected files as root. Restore the total with owner `www`, mode 600, in a
`www`-owned mode-700 `/home/www-visits` directory, before running the installer.
The installer never initializes or changes the shared count. It requires the
central read route to work before publishing native forwarding helpers.

Git preserves source and assets, not future counter updates or the complete
2.11BSD operating system/kernel/disk image. Keep normal system/disk backups
as well. Never commit passwords, private runtime archives, or a mutable total.

## Check after restoration

On the PDP, `/usr/local/libexec/webtop` should print one TMOG-11 frame. On the LAN,
open `http://192.168.1.26/`; verify the amber frame and top-right visitor count,
then refresh and confirm the count does not increment. `/cgi-bin/webtop`
returns plain text and an `X-Snapshot-Age` header. Requests inside five seconds
share one frame. Hidden tabs stop polling; the page has a Pause button.

The old network stack has previously lost connectivity under public load.
The sampler cache reduces sampling work but does not remove inetd's process
per request or fix the network driver. Use a few serial checks, not a load
test against the physical machine. The historical concurrency test is
recorded in `VERIFICATION.md`.

The public site now uses Varnish on `caddy` (`192.168.1.45`). Its preserved
configuration and cache-preserving deployment instructions are in
[`config/README.varnish.md`](config/README.varnish.md). Homepage tracking
queries such as Facebook's `fbclid` share the ordinary homepage cache entry.
This proxy configuration is separate from the native PDP installation.

For the browser's refresh/session-count logic, run on a modern host:

```
node tests/visitors.js
```

To disable sampling, `chmod 700 /usr/local/libexec/webtop`. To restore the
preinstallation homepage, copy `/home/www/index.html.bak` over `index.html`
and make it mode 644. The dated runtime backup preserves later versions too.
