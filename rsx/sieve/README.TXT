SIEVE - Sieve of Eratosthenes benchmark for RSX-11M-PLUS
=========================================================

A MACRO-11 port of Davepl's sieve.c v2.03 (the 2.11BSD integer-only version):
same algorithm and same report. It sieves the odd numbers below 500000,
repeats passes for at least 5 seconds, then prints the total time, the
number of passes, the time per pass, the prime count and whether the count
is right (41538 = PASS). Times come from the system clock (GTIM$).

Files
  SIEVE.TSK   the task image, ready to run (67 blocks)
  SIEVE.MAC   the source; assemble it yourself if you prefer
  SHA256SUMS  checksums of both

Built on RSX-11M-PLUS V4.6 (BL87) with MACRO-11 and TKB, linked only against
SYSLIB. Needs a CPU with EIS (MUL, DIV, ASH, SOB): 11/23, 11/24, 11/34,
11/44, 11/53, 11/73, 11/83 and the like.

Running the task image
  Copy SIEVE.TSK to the RSX system as a binary file (for example with
  KERMIT-11 in binary mode). A task image must be contiguous, so make a
  contiguous copy before running it:
      PIP SIEVE.TSK/CO=SIEVE.TSK
      RUN SIEVE

Building from source instead
      MAC SIEVE,SIEVE/-SP=SIEVE
      TKB SIEVE,SIEVE/-SP=SIEVE
      RUN SIEVE

Sample output (emulated 11/73, QBone)
  TOTAL TIME TAKEN      : 5.52 SECONDS
  NUMBER OF PASSES      : 6
  TIME PER PASS         : 0.920 SECONDS
  COUNT OF PRIMES FOUND : 41538
  PRIME VALIDATOR       : PASS
