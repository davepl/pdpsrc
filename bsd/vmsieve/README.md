# vmsieve on 2.11BSD

Build and run one pass through 50 million integers with the system compiler and
`libvmf`:

```sh
make
./vmsieve -1 -l 50000000
```

The expected count is 3,001,134. The options, validator and timing follow
`sieve`. Each timed pass opens fresh backing files, fills and reads the
bitmap, then closes the files. Run `make test` to check page and file
boundaries, repeated passes and the full 50-million limit.

Odd integer `n` occupies bit `(n >> 1) & 7` at flat byte offset `n >> 4`.
At 50 million the bitmap needs 3,125,000 bytes. The stock 2.11BSD library
provides 512 pages of 1 KiB per virtual space, so this run opens six
temporary spaces in `/tmp`. All of them share an eight-page RAM cache.
Allow just over 3 MiB of free space in `/tmp` for a full run.

The marking code pins page zero to look up sieving primes. It sweeps the
same bitmap for each batch of up to 128 primes, maps each page with
`vmmapseg`, and marks changes with `VMMODIFY`. After all batches finish,
a second traversal reads the completed bitmap to count or print primes.
Every odd integer keeps the same bit address throughout the pass.
