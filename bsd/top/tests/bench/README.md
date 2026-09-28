# Measuring a process monitor on a PDP-11

`topbench.c` runs an unchanged `top` executable in a native pseudo-terminal
and measures it from a separate parent process. Compile and run the observer
on the **same 2.11BSD machine** as every program being compared:

```sh
make
TOPBENCH_REFRESHES=3000 ./topbench 300 /usr/ucb/top -s1
TOPBENCH_REFRESHES=3000 ./topbench 300 /path/to/new-top -s1
```

Run as root: the observer reads the kernel process table through `/dev/kmem`,
and the monitors need access to kernel statistics. Nothing is installed
or changed in the system configuration. The observer opens a free PTY, starts
the selected executable, drains its output locally, sends `q` when the fixed
request count or timed duration finishes, and reaps it. A child that exceeds
the deadline gets five seconds before termination. Ctrl-C also terminates
the child. The duration/deadline is 3 to 600 seconds; top arguments following
the executable are passed through unchanged. A saved earlier AI build can
be included as a third candidate using the same protocol.

The terminal starts with ordinary login settings, including newline and tab
handling. The tested program selects its own raw or cbreak mode. All runs use
`TERM=vt100`, an 80 by 24 window, and the same small environment. There is no
Telnet transfer or remote terminal rendering in the timed path. Other terminal
sizes and interactive features belong in separate functional tests.

## What the numbers mean

| Output field | Meaning |
| --- | --- |
| `startup_us` | Time from immediately before `fork` to the last byte of the first recognized statistics display. |
| `wall_us` | Total elapsed time through child termination. |
| `user_us`, `system_us` | Child CPU time returned by `wait4`, including startup and exit. |
| `rendered_frames` | Recognized visible output bursts; not an exact internal sampling count. |
| `data_bytes`, `stack_bytes` | Largest observed allocated data and stack segments after the first screen. |
| `data_stack_bytes` | Largest observed simultaneous sum of those two allocations. |
| `output_bytes` | All bytes drained from the PTY, including setup, padding, and exit sequences. |
| `maxrss_raw` | Diagnostic native resource field; do not interpret it as measured physical memory. |
| `status` | Native packed wait status. Zero means successful exit; any other value needs investigation. |
| `forced_requested`, `forced_refreshes` | Requested and successfully queued refresh keys in fixed-request mode; zero in timed mode. Successful completion requires equality and normal child exit. |
| `forced_us` | Time from queuing the first forced refresh through exit; includes observer pacing and exit. |
| `deadline_hit`, `observer_failed` | Nonzero means the run failed and must not enter a successful-results summary. |

PDP-11 process sizes are measured in 64-byte clicks. Allocated **data plus
stack** is especially useful here because the process has a 64 KiB data
address space even when the whole system has free RAM and swap. These sizes
include static data, BSS, heap, and stack allocations; they exclude shared
instruction text and are not physical RSS. Instruction size is reported
separately by the native `size` utility.

The observer polls process sizes approximately every 50 milliseconds once the
first screen appears. It can miss a temporary allocation between observations,
so report **maximum observed allocation**, not an exact peak. Data and stack
can reach their individual maxima at different times; use the simultaneous
`data_stack_bytes` field when comparing the combined footprint.

CPU is charged to the child by the kernel, so the observer's polling and
terminal draining are excluded from `user_us` and `system_us`. They still
affect the system workload seen by a live process monitor. Use the same
observer and workload for every candidate. CPU accounting is clock-tick
quantized: differences of a tick or two in a single short run are not strong
evidence. Repeat runs and use enough updates to accumulate useful CPU time.

## Recognizing a displayed frame

Both monitors print a `COMMAND` column heading. The observer waits for that
heading, then recognizes the end of the output burst after 50 milliseconds
with no further output. It records the timestamp of the **last byte**, rather
than adding the 50 millisecond confirmation delay to the startup result. The
tested refresh intervals must be at least 100 milliseconds. This method does
not depend on a particular final cursor position; old curses builds differ
in the cursor sequences they emit.

Validate the first display for each executable by capturing its initial
terminal output. Under `/bin/sh` or ksh:

```sh
TOPBENCH_CAPTURE=reference.raw ./topbench 3 /usr/ucb/top -s1
```

The first 4096 bytes are buffered in the observer's memory and written only
after the child exits, outside the timed interval. Examine them with a VT100
decoder or a terminal-stream inspection tool. Native termcap padding includes
NUL bytes; their presence is normal.

A curses refresh with no visible change may emit no output, and unusually
fragmented output may defeat a quiet-gap detector. Therefore `rendered_frames`
counts visible bursts, not guaranteed internal samples. Prefer total CPU over
a fixed number of requested updates for the primary comparison. A CPU-per-render ratio
can be informative, but includes startup/exit and must retain these caveats.

## Fixed-request mode for costs below clock resolution

An efficient monitor on an idle system may accumulate only a few CPU ticks
even in a 15 second run. Repeating those short tests cannot turn a quantized
tie into convincing evidence. Instead, request the same large number of
updates from each program:

