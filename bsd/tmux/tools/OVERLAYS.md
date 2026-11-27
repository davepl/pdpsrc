# Planning the native overlay link

A full-profile native executable has linked, but its runtime behavior has not
been verified. The selected milestone manifest contains 91 application objects
and the full manifest 149. The milestone executable planned this way passed its
native session tests on November 26, 2026. This planning procedure is not
itself an interactive acceptance result. See the [port README](../README.md) for the
26-command profile and its color/status/history limits.

Collect sizes only after every application object has compiled with the native
PDP-11 tools. From `tmux` on Minerva:

    make sizes > native-tmp/object-sizes.txt

Retrieve that text file to the modern host. `tools/pack_overlays.py` reads it;
it does not run a compiler/linker, contact Minerva, or modify the link scripts.
Its default input object list is `native/objects.list`, generated together with
the child makefiles by `tools/generate_native_makefiles.py`. It is independent
of the current link script and contains the selected profile's application
objects. Per-chunk size commands include at most eight input objects. Use a separate
plain object list with `--objects-from` only when planning a different set of
application inputs; saved full-profile measurements use
`--objects-from native/objects-full.list`.
Missing or conflicting size records stop planning, rather than omitting code.

Start with 24 KiB application banks, leaving one of the 15 overlay slots free
for a possible library bank. From the project root on the modern host:

    python3 tools/pack_overlays.py /path/to/object-sizes.txt \
        --bank-size 24576 --reserve-banks 1 \
        --plan /path/to/overlay-plan.json \
        --script /path/to/overlay-candidate.sh

This candidate keeps `tmux.o`, any additional `--resident` objects, and zero-text
data owners resident. The remaining whole objects are assigned with deterministic
best-fit decreasing packing. No function or data definitions are discarded.
The default library tail is `-Y -ltermlib -lc`, so library code remains resident.
The planner supports 16, 24, and 32 KiB application banks. Try 16 KiB when
resident code needs more space and the resulting bank count fits; try 32 KiB
when a unit or bank count requires it. Do not split objects or change kernel
limits blindly.

Each overlay and resident text region rounds up to an 8 KiB hardware segment.
Their combined mapped text must fit 64 KiB. A 16 KiB overlay window leaves at
most 48 KiB for resident text, a 24 KiB window leaves 40 KiB, and a 32 KiB
window leaves 32 KiB. Raw object
text does not include startup, eight-byte global function entry thunks, library
extraction, or alignment. The plan reports the remaining unmeasured resident
space, not a promise that the final executable fits. Libraries also add shared
data/BSS; overlays do not create a larger flat heap or stack.

Keep `tmux.o` resident initially. Its global `sighandler` only saves/restores
`errno` and sets `sig_atomic_t` flags; `siginit` and `client_suspend` register
that same entry. Moving it into an overlay would add an overlay switch during
asynchronous delivery. Ordinary global-function thunks do not establish that
every interruption point in overlay switching is reentrant. There is no reason
to take that risk for this milestone. No static callbacks escaping to another
object were found in the upstream C files: mode, command, prompt, job, poll and
search callbacks are global. The native `terminfo.c` static helper is used only
by direct calls. Re-audit any new static function pointers before repartitioning.

If resident libraries do not fit, first prepare the private libc archives using
`tools/Makefile`'s native `libc` target. See [README.md](README.md) for its manifest and
assembly-entry restrictions. A separate candidate can reserve the last overlay
for termlib and safe libc, with needed assembly entries selected after `-Y`:

    python3 tools/pack_overlays.py /path/to/object-sizes.txt \
        --bank-size 24576 \
        --overlay-library=-ltermlib \
        --overlay-library=../native-tmp/libc-safe.a \
        --resident-library=../native-tmp/libc-root.a \
        --library-window 32768 \
        --plan /path/to/overlay-libc-plan.json \
        --script /path/to/overlay-libc-candidate.sh

The library window is an allowance, because extracted archive members depend on
the final unresolved symbols. Do not put the full unaudited `/lib/libc.a` behind
an overlay thunk. Audit termlib's exported entries before placing it in that
bank too; its safety is not established by the libc manifest. Root assembly may
refer back to safe C functions; retain only required dependencies with explicit
`--link-arg=-u --link-arg=SYMBOL` before the archive, following the native linker
diagnostics. Do not force-load every root member. The private `ld211` must be
used so the known BSS-thunk emission bug is corrected.

Generated scripts are candidates to review and copy separately, not adopted
build recipes. Run them from `tmux/upstream`. They retain debug symbols and print
the link status. Use `--link-arg=-x` only if stripping local symbols is intended.
After a successful native link, inspect `size tmux`, `checkobj tmux` if present,
and its a.out header/overlay table. Confirm the rounded base plus maximum bank
fits, data/BSS leave measured heap/stack space, and all expected objects are
linked exactly once. Then test startup and an isolated detached session with
two windows, window switching, attach/detach, resize and signals. A successful
link alone does not establish that session/runtime behavior is correct.
