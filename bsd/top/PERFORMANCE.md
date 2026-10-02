# Native size, memory, and timing comparison

The selected build is **34,594 bytes, 11.1% smaller than `/usr/ucb/top`**.
It uses **60.9% less CPU for 3,000 forced refresh requests** and **36.1%
less observed data plus stack** than the reference in the matched trials.
Ordinary one-second-refresh runs charged the selected build only one CPU
tick each, too little for a precise normal-refresh speed comparison.
Measurements ran on minerva (`.26`), a live 2.11BSD patch-level 481 system,
on 2026-09-28.
Its ABI has 16-bit `int`, 32-bit `long`, 122 process slots, and a 60 Hz
accounting clock. The native build uses `cc -O`, separate instruction/data
spaces (`-i`), and termcap.

The system reference's source credits Digby Tarvin and later contributors.
The previous PR13 build is commit `7ddff07`. The selected source is commit
`5975349`, called `best3` in the optimization pilots.

## Executable size

| Executable | Stripped file bytes | Instruction text | Initialized data | BSS |
| --- | ---: | ---: | ---: | ---: |
| Installed `/usr/ucb/top` | 38,894 | 35,328 | 3,550 | 6,684 |
| Previous PR13 build | 34,260 | 30,080 | 4,164 | 5,560 |
| Selected build | 34,594 | 30,464 | 4,114 | 5,902 |

The selected build spends **334 additional file bytes (1.0%) over PR13**
on reducing work repeated across refreshes. That is an explicit size/speed
tradeoff; it remains 4,300 bytes smaller than the system reference.
Instruction text is 13.8% smaller than the reference.

All file comparisons use stripped executables. The original pdpsrc binary
was 53,772 bytes with symbols, or 38,238 bytes after stripping a preserved
copy; its unadjusted size was not a fair comparison with the stripped system
program. `make` retains `top.debug` separately. Symbols are not part of the
running process's data-space allocation.

[Build identities and hashes](tests/bench/results/minerva-pl481-iteration/build.txt)
record the frozen binaries, source inputs, compiler options, and native sizes.

## Fresh final measurements

| Metric | System reference | Previous PR13 | Selected build |
| --- | ---: | ---: | ---: |
| Median CPU seconds, 3,000 forced requests | 21.650 | 20.383 | 8.467 |
| CPU range across five forced trials | 21.500–22.233 | 20.167–20.533 | 8.333–8.650 |
| Maximum observed data + stack, forced trials | 53,696 bytes | 34,304 bytes | 34,304 bytes |
| Median output bytes, forced trials | 235,314 | 318,400 | 129,734 |
| Median CPU seconds, 120 seconds at `-s1` | 0.083335 | — | 0.016667 |
| CPU range across three ordinary trials | 0.083335–0.200004 | — | 0.016667–0.016667 |
| Median first useful display, milliseconds | 1,066.668 | — | 16.667 |
| First-display range across five startup trials, milliseconds | 1,066.668–1,083.335 | — | 16.647–16.667 |

All 15 forced trials completed exactly 3,000 requests with successful exit
and no observer/deadline failure; all ten startup and six ordinary-refresh
trials also succeeded. In forced runs, the selected build used less CPU in
every paired comparison, with disjoint ranges: median CPU fell **60.9% versus the reference** and **58.5% versus
PR13**. Its allocation matches PR13. Median forced-run terminal traffic
fell 44.9% versus the reference and 59.3% versus PR13; these traffic figures
are specific to this workload. The median first useful display arrived
about 1.05 seconds earlier, with the sampling-policy distinction below.

At ordinary one-second refreshes, the reference accumulated 12, 5, and 5
CPU accounting ticks; the selected build accumulated one tick in each
120-second run. Those observed totals are lower, but the candidate is at
the clock's accounting resolution: a precise ordinary-refresh CPU speed
comparison is **inconclusive**. The ratio of these small counts is not a
reliable speedup estimate. Each selected
run produced 117 recognized visible output bursts, confirming continued
updates; bursts are not an exact sample count.

The forced-request results accumulate seconds of CPU and remain the primary
quantitative speed comparison. They are a distinct workload: formatting
caches can benefit repeated requests within one second, while the header
clock changes on each ordinary one-second refresh.

Raw records: [forced requests](tests/bench/results/minerva-pl481-iteration/final-forced.log),
[startup](tests/bench/results/minerva-pl481-iteration/final-default.log),
[ordinary refreshes](tests/bench/results/minerva-pl481-iteration/final-regular.log),
and [calculated summary](tests/bench/results/minerva-pl481-iteration/summary.csv).
The [series script](tests/bench/results/minerva-pl481-iteration/final-series.sh)
preserves the commands and trial order. Earlier
[PR13 results](tests/bench/results/minerva-pl481/summary.csv) remain historical;
they are not pooled with these fresh measurements.

## What was retained, and where optimization stopped

The selected changes avoid repeated work without skipping current samples:

