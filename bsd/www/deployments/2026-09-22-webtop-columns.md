# Webtop CPU, terminal and residency columns — 2026-09-22

Deployed independently compiled native samplers to both `192.168.1.26` and
`192.168.1.29`. CPU% replaces DTIME; TTY and M (core/swapped) are added.
Process rows retain 16-character command names and fit in 79 columns. The
overall CPU summary now brackets the sampling sweeps and reports its actual
interval. The homepage does not require an update for these text columns.

Validation:

- Integer calculation/formatting tests pass on the modern host and both PDPs,
  including maximum 32-bit CPU totals, 100% CPU, invalid intervals, terminal
  abbreviations, and full-width rows.
- Both native compilers build the sampler with `cc -O -i`; fresh uncached
  diagnostic frames pass column, percentage, sort order and width checks.
- Both direct HTTP endpoints serve the new columns. Public HTTPS and the
  existing browser panel show the new `.29` frame and terminal labels.
- Installed samplers are 38,481 bytes, root:wheel, mode 4711. The persistent
  cache locks remain root-owned mode 600. Five-second caching is unchanged.
- The proxy remains in automatic mode with both PDPs healthy. The shared
  visitor counter remains active; no counter files or services were replaced.
- `npm test`, source-bundle generation, shell syntax checks and
  `git diff --check` pass.

The first `.29` installation stopped at `mv`'s mode-4711 overwrite prompt,
leaving the old sampler in service. Retrying with `mv -f` succeeded; the
canonical installer and instructions now use that form for this binary.

On each PDP, `/usr/src/local/webtop-columns-20260922/` is the private staging
and rollback directory. `webtop.before` is the previous native binary and
`webtop.before.c` its source. Copies were also downloaded to the deployment
host. To restore the previous sampler as root:

```sh
cd /usr/src/local/webtop-columns-20260922
cp webtop.before /usr/local/libexec/webtop.new
chown root /usr/local/libexec/webtop.new
chgrp wheel /usr/local/libexec/webtop.new
chmod 4711 /usr/local/libexec/webtop.new
mv -f /usr/local/libexec/webtop.new /usr/local/libexec/webtop
```

Allow the normal frame cache to expire. Do not remove `/tmp/webtop.lock`.
