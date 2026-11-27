# Host regressions

From `bsd/tmux`, run:

```sh
python3 tests/run-host-regressions.py
python3 tests/run-host-regressions.py --milestone
python3 tests/test-native-makefiles.py
python3 tests/test-asprintf.py
python3 tests/test-xsnprintf-native.py
python3 tests/test-native-input-args.py
python3 tests/test-allocator-stats.py
python3 tests/test-imsg-native.py
```

This requires Python 3, Clang, GNU make, and curses development headers/libraries
(macOS Command Line Tools provide the host dependencies). macOS and Linux are
supported. The default builds the full command set. `--milestone` independently
builds the explicit 28-command native profile with its inactive command and
mode objects omitted, matching the native milestone filter.
`--keep` retains the temporary build and log; failures also retain
them. The runner copies sources and generates/configures headers only inside a
temporary directory. It does not alter the repository's native configuration or
run the PDP-11 compiler.

The build checks the generated per-unit declaration sets using host system types,
and compiles the generated release C copies plus pre-expanded trees inside
the temporary copy. It does that twice. The first pass has every string
literal in place and only checks syntax, so that the compiler can check each
format string against its arguments. The second builds exactly what the
native compiler is given: repeated literals are references into the shared
array `native/xstr.c`, which is `const` here so that a write through a shared
string faults as it would for a literal, and error text is read from the
generated message file through `TMUX_MSGFILE`. It enables `TMUX_SMALL_MEMORY=1`, `TMUX_IO_SIZE=256` and
`TMUX_NATIVE_WIDTH_CODE=1`, so the regular Unicode tests use the native width code.
The milestone build also enables `TMUX_NATIVE_GRID_ALLOC=1` and
`TMUX_NATIVE_HEAP_TEST=1` to exercise native grid, option-name and CSI allocation
paths without selecting the old native system headers.
The tests link the actual tmux objects and cover:

- Trace-expression removal preserves declarations, literals and unbraced control
  flow, and rejects arguments with possible side effects.
- Shared strings: which literals may be replaced (not array initializers,
  `sizeof` operands, text joined to an unknown macro, or escapes compilers read
  differently), three-valued `#if` evaluation, tail sharing, offsets that
  survive later additions, and a rewritten program that behaves as the
  original. Message relocation: only the listed argument positions, offsets
  that survive additions, and a stamp that changes with the file. The
  checked-in copies must be current and refer only to offsets that exist.
- `message-file.c`: every message read back whole from the generated file
  with `errno` undisturbed, a real command error formatted with its argument,
  the numbered fallback for a missing file, a file from another build, a
  truncated file and an offset past the end, and the file found beside a
  relative executable path.
- The real fallback `asprintf`/`vasprintf` against old negative-on-truncation
  and C99 formatters: no zero-capacity calls, 64-byte/retry boundaries, varied
  arguments, reusable `va_list`, exact result allocations, every allocation
  failure point, persistent formatting errors and 16-bit capacity overflow.
- Actual native `xsnprintf`/`xvsnprintf` measurement calls use capacity one
  with an old formatter that rejects capacity zero. Tests cover NULL/non-NULL
  zero-size destinations, caller canaries, varied arguments, truncated/full
  output, formatter failure and the signed 16-bit capacity guard.
- Native imsg read/writev framing, 15-vector/32767-byte bounds, partial I/O,
  retry/error/EOF behavior and explicit descriptor rejection. Native object
  symbols must contain no sendmsg/recvmsg references. A separate ordinary-host
  test verifies real descriptor transfer remains available in that branch.
- Native identify encoding without descriptor transfer, bounded tty paths,
  device/path validation, reopen failures and descriptor ownership. Its host
  branch retains the original descriptor transport.
- Native allocator contracts: fresh `xrealloc` uses `malloc`, and optional
  drained-buffer shrinking keeps its live allocation after failure while
  preserving `errno`. The ordinary host branch keeps its realloc behavior.
- Optional allocator statistics walk synthetic 16-bit rings, including the
  largest representable arena, free/busy payload totals and malformed links.
  The diagnostic remains disabled unless `TMUX_ALLOC_STATS` is explicitly set
  for the native build. Its busy total includes reserved gaps from external
  `sbrk` calls; the walk itself does not allocate or alter the arena.
- Native milestone global option names borrow their audited static literals;
  subordinate option names remain independent copies. Tests cover all setter
  types, replacement, removal, destruction and inherited lookup.
- CSI argument storage starts at four slots and grows without limiting argument
  count or the existing 64-byte field; tests cover growth, reuse and long numeric
  arguments with leading zeroes. A sanitized standalone harness compiles the
  actual growth/add functions with 16-bit size/count fields to check 992-argument
  capacity, near-limit growth and overflow rejection.