- Identical screen rows bypass another copy/sanitize/compare pass; cached
  row lengths avoid rescanning old text. Changed rows keep the bounded path.
- Decimal conversion switches to native word arithmetic once the remaining
  integer fits in 16 bits. Large values remain fully represented.
- Selected pointers and counters use explicit `register` declarations,
  which this historical compiler needs to allocate those locals to registers.
- Bounded, typed appends replace repeated `printf` format parsing in process
  rows. Field widths, rounding, padding, and unavailable values are preserved.
- The header reuses formatted text only when freshly read time, user count,
  all three loads, query status, and terminal width match its complete key.

Five rounds of [pilot results and decisions](tests/bench/results/minerva-pl481-iteration/pilots/README.md)
led to this combination. Additional fractional-arithmetic shortcuts were
correct but added 64 instruction bytes without a consistent gain: their
three paired differences were one tick faster, about 34 ticks slower, and
effectively tied. Multiple dirty spans and bulk utmp reads also lacked a
convincing tradeoff. Removing a temporary row field unexpectedly increased
observed allocation; the allocator/layout cause was not established.
Further cursor register hints produced mixed paired results, including an
unusually slow control run, with no size or memory improvement. That
experiment was also rejected; every recorded pilot remains available.

This is a practical stopping point after measured diminishing returns among
the changes tried, not a claim that no further optimization is possible.
The selected combination was assessed again in the fresh final series.

## Method and limits

After discarded warmups, the unchanged native observer runs each executable
as root in the same 80×24 VT100 pseudo-terminal. It drains output locally;
Telnet transport and Mac rendering are outside the timed path. Background
services and the observer remain part of the live workload.

The final protocol uses five interleaved 3,000-request `-s1` runs for each
of the three binaries, reversing order on alternate rounds. At most one
unread space key is queued. In the inspected key loops, successful exit on
the final `q` confirms that preceding updates finished. Accepted runs must
complete every request, exit successfully, and have no observer or deadline
failure. Native `wait4` user plus system time measures child CPU, including
startup and exit amortized over the request count. Observer CPU is excluded;
wall throughput still includes its pacing. Visible output bursts are not an
exact count of internal samples and are not used as a CPU denominator.

Three interleaved 120-second `-s1` runs compare the reference and selected
build during ordinary refreshes. An initial ordinary run was deliberately
interrupted before any accepted record to try the last cursor experiment;
the complete matched series restarted with the unchanged selected binary.
These are total lifetime CPU costs for the same duration, not isolated
per-sample or pure steady-state timings. Five separate three-second runs
per program measure startup. Its timestamp is
from before `fork` to the last byte of the first statistics display; a 50 ms
quiet gap confirms completion without being added to that timestamp.

The reference waits for its first interval. The selected build immediately
shows identities, cumulative accounting, and memory, with interval CPU
fields explicitly pending. Its earlier useful display includes this
intentional sampling-policy change, not just faster computation.

Allocation is the maximum **observed simultaneous data plus stack** after
the first display, polled about every 50 ms. It excludes shared instruction
text and is neither physical RSS nor a guaranteed instantaneous peak. The
64 KiB data address-space limit, allocation layout, and active process count
matter independently of system-wide free RAM. All timings are warm-cache
results on this machine; short differences near one or two clock ticks
and small live-system samples do not support broad statistical claims.
Terminal output is reported separately rather than inferred from CPU time.
See the [observer documentation](tests/bench/README.md) for reproduction.

## Functional verification

The actual selected source passes native `cc -O -i` checks for all **88
fixed-point golden cases**, signed 16-bit boundaries, padding/precision and
buffer guards, and 128 complete process rows against the former layout.
Rebuilding as `dave` in `/usr/dave/source/repos/pdpsrc/bsd/top` reproduced
the measured stripped binary byte for byte, and the native suite passed
again there. The application source and native formatting tests from commit
`5975349` were copied to both the Mac and native source checkouts. The native
test oracle uses explicit left alignment because this libc does not implement
negative dynamic `printf` widths as modern libc does.

Host checks additionally cover all 65,536 signed 16-bit values, 274,630
independent arithmetic-oracle cases, header dependency changes and query
failure/recovery, and 200 randomized renderer frames with focused clipping,
attribute, unchanged-output, allocation, and terminal-restoration cases.
The [test instructions](tests/README) distinguish host and native coverage.

Native interactive checks pass for immediate pending-field display, first-key
help, sorting and idle filtering, invalid input, quitting, Ctrl-C including
an interrupted prompt, and clean unsupported-terminal failure. VT100 sizes
80×24 and 132×36 work; 200×80 is safely capped, a 30×8 window gets a notice,
and live SIGWINCH resizing down and back works. `TERM=xterm` starts without
the former allocation failure. Terminal settings are restored.

A bounded live workload exercises more than 32 occupied slots, stopped and
zombie processes, process creation/exit during sampling, and cleanup. Process
identity/location checks and fresh kernel reads remain enabled throughout.
The older curses implementation's [historical validation](VALIDATION.md)
is preserved separately.
