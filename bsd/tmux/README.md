# tmux 1.1 for 2.11BSD

This is a native PDP-11 port of tmux 1.1. The complete upstream command set is
retained in the full source profile; a compact milestone profile reduces native
data use. The source baseline and original licenses are recorded in
[UPSTREAM.md](UPSTREAM.md). The first runtime milestone is one session with two
80-column windows, window switching, and detach/reattach on Minerva.

**Native runtime validation: the milestone profile PASSED on Minerva**
(2.11BSD kernel #42, simulated PDP-11/83, November 26, 2026). One session with
three windows, each filled to all 80x23 cells, survived window switching,
detach, `list-windows` and reattach with every shell PID unchanged, and the
detached server kept running; in the same run a window's scrollback was paged
through in copy mode and a line of it copied and pasted. Two sessions with two
clients attached at once (three populated windows) passed the same checks,
including each client reattaching to the other's session. One server holds six
populated windows and loses everything when a seventh is opened. See
[Native results](#native-results-and-measured-memory) for the numbers and the
limits that remain. The full profile has linked natively but has not been run;
its memory needs are larger and it is not expected to fit as it stands.

The selected milestone profile has 97 objects; the full profile has 152.
Passing host regressions or linking an executable does not establish that
populated windows fit or run correctly on a PDP-11: only the native session
tests do.

The first milestone binary passed descriptors over the Unix socket and
reproduced a kernel panic in an isolated Minerva clone. Native builds have not
used that path since protocol 6; see [native/IPC.md](native/IPC.md). The panic
is a bug in the kernel's `recvmsg`; [kernel-recvmsg/](kernel-recvmsg/README.md)
has the one-line fix and test programs.

## License

tmux and the port's own code are under the ISC license. The compatibility
files tmux ships are ISC or BSD licensed, and three build-tool files come
from 2.11BSD under the Caldera and Berkeley licenses. [LICENSE.md](LICENSE.md)
lists every part, its owners and its terms.

## Source layout

| Path | Purpose |
| --- | --- |
| `upstream/` | Canonical tmux C sources, retaining the upstream licenses and host branches. Make source changes here. |
| `upstream/tmux-full.h` | Authoritative complete application header. |
| `upstream/tmux.h` | Selects the full host header or generated native header. |
| `upstream/tmux-native.h` | Generated per-compilation-unit declaration sets, keeping the native compiler within its symbol limits. |
| `upstream/config.h` | Checked-in native configuration; do not overwrite it with `configure` on the PDP-11. |
| `native-src/` | Generated native release copies of upstream C: debug traces omitted, repeated string literals replaced by references to a shared array, error text replaced by message-file references. Profile selection determines which objects are compiled and linked. |
| `native/xstr.c` | Generated array of the string literals the native objects share. |
| `native/tmux.msg` | Generated message file: the error text the native executable reads at run time. It is installed beside the executable. |
| `native/` | 2.11BSD terminal, pty, termcap, string and working-directory adapters, plus protocol-size checks and compiler wrappers. |
| `native/trees/` | Checked-in, pre-expanded RB/SPLAY implementations from the canonical tree macros. |
| `native-mk/Gxx.mk` | Generated small makefiles, each compiling at most eight objects: 13 chunks for milestone or 19 for full. |
| `native/objects-full.list` | Authoritative full 152-object source manifest, retained across profile changes. |
| `native/objects.list` | Generated selected-profile manifest for build and link planning: 97 milestone or 152 full objects. |
| `tools/` | Host generators and overlay planner; private native compiler driver/linker and optional libc preparation. |
| `tests/` | Isolated modern-host build and focused regressions. `tests/vmf/` is the 2.11BSD paging library's source, used only by them. |
| `kernel-recvmsg/` | The 2.11BSD `recvmsg` fix found during the port, with native test programs. Not needed to build or run tmux. |

The native adapters use 2.11BSD's BSD ptys, sgtty interfaces and termcap
library. The native protocol (version 7) carries the client terminal pathname
and uses ordinary `read`/`writev` on local Unix sockets, avoiding kernel
descriptor passing. Its message payloads are at most 256 bytes. The server validates and opens the terminal device itself. The design
uses the existing kernel and needs no external gateway. Host protocol 5 retains
its original descriptor transport. See [native/IPC.md](native/IPC.md) for the
porting reason and [tools/README.md](tools/README.md) for private build tools.

## Native build

Use the installed ANSI C compiler and native development headers on Minerva.
The build also needs the installed termlib and `libvmf`: the private linker uses
the latter for its working storage, and tmux keeps window history through it. Preserve the sibling
`bsd/pdp11_unistd.h` when transferring this directory. All generated sources,
headers, tree expansions, and makefiles are checked in: Python and Clang are
not required on the PDP-11.

From `bsd/tmux`:

```sh
make objects
```

The checked-in default is milestone. This builds the private `tools/cc211`,
then runs the 13 selected child makefiles in
sequence and prints native object sizes. The child processes are intentional:
the old make exhausts its data space when all expanded object rules are parsed
in one process. A failed group records failure while later groups are still
checked, and the overall command returns failure. Do not infer completion from
the last compiler line alone.

To retry a single group, from `upstream`:

```sh
make -f ../Makefile.211bsd G01
```

To retry one object without scanning all groups, select the child makefile
containing it. For example, from `upstream`:

```sh
make -f ../native-mk/G01.mk attributes.o
```

The checked-in child recipes use the private driver and shell wrappers for the
installed `cpp`, `c0`, and `c1` passes. Keep the `PDP11_UNIT_*` definition from
the object's recipe: it selects that unit's declarations. Native compiler
startup and preprocessing failures have required these private wrappers in
other ports; changing system tools is not part of this build.

After all objects compile, collect their native sizes and follow
[tools/OVERLAYS.md](tools/OVERLAYS.md) to review a complete overlay candidate.
The root `make` target builds `tools/ld211`, checks the object groups, invokes
the selected profile's link script, copies `native/tmux.msg` beside the
executable, and prints the executable size. The executable reads its error
text from that file, so the two travel together; see
[tools/README.md](tools/README.md#strings-and-messages). Milestone
uses `native/link-milestone.sh` and produces `upstream/tmux`; full uses
`native/link-full.sh` and produces `upstream/tmux-full`. The two executable
names preserve a full baseline while testing the smaller profile.

**Run `make clean-objects` before switching profiles.** Both profiles share
object paths, and compiler flags alone do not identify which profile produced
an existing object. This target removes all known application objects from the
full manifest in bounded commands; it preserves both executables, source files,
generated files, and private tool binaries. `make clean` additionally removes
only the selected profile's executable. `cd tools` followed by `make clean`
removes the private tool binaries separately.

Profile generation happens on the modern host. After cleaning native objects,
select the full profile with:

```sh
python3 tools/generate_native_makefiles.py --profile full
```

Transfer the generated `Makefile`, `Makefile.211bsd`, `native-mk/`, and
`native/objects.list` to Minerva, then run native `make objects` or `make`.
To return to milestone, clean native objects again and regenerate on the host
with `--profile milestone` (the default). No Python is required on Minerva.

When staging files, preserve executable permission on shell wrappers and build
scripts. Compare source timestamps with the native clock if make reports a
missing rule for an existing file or repeatedly rebuilds unchanged objects;
zero or future timestamps are unsuitable for this old make. Use a script file
and a short shell invocation for long commands: the interactive native console
can truncate long input lines.

## Memory profile and current limits

PDP-11 overlays enlarge the available code, not the flat 64 KiB address space
shared by data, BSS, heap, and stack. Every byte of static data is a byte the
heap does not get, and every window and client lives in the heap. Three
measures keep data out of that space. Steven Schultz pointed to the 2.11BSD
facility behind each of them:

- **Strings are stored once**, as `xstr(1)` arranges. The native compiler
  gives every string literal its own storage, and tmux repeats many: each
  `fatal()` expanded to a format, a file name and a message at some 190 call
  sites, and option names recur wherever an option is read. The generated
  native sources refer to one shared array instead, and `fatal()` passes the
  unit's name to a function in `log.c` that holds the one format.
- **Error text is in a file**, as `mkstr(1)` arranges. The text of fatal
  errors, command errors and warnings is in `tmux.msg`; the program holds the
  byte offset and reads the text when the error is reported. Without the file
  an error is reported as `error N`, and N is the offset of its text in
  `native/tmux.msg`.
- **Window history is in a paging file**, through `vmf(3)`. A row that scrolls
  off a window is written as an 85-byte record to an anonymous file in `/tmp`,
  twelve records to a 1024-byte segment, of which two are in memory at a
  time. History therefore costs the heap about 2.1 KiB in total, whatever the
  number of windows and rows. `upstream/grid-history.c` has the details.

The native configuration keeps 480 rows of history per window and one paste
buffer. The full profile can change such defaults with its option-setting
commands; the milestone profile uses the built-in defaults.

The current small-memory implementation also:

- Starts pane and terminal I/O buffers at 256 bytes, grows in bounded 256-byte
  increments as needed, and
  attempts to shrink drained buffers back to 256 bytes. A failed shrink keeps
  the old allocation usable.
- Loads each default mode-key table on first use, while preserving customized
  bindings and intentionally emptied tables.
- Bounds every native IPC payload at 256 bytes and each connection's read
  buffer at 272. Compile-time checks cover all six fixed protocol payload
  structures. Text replies are queued at their used length, not as fixed
  256-byte records. A packed command line is limited to 250 bytes, a working
  directory to 189, and environment variables longer than 255 bytes are not
  forwarded to the server.
- Writes terminal output during a redraw once 192 bytes are pending, with a
  bounded wait for a busy terminal, so a full-screen redraw does not first
  collect in one multi-kilobyte heap buffer.
- Frees termcap setup scratch storage after copying capabilities, releases
  inherited stdout/stderr buffering in the server child, and frees startup
  command storage before entering the server loop.
- Embeds Unicode width ranges in small generated code functions rather than
  a writable lookup tree, preserving the complete existing width table.
- Omits 57 debug trace expressions from native release C. The audit counts
  1093 string-literal payload bytes before preprocessing; this is not a
  measured linked-data saving. Logging definitions and host trace calls remain.

Screen rows allocate lazily. Many windows or clients, screens that are styled
in every row and UTF-8 workloads are not established to fit; the measured
cases are listed under [Native results](#native-results-and-measured-memory).

### Compact milestone profile

`TMUX_MILESTONE` selects a 28-command surface for the native executable. Its
linked object list must match this command table. It retains:

```text
attach-session   bind-key         command-prompt   copy-mode
detach-client    display-message  has-session      kill-server
kill-session     kill-window      last-window      list-clients
list-commands    list-keys        list-sessions    list-windows
new-session      new-window       next-window      paste-buffer
previous-window  refresh-client   rename-session   rename-window
select-window    send-keys        send-prefix      unbind-key
```

The normal prefix defaults for `c`, `n`, `p`, `0` through `9`, `d`, `l`, comma,
colon, `?`, `i`, `r`, `[`, `]`, PageUp and prefix forwarding remain. Default
bindings to omitted commands are absent, including prompt templates for
move-window and find-window. Command completion uses only retained commands.
Editing and choice mode keys remain for the command prompt and paged output.
Copy mode uses the emacs key table; the vi table is left out because no
command of this profile can select it. Choose-window mode, clock mode,
pane-management commands, and option-setting commands are outside this
profile. Built-in option defaults
still control status and window behavior. The full command and mode tables
remain available when `TMUX_MILESTONE` is not defined; the ordinary host build
does not enable it.

History and copy mode in this profile:

- `C-b [` (or `C-b` PageUp) enters copy mode: page and scroll through the
  window's history, search it, select, and copy. `C-b ]` pastes.
- A window keeps its last 480 rows. Twelve windows can have history at once;
  a thirteenth works without it. If `/tmp` cannot hold more, a window's
  history stops growing at the size it has reached instead of failing.
- A history row keeps its characters and whether it wrapped, not colors or
  attributes: text that has scrolled off is shown plain. A character of a
  UTF-8 sequence is kept as `_`.
- Rows in history stay there when a window is made taller; the new rows at
  the bottom are blank. The original moves rows back out of history.
- A copy holds at most 2048 bytes, and there is one paste buffer. Copying
  more says so on the status line and keeps the first 2048 bytes.

The milestone also makes these explicit memory tradeoffs:

- `struct grid_cell` keeps all eight attribute bits, all four flags, four-bit
  foreground/background colors and the full data byte in four bytes, but it is
  now only the interchange form. A stored row holds one character byte per
  cell. The other 20 bits take three style bytes per cell in a second array
  that exists only while some cell of that row differs from the default style;
  it is released when a truncation leaves no such cell. Rows are allocated in
  80-cell steps, so the rows of an 80-column grid are one block size and a row
  freed by a scroll is exactly reusable. A plain 80x23 screen costs about
  1.9 KiB instead of 7.4 KiB; a screen styled in every row costs what it did
  before. Cells are read with `grid_peek_cell`, whose result is valid until
  the next peek, and written with `grid_set_cell`; the writable-pointer
  `grid_get_cell` does not exist in this profile. UTF-8 cell storage and width
  handling remain present.
- The stored palette has 16 colors. Incoming 256-color SGR sequences, color
  APIs and embedded styles are mapped to that palette before assignment.
  Bright palette index 8 remains distinct from terminal-default color 8.
- Status formatting uses 128-byte input/output buffers and retains tmux `#`
  substitutions. Date/time `%` formats are not expanded; the default right
  status contains the terminal title without a clock or date.
- `list-sessions` prints session creation time as decimal Unix epoch seconds,
  avoiding the date/time formatting tables.

The effective terminal size is capped at 80x24. On a wider terminal the
terminal's own automatic wrap does not happen at column 80, so the server
emits the carriage return and line feed itself wherever its cursor tracking
assumes a wrap. Keep `tmux.o`, which contains the signal handler, resident
during overlay planning.

### Native results and measured memory

Measured on Minerva on November 26, 2026, on the executable linked at 22:40.

| | Before the three measures | Now |
| --- | --- | --- |
| Resident text | 28160 | 30016 |
| Data | 25326 | 16860 |
| BSS | 1072 | 1300 |
| Total text | 183104 | 197696 |
| Overlays | six of 24576, one of 7488 | six of 24576, one of 20224 |
| Data space left for the heap | 30946 | 39184 |

The data segment can grow to 57344 bytes: seven of the eight 8 KiB segments,
the last being the stack's. Static data and BSS come out of that, and the
heap has the rest. The earlier `[lost server]` exits were this limit: with
four-byte stored cells two populated windows needed 14.7 KiB by themselves,
each connection held a 2.3 KiB read buffer, and each received command was
copied into a further 2 KiB block. The logged failure was `xrealloc failed`
with the break at 57344.

The 8238 bytes of static data went in three steps, each linked and run
natively. The "now" column also contains what was added on the way: copy
mode, the paste command, the emacs copy key table and the history code.

| Step | Data + BSS after it |
| --- | --- |
| Before | 26398 |
| Shared strings, the `fatal()` rewrite, and the added features | 20426 |
| Error text moved to `tmux.msg` | 18482 |
| vi copy key table left out; two history segments in memory instead of three | 18160 |

Passed natively on the final executable:

- One session, three windows filled to 80x23, window switching, detach,
  `list-windows`, reattach, and all three shell PIDs unchanged throughout.
  In the same run: 150 lines printed in window 0, copy mode entered with 166
  rows of history, two pages up showing rows that had scrolled off, one row
  selected and copied, and `C-b ]` pasting `HIST_LINE_108` at the shell
  prompt. The outer terminal was 100x40, so this also covers the 80x24 cap on
  a larger terminal.
- Error text from the message file: `session not found: nosuch` from the
  server, `server not found: No such file or directory` from the client, and
  with `TMUX_MSGFILE` naming a file that does not exist,
  `error 2338 (no tmux.msg for this build): No such file or directory`.
- Two sessions (two windows and one), two clients attached at the same time
  from separate logins, all three windows filled, then each client detached
  and reattached to the other session with all three shell PIDs unchanged.
  The outer terminals were 80x24.
- With the diagnostic build described below: 600 lines through each of two
  windows, copy mode then showing the 480 rows kept, seven pages up, a
  four-page selection copied, three detach/attach cycles, `list-keys` (26
  bindings) and `list-commands` (28), and six populated windows. That
  selection was about 1.2 KiB, so the 2048-byte cut-off was not reached
  natively; the host test covers it.

Passed earlier the same day on the previous executable and not repeated on
this one: the paged `list-keys` display (prefix `?`), the command prompt with
`rename-window`, last/next/previous window, and `vi` running in a pane, each
checked by replaying the terminal output through a VT emulator.

Heap in use, from a diagnostic build of the same sources
(`-DTMUX_ALLOC_STATS` on `xmalloc.c` and `cmd-list-sessions.c`, which adds an
allocator line to `list-sessions` and writes the block list to
`/tmp/tmux-heap.dump`; it has 196 bytes more static data). Each figure
includes the connection that ran `list-sessions`:

| State (one client attached unless noted) | Heap in use | Break |
| --- | --- | --- |
| Detached, one empty window | 7792 | 29696 |
| Attached | 11084 | 32768 |
| Window 0 filled | 12844 | 33792 |
| Second window, showing the login banner | 18832 | 39936 |
| Second window filled with plain text | 16112 | 39936 |
| 600 lines printed in the second window | 16112 | 39936 |
| Copy mode entered, three pages up | 18980 | 40960 |
| Four pages selected and copied, copy mode left | 17734 | 40960 |
| Three windows filled | 21002 | 45056 |
| 600 lines printed in the third; three detach/attach cycles; the list commands | 21002 | 45056 |
| Four windows filled | 24270 | 49152 |
| Five windows filled | 27538 | 53248 |
| Six windows filled | 30806 | 56320 |
| Seventh window opened | server lost | |

Scrolling 600 lines through a window changed nothing in the heap: history is
in the paging file, and its two 1024-byte segment buffers are allocated once,
with the first window. A filled plain-text window costs 3268 bytes; the same
window costs 5988 while the login banner with its colors is on it. Before
these changes three populated windows had the break at 51200. Six is the
measured ceiling now, with 1024 bytes of break left: opening the seventh
ended the server with `[lost server]`, and its six shells with it. The
server's stack reached 2876 bytes of its 8 KiB segment.

Limits that remain:

- A client can attach only from a terminal that the server's user may open,
  because the server opens the terminal by pathname itself. In practice, log
  in as the user who started the server; `su` does not change who owns the
  terminal. Root attaching to another user's server must first run
  `chmod o+rw` on its terminal. The client recognizes the case where the open
  is certain to fail and says so, naming the server's user and the command,
  instead of relaying the server's "cannot reopen client terminal". That
  decision is unit-tested on the host; it has not been run natively from a
  second account. A server started by root can open any terminal.
- Running out of heap is still fatal to the server; nothing degrades
  gracefully, and nothing refuses a window that will not fit. With one
  client, the seventh window is the one that does not. Two clients and
  styled or UTF-8 screens lower that number; how far was not measured beyond
  the two-client, three-window run above.
- `tmux.msg` has to be the one generated with the executable. With no file,
  or a file from another build, every error is reported as `error N`; the
  stamp check that decides this is what keeps a foreign file's formats away
  from this build's arguments.
- History is plain text, is not moved back when a window grows, and exists
  for at most twelve windows; see the profile description above. The heap
  runs out long before a thirteenth window, so that case has only been run
  on the host.
- Resizing, signal delivery, more than two clients, UTF-8 output, a full
  `/tmp` and long-running sessions have not been exercised natively. The full-`/tmp` path is covered
  on the host by refusing a write.
- The native compiler is close to its own memory limit in the largest units.
  During this work `client.c` stopped with `xrealloc: out of mem` reported at
  line 2047 of `tmux-native.h`, a message the installed preprocessor (the PCC
  one) contains. The few declarations that string sharing first put ahead of
  the header were enough to cause it. Turning them into macros and taking two
  unused system headers and the terminal-ownership check out of that unit
  (`client-tty.c`) cured it. `server.c` showed the same failure earlier when
  diagnostic declarations were added to it. Additions to the shared header
  need a native compile of those two before anything else is trusted.
- 2.11BSD returns `EWOULDBLOCK`, not `EAGAIN`, when a non-blocking descriptor
  cannot proceed; native units map `EAGAIN` to it in `compat.h`. Before that
  change every would-block condition was treated as a lost connection.

## Regenerating and testing on a modern host

Edit canonical C in `upstream/`, declarations in `tmux-full.h`, and adapter C
in `native/`. Do not repair generated copies directly. From `bsd/tmux`, run:

```sh
python3 tools/generate_native_widths.py
python3 tools/generate_native_trees.py
python3 tools/generate_native_header.py
python3 tools/generate_native_sources.py
python3 tools/generate_native_makefiles.py
python3 tools/generate_native_sources.py --check
python3 tests/test-native-makefiles.py
python3 tests/run-host-regressions.py
python3 tests/run-host-regressions.py --milestone
```

The tree generator needs Clang, or another host preprocessor supplied with
`--cc`. It verifies that line formatting preserves the expanded C tokens. The
release-source generator rejects trace arguments with possible side effects
and records the omissions in `native-src/TRACE-AUDIT.json`. When adding or
removing source objects, edit `native/objects-full.list`, then regenerate the
makefiles and selected manifest. The generator derives milestone commands from
the guarded table in `upstream/cmd.c` and removes the same inactive command/mode
objects as the host runner. Do not edit generated `Gxx` lists or makefiles.
The generator's default is milestone; pass `--profile full` explicitly when
preparing the full build.

The host runner requires Python 3, Clang, GNU make, and curses development
headers/libraries on macOS or Linux. It configures and builds only a temporary
copy, exercises the generated native declaration sets, release C, pre-expanded
trees, and actual small-memory paths, and leaves the native configuration
untouched. `--keep` retains its scratch build; failures retain it automatically.
See [tests/README.md](tests/README.md) for coverage and limits.

The old native compiler has tighter limits than a modern host compiler. Avoid
automatic array/structure initializers, oversized macro expansions, assumptions
that `int` is 32 bits, and large persistent stack objects in native paths.
Regenerate after source/header changes, run the host checks, then rebuild the
affected native groups. Only native compilation, link inspection, and actual
session tests can complete the port's validation.
