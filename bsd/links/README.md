# Links 1.00pre12 for PDP-11 / 2.11BSD

A native split-I/D port of the real Links text browser, verified on Minerva's
PDP-11/73 2.11BSD environment. Basic HTTP and local HTML rendering, VT100 link
navigation, tables, text-entry forms and GET submission work. This is a
small-page historical browser, not a modern web engine or an emulation wrapper.

## Current native verification

Verified on Minerva with the delivered executable: the real FrogFind homepage
loads interactively, a `PDP-11` search submits and renders, results scroll, Back
preserves the query, and resubmitting works. The native local/HTTP/redirect/
chunked/UTF-8 regression suite passes. A normal clean Makefile build succeeded;
see BUILD-RESULTS.txt for exact files, hashes and test details.

Known limit: forced reload after the homepage/search/Back/resubmit sequence can
still exhaust the 64 KiB data space. A fresh direct-results launch and reload
worked, but long sessions and arbitrary pages are not guaranteed. Restart Links
if it reports allocation failure. This is an explicit remaining limitation,
not a claim of general modern-web compatibility.

## Build and run on Minerva

From `/usr/dave/source/repos/pdpsrc/bsd/links`:

    make native
    TERM=vt100 upstream/links-pdp -no-connect http://frogfind.com/

For local pages or noninteractive output:

    upstream/links-pdp -no-connect -dump tests/page.html
    upstream/links-pdp -no-connect -source http://your-http-server/page.html > page.html

The included generated C/headers build with Minerva's ANSI-capable compiler,
make, ar, ranlib and libvmf. Python/autoconf/clang are NOT needed on the PDP.
`Makefile.211bsd` builds project-private compiler/linker helpers and uses 15
code overlays with split I/D. It leaves `/bin/ld`, `/lib/libc.a`, the kernel,
security level and system clock unchanged. The 300 KiB process limit is retained.
A normal build produces `upstream/links-pdp`. Optional `make install` places it
at `/usr/bin/links`; `BINDIR` and `DESTDIR` are configurable. No system install
has been performed as part of testing.

Host builds remain separate:

    make host
    make test
    python3 tests/reduced_host.py

The last command tests the native-equivalent feature profile on a modern host;
it is not a substitute for native ABI and memory testing.

## Using the VT100 interface

* Up/down select links and form fields; Right or Enter follows/submits.
* Left returns Back; history retains query/form values and may refetch the page.
  `g` opens a URL dialog.
* Type directly into a selected text field; Ctrl-U clears its contents.
* Escape (press once, then pause) opens the menu bar. `/` searches; `n` finds the next match.
* `q` opens the exit confirmation; `y` confirms.

Use an 80-column, 24-row terminal for the verified layout. Larger terminal grids
consume more of the small native heap.

## Native feature profile and limits

Retained: original HTML renderer, table layout, forms, navigation,
history, search, menus, cookies/authentication code, HTTP and local files.
The tested subset is listed in BUILD-RESULTS.txt; retaining code is not a claim
that every feature or site has been exhaustively tested.

Native-only omissions: graphical/JavaScript/TLS functionality, frame layouts, FTP/Finger/SMB,
mailto/telnet launching, bookmarks, integrated download manager, external
viewers, saving configuration/URL history to disk, and spawning additional terminal windows.
Existing configuration files are still read; runtime options and navigation history work.
Frames and iframes are offered as ordinary clickable links; noframes content is shown. Disabled shortcuts explain
that the feature is unavailable. Saving remains possible using shell redirection
with `-source` or `-dump`. The normal modern-host profile is not stripped this way.

Other space reductions include English-only UI, reduced Latin-1 entities and
common Unicode transliterations with allocation-free direct charset conversion,
one network connection, no retained formatted
page cache, no retained unreferenced raw-page cache, eight history entries, bitmap-based terminal
dirtiness, static default keymaps, small initial table capacity with normal
growth (four links/rows at a time), 16-byte string allocation batches, and compact diagnostics. Original diagnostic prose stays in the canonical
sources; native internal errors retain file/line information.

Wrapped rows release their old pre-wrap storage immediately, keeping only the
aligned capacity needed for the shortened row. Terminal redraw streams ANSI output in bounded pieces instead of building one
large whole-screen output string. These changes reduce peak memory without
dropping page text. This is terminal-output streaming, not incremental network
rendering: the full response is still received before formatting. With unreferenced raw-page and
formatted-page caching disabled, revisiting a history entry may fetch it again.

URL input is limited to 512 bytes; configuration/general-string buffers to 256.
Native link/search coordinates use two bytes: each coordinate must be 0–255.
Pages exceeding that limit stop with an explicit error rather than corrupting
link positions. The bound also applies to temporary positions before line wrapping;
very long unbroken link labels can therefore exceed it on an 80-column display.
Cache settings are targets, not hard memory guarantees. Large or complex pages
can exhaust the 64 KiB data space or 300 KiB process cap. Code overlays do not create
a flat large heap. This port is intended for small basic HTTP pages.

## Native English UI data

The native executable requires `language.dat` beside the invoked executable.
`make install` copies both files into BINDIR. For a separate data directory,
set `LINKS_DATA_DIR` to that directory. Launches through PATH resolve the
sidecar beside the matching executable; symlink launches use the symlink's
location. Missing or invalid data produces an explicit startup error.
English labels are loaded once on demand and freed at shutdown, instead of
keeping every label in the PDP-11's 64 KiB data segment. Regenerate the binary
sidecar on the modern host with `python3 tools/generate_language_data.py`.
Normal host builds retain compiled-in English and do not need this file.

## Safety

This is historical GPL browser code, not a maintained secure browser. It has
not had a complete hostile-input audit. Use trusted small LAN HTTP pages; do
not enter passwords, payment details or other sensitive information. HTTPS is
not supported. No HTTPS-bypass proxy or ongoing service is installed.
Native pages are formatted after transfer completes rather than incrementally,
reducing peak memory; the transfer status remains visible.
`-no-connect` avoids joining another local Links instance.

## Build internals

The project-private linker fixes a verified 2.11BSD overlay bug that emitted
uncounted thunks for BSS symbols. Audited libc assembly entries remain in base
text because they cannot use normal C overlay entry stubs; private archive
copies hold the remaining C routines. `tests/check_aout.py` rejects malformed
native executables, including the original 64-byte displacement failure.

Compiler intermediates stay in `native-tmp/`, avoiding Minerva's tiny root
`/tmp`. Source archives may carry dates newer than Minerva's clock: normalize
staged source mtimes before an initial build, without changing the system clock.
The optional `tools/copy_mtime.c` supports reference-time copying where historic
`touch` lacks `-r`; it does not change file contents.

See tools/README.md for source generation and private tool details,
BUILD-RESULTS.txt for verification, and UPSTREAM.md/upstream/COPYING for provenance
and licensing. No credentials are stored in this directory.

Reproducible native CLI checks:

    make native-test

With `python3 tests/serve.py --bind HOST_LAN_IP` running temporarily on a modern
host, add `TEST_HTTP_BASE=http://HOST_LAN_IP:8765` to exercise HTTP, redirects,
chunked responses and UTF-8. The test script uses its own HOME below native-tmp.
Interactive navigation/form verification is recorded separately in BUILD-RESULTS.
