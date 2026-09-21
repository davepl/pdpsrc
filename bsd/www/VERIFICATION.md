# Recovery and verification

## Provenance

The three C programs and `site/index.html` are exact copies of the final
saved September 19, 2026 artifacts, including the visitor counter change.
The original photograph and previous homepage were also recovered. On
September 20, the restored PDP's homepage, image, and HTTP server source
were retrieved before any installation. The homepage exactly matched the
saved original. `server/` contains that deployed server's source, Makefile,
and MIT license; no HTTP server change is part of this restoration.

## Earlier native verification (September 19)

The sampler compiled with native `cc -O -i` and produced a frame containing
uptime, load, process states, interval CPU, RAM, swap, and process names.
The original concurrency check returned complete old/new cached frames to
ten simultaneous requests, with no partial output or accelerated sampling.
That is historical evidence, not a load test repeated during this restore.
The physical machine has subsequently lost networking under public load.

The visitor CGI compiled natively and returned one increment while repeated
static reads left the total unchanged. The final HTML's earlier publication
could not be read back because networking failed. This restoration uses
that saved final HTML and verifies publication again.

## September 20 restoration

- All three programs built with the native compiler. Text/data/bss sizes:
  `webtop` 21824/2520/2730; launcher 3104/508/40; counter 4238/714/44 bytes.
- Two complete `make install` runs succeeded. The second retained the
  nonzero visitor total; existing directories, homepage backups, and the
  stable counter lock were preserved. Installer/backup scripts were adapted
  to the native Bourne shell, `mkdir`, and `date`, rather than assuming
  modern POSIX utility behavior.
- Helper ownership/mode: root/4711. Both CGI executables: root/755. Counter
  directory: www/700; total and lock: www/600; the static symlink is correct.
- HTTP homepage readback exactly matched `site/index.html` (15300 bytes,
  SHA-256 `4308d36fae55844ca7d1b8c54a5fc17c17e1b9bdac2fbba74d39ca693e57182c`).
- HTTP snapshot responses returned 200 with matching Content-Length,
  `Cache-Control: no-store`, `X-Snapshot-Age`, and real kernel/process data.
  The restored kernel reports 3072 KiB of physical RAM.
- Two immediate native helper calls produced byte-identical cached frames
  (`cmp` status 0). HTTP round trips sometimes exceeded the five-second
  window, so identical output was not required across those slower requests.
- One HTTP counter call changed 0 to 1; static reads preserved 1. The
  browser subsequently displayed 2, and reloading retained **VISITORS: 2**.
  The live amber frame rendered successfully and Pause/Resume worked.
- `node tests/visitors.js` passed for new sessions, reloads, same-session
  tabs, blocked cookies/storage, count formatting, and interrupted responses.
- The runtime backup was copied off the PDP and inspected: it includes the
  installed site and binaries, configuration, and a nonzero visitor total.
  Runtime backup names use the PDP's restored clock; this record uses the
  actual restoration date.
- No kernel, network, httpd binary, inetd configuration, or account change.
  No public load/concurrency test was repeated. Existing unrelated local
  repository edits were excluded from the source-control change.

## Compact homepage follow-up (deployed September 20)

The photograph and unused image styles have been removed from the main page.
The history section now places its heading and prose in the two columns.
The image is retained only for the archived previous homepage.

`site/index.source.html` is the editable source; `npm run build` generates
the committed `site/index.html` using the pinned minifier and lockfile.
HTML, CSS, and JavaScript are minified on the development machine, with no
extra work on the PDP. CSS level 1 preserves the responsive `clamp()` font;
the more aggressive level 2 was found to discard that declaration.

The final HTML is 13003 bytes, versus 15300 bytes before this change (15.0%
smaller). Removing the 82029-byte photograph reduces combined HTML/image
transfer from 97329 to 13003 bytes (86.6%), excluding TOP/counter responses.
The main page contains no image element or reference to the photograph.

The reproducible build check and visitor-counter tests passed for both
readable and minified pages. A local browser preview with recorded TOP data
confirmed the counter, terminal rendering, restored heading typography, and
image-free layout. The full restore bundle contains the build source and
the minified output; `deploy-page.py` provides a static-page-only update.

The PDP returned during preparation and the static-page-only publisher
completed successfully. FTP and HTTP readback both exactly matched the
13003-byte page, SHA-256
`639e1350394a105f3d3c87dd60256481a7b91b068b2646e5d8a85fd0efd45eed`.
The previous page was saved outside the document root, and the on-machine
source/build files were updated. The existing FTP server supports SITE
CHMOD and atomic replacement, so no native rebuild or shell change was
needed. TOP returned HTTP 200 with a complete 1846-byte frame; the static
visitor count remained 8 before and after deployment. Verified at
2026-09-20 17:54 UTC. The temporary deployment-retry heartbeat was disabled
after success.

