# Optimization experiments after PR13

These are selection notes for the native 2.11BSD build on minerva, not a
replacement for the final comparison with `/usr/ucb/top`. After five native
pilot rounds, `top-best3.c` remains selected for final validation. Neither
extra arithmetic shortcuts nor further cursor register hints established
a consistent gain worth retaining.
The completed final comparison, including ordinary refreshes, is recorded in
[PERFORMANCE.md](../../../../../PERFORMANCE.md).

## What the code changes do

| Change carried into the candidate | Reason and preserved behavior |
| --- | --- |
| Return early for an identical screen row | The renderer already owns the previous sanitized text. Exact equality with the new text and matching attributes proves that copying, sanitizing, and comparing it again cannot change the display. Truncated or unsanitized input still uses the bounded normal path. A 50-byte length array also avoids rescanning old rows. |
| Finish decimal conversion with 16-bit arithmetic | The native machine has 16-bit words. Once a 32-bit integer has been reduced below 65,536, the remaining decimal digits can use word-sized division. This is a change of arithmetic width, not a cap on displayed values. |
| Explicit `register` locals | Inspection of this historical compiler showed that selected word-sized pointers and loop counters need explicit register declarations. The choice is specific to this compiler; it is not advice to add `register` to modern C indiscriminately. Native timing remains necessary because using registers also leaves fewer for temporary values. |
| Typed process-row formatting | A process row always has the same fields. Small helpers append numbers, text, and padding to a bounded line instead of repeatedly interpreting two `printf` formats. Widths remain next to the corresponding values in `draw()`. Oversized values remain visible, username precision stays eight characters, and one byte is always reserved for the terminating NUL. |
| Cache a completely formatted header | Every frame still reads the time, user count, and all three load averages. Only formatting is reused when every changing input, the load-query result, and the terminal width match. Hostname and boot time are fixed during setup. Failure, recovery, resize, login, or a clock change invalidates the relevant key. This helps repeated requests within one second more than ordinary one-second updates. |

No selected change skips process sampling or removes the final
identity/location checks. The renderer's cache concerns output text,
not stale process data.

## What the native pilots actually measured

All numbers below are **total child user plus system CPU seconds**, including
startup and exit. Each row has three trials. Rounds 1 and 2 request 1,000
space-key refreshes; rounds 3 through 5 request 3,000. All recorded trials completed
their request count with zero child status and no observer/deadline failure.
Use comparisons within a round: do not treat different request counts or
different live-system conditions as a single sample population.

| Round | Candidate | Median CPU seconds | Full range |
| --- | --- | ---: | ---: |
| 1 | Frozen PR13 | 6.950 | 5.783–7.067 |
| 1 | Identical-row fast path | 4.467 | 3.900–4.933 |
| 1 | 16-bit decimal tail | 6.350 | 6.133–6.367 |
| 1 | Both preceding changes | 3.717 | 3.467–3.950 |
| 1 | Multiple dirty spans | 6.817 | 6.717–6.900 |
| 2 | Combined round-1 candidate | 3.917 | 3.433–4.450 |
| 2 | Load-string cache | 3.633 | 3.567–4.467 |
| 2 | Typed process rows | 3.217 | 3.183–3.267 |
| 2 | Smaller row structure | 3.633 | 3.333–4.150 |
| 2 | Explicit register hints | 3.350 | 3.317–3.417 |
| 2 | Bulk utmp records | 3.550 | 3.500–4.017 |
| 3 | Frozen register candidate | 10.550 | 9.500–10.833 |
| 3 | Typed rows plus registers (`best2`) | 9.200 | 9.067–9.267 |
| 3 | Register candidate plus header cache | 9.633 | 9.583–9.700 |
| 4 | Typed rows plus registers (`best2`) | 9.150 | 9.100–9.533 |
| 4 | Preceding combination plus header cache (`best3`) | 8.617 | 8.383–8.650 |
| 4 | Preceding combination plus final arithmetic shortcuts (`best5`) | 8.650 | 8.600–8.950 |
| 5 | Frozen selected build (`best3`) | 8.767 | 8.433–22.033 |
| 5 | Further cursor register hints | 11.883 | 8.567–16.017 |

Sources: [round 1](iteration1.log), [round 2](iteration2.log),
[round 3](iteration3.log), [round 4](iteration4.log),
[round 5](iteration5.log). The paired order reverses in the middle trial.
The reference candidates themselves varied, so a favorable median alone
does not prove an individual small optimization helped.

The identical-row path was below every PR13 result in round 1. Typed rows
were below every combined result in round 2; the typed-row/register
combination was below every register-only result in round 3. Those are
useful selection signals. The initial header-cache pilot improved two
paired trials and lost one. Once combined with typed rows, `best3` beat
`best2` in every round-4 pair, with disjoint ranges. Benefits of separate
changes must not be added together as percentages.

## Experiments not carried forward separately

- **Further cursor register hints:** the three cursor-minus-control CPU
  differences were −6.016667, +3.450009, and −0.200004 seconds. The first
  control was unusually slow at 22.033354 seconds, versus 8.433342 and
  8.766682 later; no cause was established and no result was discarded.
  The cursor variant's median was higher, but it was not slower in every
  pair. Both binaries were 34,594 bytes and used 34,304 observed bytes of
  data plus stack. The mixed timing and absence of a size or memory gain
  do not justify changing the selected build.
