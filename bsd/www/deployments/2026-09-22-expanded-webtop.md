# Expanded process table and homepage integrity — 2026-09-22

The native process table adds PPID, AGE, FD, R/s, W/s, I/D and OVL. Rows use
at most 118 columns, retaining all 16 command-name characters. Rates use
the existing per-process sampling window; no extra kernel reads or sampling
passes are added. Start timestamps inconsistent with the reported boot time
or current time display `?` instead of an impossible process age.

Both PDPs built the sampler with their own compiler and headers. Host and
native field tests pass, including 32-bit limits, fractional I/O rates,
counter/clock errors, long ages, compact rates, and full-width rows. Fresh
uncached frames passed column, sorting and width checks. Each installed
sampler is 41,631 bytes, root:wheel, mode 4711. Previous binaries and source
are in `/usr/src/local/webtop-expanded-20260922/` on each PDP. Installation
changed the sampler and source files under `/usr`; it did not write `/home`.

## Layout incident

No HTML, CSS, JavaScript or image files were changed for this update. The
reported broken layout came from corrupted HTML on `.29`: its 42,106-byte
homepage contained README text starting at offset 14,336, inside the inline
console SVG. FTP and HTTP returned the same corrupted bytes. `.26` matched
the committed homepage exactly.

A read-only native filesystem check of `/dev/rra0h` (`/home`) found:

- Eight cross-linked blocks shared by inode 465 (`/www/index.html`) and
  inode 883 (`/www/README.webtop`).
- Twenty duplicate blocks in the free list and a bad free list.

The underlying cause of that filesystem inconsistency is not established.
The check made no filesystem repairs. Its log is saved in the private staging
directory as `home-fsck-before.txt`. A full pre-repair `/home` tar archive is
saved there as `home-before.tar` (17,355,776 bytes).
The archive was copied off-host and its SHA256 is
`e12fc0c0615e39f4bef2a8f496a09d47dca663047f7ddddf918068a4977dbd39`.

The public route was forced to `.26`, then only the homepage's Varnish cache
entry was invalidated. Public HTML again matches the committed original:
`ec252e194d7d147ff9c13e22809c633ff1ad1382dff808b6e8aa831b113f6651`.
The browser shows the original article layout with no console SVG inside it.
Desktop table overflow and narrow-screen page overflow were checked; the
wide table scrolls within its own container on narrow screens.

Offline repair of `.29`'s `/home` requires the user's pending approval.
Keep the public route forced to `.26` until repair and static integrity
verification complete; the HTTP health probe alone does not detect damaged
page content. The new sampler is deployed on both machines.

Both direct snapshot endpoints and public HTTPS passed the expanded column,
value, sort and width checks after installation. The static integrity test
passed for `.26` and the public site, including all four checked images.

`tests/site-integrity.py` compares the complete homepage and public images
against the checkout after deployments, including sampler-only changes.
It uses explicit origins and sequential requests, without disabling TLS checks.
