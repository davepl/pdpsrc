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

## Compact homepage follow-up (prepared September 20)

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

The physical PDP was unreachable while this follow-up was prepared. Live
deployment and HTTP byte-for-byte readback are pending its return.