- **Final arithmetic shortcuts:** `best4` shifts fractional rounding for
  power-of-two scales, and `best5` also bypasses that arithmetic for a zero
  remainder in the generic branch. These are correct identities and passed
  the formatter tests. Nevertheless, `best5` versus `best3` was one CPU tick
  faster, approximately 34 ticks slower, and effectively tied across the
  three round-4 pairs. It also added 64 instruction bytes without reducing
  observed allocation. Keep the smaller implementation: the experiment did
  not demonstrate a consistent practical benefit. This does not prove
  either shortcut is slower on every machine or workload.
- **Multiple dirty spans:** skipping unchanged gaps can reduce output, but
  requires extra scans and termcap cursor-cost calculations. Its round-1
  CPU range overlapped the reference and it lost one paired comparison.
  The build added 320 instruction bytes. This was insufficient evidence to
  justify the extra renderer logic; it does not establish that the idea is
  useless on every terminal or workload.
- **Load-string-only cache:** two paired gains and one large regression in
  round 2 made the median inconclusive. The later complete-header candidate
  covers the same formatting reuse with one complete dependency key, so
  there is no reason to stack both caches.
- **Smaller row structure:** removing a temporary start-time field saved
  structure payload, but measured data plus stack rose from 34,368 to
  35,328 bytes in all three round-2 trials. Data allocation rose by 1,024
  bytes while stack fell by 64. CPU also lost two paired comparisons. The
  allocator/layout cause was not established; a theoretical payload saving
  is not evidence of a smaller running program.
- **Bulk utmp reads:** fetching 16 complete records per `fread` reduces libc
  call count, not necessarily kernel I/O. It introduces a larger automatic
  array and had inconsistent paired CPU results. The current implementation
  retains its stream, rewinds it, and reads fresh records every frame.

## Pilot scope and native test corrections

These were exploratory selection runs. Some log retrievals added a brief
FTP session during the pilots; the completed final forced/startup blocks
were left undisturbed by further remote checks. The first ordinary-refresh
run was deliberately interrupted before producing any accepted record so
that the cursor variant could be tried. The complete ordinary comparison
was then restarted with the unchanged selected binary. The fresh final
measurements, not a favorable pilot, support the published comparison with
the system monitor.

The retained build passed native correctness checks after two test-portability
fixes. The old compiler requires initialized test arrays to be static, and
its printf ignores a negative dynamic width. The oracle now uses an explicit
left-alignment flag with positive width. That matches the existing literal
process-row layout; the new formatter already produced the intended text.
The original failures remain visible in the build logs. Native runners also
keep an explicit failure status because this shell resets $? before its
cleanup trap; a failing compile can no longer appear successful.

## Correctness and readability review

The selected code retains a readable path from a field to its displayed
width. Arithmetic shortcuts state their numeric reason beside the code.
`format.h` explains left/right padding and the reserved NUL byte; the
renderer explains why equality is safe before it bypasses sanitization.
No opaque record packing, unbounded output builder, or sample throttling
was introduced.

Integration added a short contract for `fixed()`: it accepts one or two fractional digits and a positive scale,
and callers provide enough output space. Production scales are 10, 16,
1024, or the positive native 16-bit clock frequency. The generic expression
is safe for those bounds; it is not a promise to accept arbitrary 32-bit
divisors without overflow. The helper intentionally remains small and
private to these callers.

Host tests cover every signed 16-bit integer in the typed helper, bounded
output and padding, 128 complete rows against the former `printf` layout,
header cache dependencies and failed/recovered queries, and formatter golden
cases. The shift variant additionally passed 274,630 comparisons against an
independent 64-bit rounding oracle. Host arithmetic tests supplement native
compiler/library tests; they do not establish native execution speed or
prove the old compiler generates correct code. The final report preserves
the selected build's native results as well.

## Final validation protocol and stopping rule

The following protocol guided selection; the linked final report records its
completed measurements and applies these limits.

Freeze the selected executable and record its source/build identity before
collecting a fresh final set. The measurements are five interleaved,
order-reversed pairs of 3,000 forced requests, five separate startup pairs,
and at least three longer paired ordinary `-s1` runs. Keep each workload in
its own results file and retain all runs. If ordinary timed CPU differences
remain only a few 60 Hz accounting ticks or reverse with run order, extend
all durations equally or report the comparison as inconclusive.

Forced bursts legitimately benefit from unchanged formatted values. They
do not establish the same percentage gain at one-second refresh intervals,
where the header clock changes every frame. Report regular timed runs as
total child CPU for the same duration; visible output bursts are not an
exact count of internal samples. The observer itself influences the live
workload even though its CPU is excluded from the child's accounting.

Report medians, complete ranges, paired differences, and maximum observed
simultaneous data-plus-stack allocation. This is warm-cache live-machine
evidence, not exact RSS, a guaranteed instantaneous memory peak, or a
general statistical confidence interval for all PDP-11 workloads.

Stop when the remaining simple candidates fail repeated matched comparisons,
or their measured gain is too small to justify added code or memory. That
is evidence of diminishing returns among the changes tried, not a claim
that no faster implementation is possible.