- All 52 command flag letters, combined key modifiers and distinct prefix bits.
- The milestone command allowlist, retained bindings/completion and unavailable
  commands are checked separately when `--milestone` is selected.
- Four-byte milestone cells retain all attributes, flags and byte data; API, SGR
  and style colors map the full palette before storage, retaining 16 colors and
  distinguishing bright palette index 8 from terminal default.
- Word-sized key-switch bounds, 32-bit numeric bounds and supplementary Unicode.
- Native width decision code versus the original table for every codepoint from
  U+000000 through U+10FFFF, including all boundaries and default-width gaps.
- Grid equality with unallocated/partially allocated rows, height differences,
  cells hidden after resize, and differing unused milestone bitfield padding.
- Zero-history scrolling, vertical/horizontal resizing and alternate-screen
  save/restore, including the first scroll after restore and 200 later scrolls.
- Positive-history line retention, resize recovery and limit collection.
- `vmf-history.c` (`--milestone`): history in a paging file, run against
  `tests/vmf/`, the 2.11BSD paging library's own source. 1500 lines through a
  480-row ring with every row checked; five grids interleaved through the
  two in-memory segments; slot exhaustion, release and reuse; a paging file that
  cannot grow, simulated by refusing one write, both mid-ring and at the first
  segment; history fed by the real input parser, the alternate screen, resize
  and scroll regions; and copy mode paging, searching and copying from the
  file, including the 2048-byte bound on a copy.
- Clearing a one-row screen when zero-history scrolling occurs.
- Buffer growth, partial consumption, drain compaction/shrinking, failed-shrink
  recovery, reuse, and socket-poll input/output.
- Lazy mode-key defaults, preserved customization/empty bindings, and explicit
  free/reinitialization restoring defaults.
- Milestone message-payload ownership, malformed payload-size rejection,
  command replies, attached-client environment release, and lost-client
  environment cleanup through the actual imsg dispatcher.
- Guarded grid allocations: five reallocations for an 80-cell row with 16-cell
  rounding and one with the milestone's compact rows, logical
  growth/truncation, independent odd-width and zero-width duplicates,
  line/cell moves, horizontal resize, and rejection before rounding overflow.

The failed-shrink test redirects `realloc` only while compiling its private
buffer object. It does not replace the process-wide allocator.

The dispatch test compiles one private server-client object with a command
execution hook, then links the ordinary program objects. Normal reply tests use
the real command executor; controlled return/session combinations exercise the
environment cleanup guards. Its socketpairs stay local to the test process and
it starts no sessions or daemons.

The grid capacity test redirects only grid.c's allocators, with moving
reallocations and guard bytes. It runs three storage models: exact
full-profile cells, 16-cell rounded full-profile cells, and the milestone's
compact rows (one character byte per cell in 80-cell steps plus the lazily
allocated style array). Other program objects remain unchanged.

`compact-rows.c` runs with `--milestone` against the real grid and screen
writer objects. It covers dense plain screens without style arrays, creation
of a style array by the first styled cell, release on truncation, exact round
trips of every attribute/flag/color combination, growth across allocation
steps, cell moves, duplication, equality of an all-default style array with
none, zero-history scrolling, and the wide-character padding and combining
paths that replaced writable cell pointers.

The makefile-generator tests use temporary source sentinels, host make, and
fake compiler/size tools. They check both profile manifests, bounded chunks,
failure propagation before linking, per-chunk size records, profile switching
with explicit object cleanup, preservation of both executables, and the
generated Unicode-width dependency. They do not run native tools or establish
the old make's actual memory use.

The standalone formatting test uses a temporary header shim with `INT_MAX=32767`
and redirects formatter/allocator calls only in the fallback object. It exercises
the host `va_copy` branch; native pointer-based `va_list` behavior still requires
a native check. It leaves repository configuration unchanged. The imsg test
uses host shims for native branches and only local host sockets for its ordinary
SCM_RIGHTS check; it never connects to Minerva or runs a tmux server.

`python3 tests/test-native-identify.py` runs the identify checks separately.
The runner extracts the current canonical functions and identify message layout
into mock-only C harnesses, using AddressSanitizer by default. It checks invalid
paths, symlinks/non-devices, changed device identities, each reopen failure,
error/exit replies, pathname limits and unchanged host descriptor encoding.
`--no-sanitize` supports compilers without AddressSanitizer. These mocks open no
terminal devices or network connections.

`python3 tests/test-native-allocator.py` compiles the actual `xrealloc` and
buffer code with allocator fault injection and AddressSanitizer. The native
model rejects `realloc(NULL)` and frees its input on a failed reallocation;
the host model retains the ordinary contracts. The checks cover allocation
failure, preserved contents, bounds rejection, shrink failure/reuse, errno
preservation and final allocation ownership.

These checks validate host logic and declarations. They do not establish native
ABI correctness, PDP-11 memory fit or interactive behavior on 2.11BSD.
