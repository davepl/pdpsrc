# Private native build tools

These sources are copied from `bsd/links/tools`, with their original notices
retained. They are private native build helpers for Minerva's 2.11BSD system.
Nothing installs or replaces the system compiler, linker or libc.

From this directory on Minerva, `make` builds `cc211` and `ld211` with the native
compiler, assembler and linker. The default compiler-pass wrappers are the
project's existing `native/compiler` scripts; `CC`, `CFLAGS` and `PASSFLAGS` can
be overridden. `ld211` also requires the installed `libvmf`.

`cc211.c` preserves the installed compiler driver except for its temporary-file
prefix. Both tools use `../native-tmp`, so invoke them from `tmux/upstream` or
`tmux/tools`. `ld211.c` retains the Links port's narrow overlay correction:
`finishout()` must check equality with `N_EXT+N_TEXT`, rather than a bitwise
overlap that can emit uncounted thunks for BSS symbols.

The native makefiles build and select `cc211` for application compilation, and
build `ld211` before the executable link. Their selected profile uses
`native/link-milestone.sh` or `native/link-full.sh`. A full-profile native
executable has linked; its runtime behavior is unverified. The selected
milestone manifest has 91 application objects, compared with 149 in the full
manifest. The milestone executable passed its native two-window and
two-session tests on Minerva on November 26, 2026.
See [OVERLAYS.md](OVERLAYS.md) for candidate planning from native
object sizes and the [port README](../README.md) for profile limits.

The native client/server protocol is now version 7. Its payloads are at most
256 bytes. Identify messages carry a terminal device pathname; ordinary `read`/`writev` moves framed messages over
local Unix sockets, and descriptor-bearing messages are rejected. The server
validates and opens the named terminal. This is an application-only workaround
for the old rights path's reproduced kernel panic, requiring neither an external
gateway nor a kernel change. Host protocol 5 keeps descriptor passing. See
[IPC.md](../native/IPC.md) for the reproduction and validation.

If resident libc cannot fit, `make libc` prepares two private archives below
`native-tmp`. Its assembly-entry manifest comes from the Links port's audited
Minerva libc. Re-audit it if that libc changes. The safe archive contains only
members whose exported text entries have ordinary C prologues; the root archive
retains assembly entries, including `csv.o` and `mon.o`. Unlike Links, tmux has
no separate custom `csv.o` supplying those runtime symbols.

Place `libc-safe.a` in a measured code overlay, then `-Y libc-root.a` to select
needed assembly members into resident text. Preserve the application's normal
startup object in resident text. Do not force-load the root archive or blindly
link all files in `libc-root.list`: startup/runtime symbols could conflict and
unused routines waste the small resident window. The list is for inspection,
not an instruction to load every member. Profiling needs a separate audit.

Some root assembly entries refer back to safe C functions. The inherited audit
identified `___acvt`, `__logname_valid`, `_ecvt`, `_fcvt` and `_longjmperror` as
possible such dependencies. If needed, a measured link can retain the relevant
symbol with `-u` before the safe archive; do not retain all of them by default.

The private helpers have built on Minerva, and a full-profile overlay executable
has linked. The milestone build uses 26 commands, compact cells and a 16-color
palette, 128-byte status formatting without date/time expansion, epoch creation
times in `list-sessions`, and zero saved-history defaults. Its native session
tests and measured memory are recorded in the [port README](../README.md).
Do not treat a successful helper build or executable link as runtime validation.