## Complete deployment to 192.168.1.26 (September 20)

- Target identifies as `simh`, 2.11BSD patch 481, kernel `MINERVA #15`,
  with 4088 KiB physical RAM. This is a different target from the earlier
  physical machine at `.29`; the saved website content was deployed intact.
- Uploaded the complete source bundle to `/usr/src/local/webtop` and built
  all three programs plus the preserved HTTP server with the native compiler.
- Backed up the original server/configuration and `/var/www` before replacing
  the server. Created locked `www` uid 80, gid 10 after checking for conflicts;
  changed only the HTTP inetd entry from `nobody` to `www` and reloaded inetd.
  The preserved server uses `/home/www`. No OS or kernel update was made.
- Installation and cache warm-up succeeded. Verified helper root/4711,
  CGI executables root/755, counter directory www/700, and counter files
  www/600. No previous visitor directory existed on this target.
- Homepage HTTP 200 readback exactly matched the saved 13003-byte minified
  page, SHA-256
  `639e1350394a105f3d3c87dd60256481a7b91b068b2646e5d8a85fd0efd45eed`.
- TOP returned HTTP 200, a complete 1841-byte live frame, `Cache-Control:
  no-store`, and `X-Snapshot-Age`. Two immediate native helper calls returned
  identical frames (`cmp` exit 0).
- Static counter read initially returned zero. A real browser displayed
  **VISITORS: 1** and live TOP data; refreshing retained **VISITORS: 1**.
  Pause stopped the updates. Source and minified visitor tests passed.
- Predeployment backup directory:
  `/usr/src/local/webtop/backups/Sun_Sep_20_15_00_06_PDT_2026-201`.
  Installed runtime backup directory:
  `/usr/src/local/webtop/backups/Sun_Sep_20_15_03_18_PDT_2026-399`.
  The predeployment backup also contains `previous-webroot.tar` for `/var/www`.

## Original layout and photograph restored (September 20, Pacific time)

- Recovered the original red-and-gold HTML and photograph byte-for-byte from
  `previous-webroot.tar`. Preserved them in `archive/pre-webtop-192.168.1.26/`
  and the previous amber webtop design in `archive/amber-webtop/`.
- Restored the original layout with TOP as the first main-content section,
  the existing site-wide session counter in the masthead, and M11 hardware
  references. The old per-refresh browser-only counter and public telnet
  invitation were not restored.
- Losslessly optimized the original 106,717-byte JPEG to 101,511 bytes,
  retaining 640×360 pixels. Decoded PPM files compared identically. The
  published name is `pdp1183-web.jpg` to avoid the larger photograph already
  cached by Cloudflare. The image is lazy-loaded and reserves its aspect ratio.
- Minified HTML is 14,886 bytes (readable source 24,688 bytes). Reproducible
  build and counter tests passed for both versions, including new sessions,
  reloads, same-session tabs, blocked storage, and interrupted responses.
- Browser previews at desktop and 390×844 mobile sizes confirmed the restored
  layout, loaded photograph, visitor count, and Pause/Resume controls. The
  document did not overflow horizontally; wide TOP content scrolls inside
  its own panel on narrow screens.
- Published the static page and photo to `.26`, keeping timestamped previous
  pages outside the document root. Updated on-machine source and both design
  archives. No native programs were rebuilt and no visitor state was reset;
  the total was 32 before publication and later 36 with real visitors.
- At 2026-09-21 01:10 UTC, direct origin HTTP and public HTTPS returned 200
  with exact local-file matches for the page and optimized photo. SHA-256:
  - HTML: `503c052f0dcb80949762988ce21fa686d5b42196937d00e47df3c483b039627c`
  - JPEG: `8c932f29f61823b8e029bebd2c602319587f3da10aef8bcbb2a27999adc6cc02`
- Refreshed only relevant Varnish objects. A public `/?fbclid=...` request
  returned the same page with `X-Cache: HIT`. Public TOP returned a complete
  live frame; a browser showed TOP, the photo, and **VISITORS: 36**.
- The pre-existing HTTP availability issue recurred during this deployment:
  inetd logged `http/tcp server failing (looping), service terminated`, while
  FTP and the console remained available. Restarting inetd restored HTTP for
  verification. This static layout change does not resolve that issue; no
  rate-limit, kernel, HTTP-server, or Varnish configuration change was made.

