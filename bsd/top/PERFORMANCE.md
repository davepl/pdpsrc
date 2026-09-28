# Native size, memory, and timing comparison

Measured on minerva, the user's `.26` host, on 2026-09-28. Both executables
run against the same live 2.11BSD patch-level 481 kernel. The native ABI
uses 16-bit `int` and 32-bit `long`; the build uses `-O` and separate
instruction/data spaces (`-i`).
The kernel has 122 process slots and a 60 Hz accounting clock.

The reference is the existing `/usr/ucb/top`, whose source credits Digby
Tarvin and subsequent contributors. The candidate is this directory's
native build. These results describe this machine and workload.

## Executable size

| Executable | Stripped file bytes | Instruction text | Initialized data | BSS |
| --- | ---: | ---: | ---: | ---: |
| Installed `/usr/ucb/top` | 38,894 | 35,328 | 3,550 | 6,684 |
| Previous pdpsrc implementation | 38,238 | 33,856 | 4,366 | 7,696 |
| This implementation | 34,260 | 30,080 | 4,164 | 5,560 |

The new file is **11.9% smaller than the system reference**, and its
instruction text is 14.9% smaller. The previous pdpsrc file was 53,772 bytes
with symbols; stripping a preserved copy reduces it to 38,238 bytes. The
comparison above removes that unequal symbol-table overhead. Symbols do
not account for a running process's data-space use. `make` keeps a separate
`top.debug` with symbols while stripping `top`.

## Repeated native measurements

| Metric | Installed `/usr/ucb/top` | This build | Change |
| --- | ---: | ---: | ---: |
| Median CPU for 1,000 requested refreshes | 7.400 s | 6.717 s | 9.2% less CPU |
| CPU range across five trials | 7.267–7.717 s | 6.350–6.833 s | All candidate trials below all reference trials |
| Maximum observed data + stack, forced trials | 53,696 bytes | 34,304 bytes | 36.1% less allocation |
| Median first useful display, default interval | 1,066.668 ms | 16.667 ms | About 1.05 s earlier |

All ten forced trials completed their 1,000 refresh requests with normal
exit and no observer/deadline failure. The candidate's CPU improvement
appeared in every paired trial; the reported percentage compares medians,
not the best run. Default startup was identical at the clock's resolution
across each program's five separate trials. The first display has different
interval-field semantics, explained below.

These are total CPU costs per matched request count, not a claim about
CPU utilization percentages over unequal wall times. The candidate's
median terminal output in the forced runs was 117,523 bytes versus 81,350
for the reference: this version does **not** claim lower serial traffic.
Its extra consistency checks and different row ordering/rendering behavior
remain included in the measurement.

Raw logs: [forced updates](tests/bench/results/minerva-pl481/final-forced.log),
[default startup](tests/bench/results/minerva-pl481/final-default.log).
The [summary](tests/bench/results/minerva-pl481/summary.csv) and
[build identities](tests/bench/results/minerva-pl481/build.txt) preserve the
calculated values and hashes.

## Method and scope

The native observer in `tests/bench/` launches each unchanged executable
in an 80-column, 24-row VT100 pseudo-terminal as root. It drains terminal
output locally, so Telnet transport and Mac rendering are outside the timed
path. Ordinary background services and the observer remain present; this
is a live system rather than a fabricated process-table fixture.

After warmup, five trials per program request 1,000 space-key refreshes
with `-s1`. Program order reverses in alternate trials. The observer keeps
at most one unread refresh key queued, and successful exit after the final
`q` confirms that the preceding update completed. Every accepted run must
report exactly 1,000 requests, successful child exit, and no deadline or
observer failure. Child CPU is native `wait4` user plus system time; it
excludes the observer's CPU. It includes startup and exit, amortized across
1,000 requests. Throughput wall time also includes observer pacing and is
not treated as pure computation time.

Five separate three-second `-s1` runs measure ordinary startup. Startup is
from before `fork` through the last byte of the first statistics display;
a 50 ms quiet gap confirms completion but is not added to that timestamp.
The old program deliberately waits for the first interval. The new program
immediately shows identities, cumulative accounting and memory, while
labeling interval CPU fields as pending. Its startup gain includes that
intentional sampling-policy improvement, not just faster computation.

Allocation means the maximum **observed simultaneous data plus stack**
after the first display, sampled about every 50 ms. It excludes shared
instruction text and is not physical RSS or a guaranteed instantaneous
peak. Both programs face the same 64 KiB data address-space limit. Heap
layout and background process counts can change these observations.

Short idle runs accumulated only a few CPU ticks, so their CPU differences
were unsuitable for a speed claim. The fixed-request trials accumulate
seconds of CPU instead. Results are warm-cache measurements; neither disk
cold-start performance nor every terminal size and system load is claimed.

See `tests/bench/README.md` for commands, observer details, and the summary
script. Raw final measurements and executable hashes are retained alongside
this report's results so the calculations can be checked.

## Functional checks

- Native build with `cc -O`, `-i`, and `-ltermcap`; no curses linkage.
- Immediate useful first display with explicitly pending interval fields.
- First-key help, sort shortcuts, idle filtering, redraw, and invalid sort
  and delay input.
- VT100 at 80x24 and 132x36, oversized 200x80 safely capped, and a 30x8
  resize notice. A live resize plus SIGWINCH switches to 30x8 and back to
  132x36 correctly. `TERM=xterm` starts without the old allocation failure.
- Quit and Ctrl-C, including an interrupted prompt, return to the shell;
  original terminal flags are restored.
- More than 32 occupied process slots, a stopped child, an intentional
  zombie, and process creation/exit while sampling. The monitor grows its
  row array, reports the states, and renders the zombie separately. The
  bounded workload cleans up all 36 direct children.
- Host renderer tests interpret terminal output into an independent grid:
  200 randomized frames plus unchanged output, changed spans, attributes,
  clipping, disappearing rows, dimensions, failures, and tty restoration.
- All 29 exact decimal-format cases pass on both the modern host and the
  native 16-bit ABI, with guards around the output field. The reproducible
  `tests/native-fixed.sh` runner also passes under the target's original
  `/bin/sh`, using the target compiler and libraries.

The native decimal test exposed `uldiv(4294967295, 1)` returning zero on
this system. Treating division by one as the identity avoids that library
edge case. Other formatter divisors are at least two, so their largest
quotient fits below the signed-long limit. Assembly inspection also showed
separate `uldiv` and `ulrem` calls for quotient/remainder expressions;
reusing a quotient removes a repeated software division. Comments explain
these choices beside the arithmetic.

The original curses implementation's historical verification is preserved
in `VALIDATION.md`; it is not evidence for the new renderer's test counts.