```sh
TOPBENCH_REFRESHES=3000 ./topbench 300 /usr/ucb/top -s1
TOPBENCH_REFRESHES=3000 ./topbench 300 /path/to/new-top -s1
```

Warm each executable once, then collect **five interleaved trials of 3000
requests per executable**, reversing which one runs first between rounds.
Do not include warmups in the reported five trials. If 3000 requests still
produce too few CPU ticks on a different host, increase the count equally
for all candidates and repeat the whole set.

In this mode the seconds argument is a **deadline**, not a requested runtime.
The observer waits for the first normal screen, then sends space characters,
which every inspected implementation treats as refresh requests. It checks
`FIONREAD` on the slave PTY and sends another key only when the input queue is
empty. At most one unread character is present, avoiding the old terminal
driver's small input buffer limit. Once all requested spaces have been read,
it queues `q`.

The programs' inspected loops read one key, update the statistics and display,
then read the next key. Successful exit on `q` therefore proves that the last
space's update finished, even when curses had no changed pixels to write.
Validate that key-loop behavior before extending this harness to a different
monitor. `forced_refreshes` counts successful input writes; accept a run only
if it equals `forced_requested`, child status is zero, and both failure flags
are zero. The requested count may be 1 through 100000.

Compare **total child CPU for the same number of forced updates**, including
the small common startup and exit cost. CPU divided by the request count is
an amortized cost, not a precise measurement of one call. Initial automatic
sampling is still present; unusually long scheduling stalls could permit
additional timer-driven updates. Normal `-s1` trials should have none between
closely queued requests, but the harness does not claim to count those
internal timer events. Large request counts dilute the initial sample cost.

The parent's short polling timeout paces requests; `forced_us` is therefore
useful diagnostic throughput, not an isolated computation-time benchmark.
Use the kernel's child CPU totals for the primary computation comparison.
Quiet-gap render counts during a dense forced stream are deliberately not
used as the denominator. Timed mode remains the ordinary interactive-load
comparison, and default startup remains a separate measurement.

## A reproducible comparison

1. Save the existing executables before rebuilding. Record hashes, compiler
   flags, native `size` output, host patch level, kernel clock rate, and the
   command line used for every candidate.
2. Compare **stripped copies** for installed file size. The system top is
   stripped by its installation rule, while the earlier AI build was not.
   Comparing their unadjusted file lengths includes different symbol-table
   overhead. Preserve the originals and run `strip` only on copies.
3. Run each program once to warm ordinary file caches. Discard those warmups.
   The observer also resolves kernel symbols before timing. Label subsequent
   results as warm-cache measurements; this is not a disk cold-start test.
4. Run five interleaved fixed-request trials per program with
   `TOPBENCH_REFRESHES=3000` and `-s1`. Reverse candidate order between rounds
   to reduce the effect of background changes. Keep the same terminal size,
   process workload, and privileges. Check that every successful trial reports
   exactly 3000 requests and no failure flags. Retain every raw result,
   including failed trials, and explain any exclusions. Inspect all five
   measurements as well as their median: if differences overlap or depend on
   run order, increase the measurement length or report the result as
   inconclusive rather than selecting a favorable trial.
5. Measure default startup separately with five interleaved three-second
   timed runs of each program, without `TOPBENCH_REFRESHES`:

   ```sh
   ./topbench 3 /usr/ucb/top -s1
   ./topbench 3 /path/to/new-top -s1
   ```

   Use `startup_us` from these runs, not their highly quantized CPU totals.
   The two earlier monitors wait for the
   configured interval before showing a full screen. An immediate initial
   display shows totals while interval CPU values are still pending; it
   improves responsiveness but is also a sampling-policy change.
   Report that distinction rather than attributing the whole startup gain
   to faster computation. The `-s1` option fixes the same normal interval
   explicitly instead of depending on a program's default.
6. Verify functionality independently. Omitting statistics, dropping processes,
   or rendering fewer rows is less work and is not an equivalent optimization.

For realistic steady operation, add longer default `-s1` runs. Total CPU from
any lifetime run includes startup and exit; do not label it pure steady-state
CPU. Longer runs reduce that contribution. Differences between long and short
runs can estimate ongoing cost, but they still need repetitions because the
system workload and tick accounting fluctuate.

On a modern host, summarize captured measurements with:

```sh
python3 summarize.py forced-3000.log default-startup.log > summary.csv
```

Keep each interval and workload in its own input file. The script groups by
input file, executable path, and forced request count. It reports startup
minimum/median/maximum, median CPU, observed memory maximum, and visible
render counts. Fixed-request groups use CPU per forced request instead of
CPU per visible render. It flags
failed measurements on stderr and exits unsuccessfully if any were excluded.
Keep raw logs next to any published summary so others can check the analysis.

The system reference in this comparison is installed as `/usr/ucb/top`; its
source credits Digby Tarvin and later contributors. The directory name alone
does not establish UCB authorship.