## HTTP shutdown and TOP cache follow-up (September 20, Pacific time)

- Confirmed port 80 was refusing connections while telnet still worked.
  Logs showed inetd disabling HTTP with its service invocation-rate guard.
  The archived 2.11BSD implementation defaults to 40 starts per minute,
  although its manual documents 1,000. The installed executable supports
  `-R rate`, and the original startup command specified no explicit rate.
- Started inetd with `-R 1000` and preserved the same option in `/etc/rc`.
  Verified the running command and the one-line startup diff. Original
  startup file: `/etc/rc.before-webtop-rate-20260921`, root mode 644.
  No kernel or executable replacement. No new inetd shutdown message appeared
  during subsequent checks through 01:20 UTC.
- Deployed Varnish `pdp_top_20260921` without restarting the proxy or evicting
  the homepage. Public TOP now has a five-second shared cache and 15-second
  grace, retaining no-store downstream and adding cache age to the displayed
  snapshot age. Visitor increments, authenticated requests, and CGI query
  strings retain their bypass behavior.
- Both fake-origin Varnish tests passed: homepage tracking/cookie reuse,
  shared TOP across cookies and browser no-cache requests, accurate age,
  five-second expiry, retention after failed background refresh, repeated
  uncached counter increments, authentication, and host/method guards.
- Six serial public TOP requests from approximately 01:19:43–01:20:13 UTC
  all returned 200 with advancing live timestamps. Responses included HIT
  and short-lived STALE frames during background refresh; reported ages were
  2–8 seconds. The existing public browser's TOP display resumed updating.
- This does **not** establish that the wider network problem is resolved.
  During verification, all tested TCP services and ping became unreachable
  from the Mac and caddy, recovered without an uptime reset, then failed
  again. The operator confirmed the console remained responsive and could
  ping the router. `netstat -m` after recovery showed 517 cumulative denied
  allocations and 153/170 mbufs in use; qe0 reported zero input/output errors.
- Native socket inspection showed several direct Internet HTTP clients,
  including established connections with queued send data and closing
  connections. These bypassed caddy and its connection/cache limits. The
  operator subsequently changed the UDM Pro's public port-80 forwarding from
  `.26` to `.45`, placing new incoming HTTP connections behind the proxy.
- Added the legacy DynDNS hostname to caddy's PDP site and canonicalized
  accepted hostnames in Varnish. Both test scenarios passed again with alias
  coverage; `pdp_alias_20260921` was activated and caddy reloaded after
  validation. Public HTTPS `pdp1173.com` and HTTP `davepl.dyndns.org` both
  returned 200 and exactly matched the restored homepage at 01:24 UTC.
  Origin LAN reachability was still intermittent; successful cached homepage
  delivery must not be mistaken for successful live TOP sampling.
- The updated full source upload to the PDP timed out during the recurrence.
  The active proxy and `/etc/rc` changes were applied and verified earlier;
  the new documentation, tests, and backup-script update remain preserved
  off-machine in Git and the local source bundle until upload is possible.

### Recovery after forwarding correction and operator restart

The operator subsequently reported that the console had hung and restarted
the PDP. The forwarding correction therefore should not be described as
having recovered the old kernel instance by itself. After the restart:

- Direct HTTP returned the exact restored 14,886-byte homepage, and the
  persistent visitor total was 60. No counter reset or native rebuild.
- Eight public TOP checks, alternating `pdp1173.com` and `davepl.dyndns.org`
  over about 70 seconds, all returned 200 with cache HIT and advancing live
  timestamps. Snapshot ages were 0–4 seconds.
- Authenticated inspection confirmed both `/etc/rc` and running inetd use
  `-R 1000`; the setting survived the reboot. Observed HTTP peers were now
  only caddy and the administration Mac, with no direct Internet peers.
- Initial post-restart network statistics were 138/170 mbufs, no mapped
  pages in use, and one cumulative memory-allocation denial. This is an
  observation after restart, not proof the old kernel cannot fail again.
- The complete updated source bundle was successfully uploaded and extracted
  into `/usr/src/local/webtop`, including the proxy/startup restore guidance,
  tests, and updated backup script. Existing native binaries were retained.
- At 01:28:48 UTC the cumulative allocation-denial count remained at one,
  mbuf use had fallen to 130/170, and no mapped pages were in use. No new
  HTTP-disable event appeared in the daemon log during these checks.
- Created a fresh runtime backup including the restored website, binaries,
  visitor state, and `/etc/rc` at
  `/usr/src/local/webtop/backups/Sun_Sep_20_18_28_12_PDT_2026-253`.
