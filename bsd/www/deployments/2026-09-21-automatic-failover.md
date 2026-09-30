# Automatic failover and TMOG-11 — September 21, 2026

The public proxy now prefers PDP `192.168.1.29`, falls back to `.26`, and
returns automatically to `.29`. Both have a tiny `/health.txt` endpoint.
The visible TOP/Webtop labels are now **TMOG-11**. The live panel identifies
the PDP that supplied the frame. Its endpoint remains `/cgi-bin/webtop`.

The counter is now one persistent SQLite total on caddy (`.45`), independent
of routing. Both PDPs use native forwarding helpers for direct LAN visits.
Old native counter files remain unchanged for rollback; do not sum them.
The authoritative seed was taken from the active `.29` counter, then an
idempotent additive adjustment preserved visits during cutover.

Before-change runtime backups of both PDPs and the proxy configuration were
taken. Private copies and deployment logs are under the task's
`work/automatic-failover-20260921` directory; proxy copies are under
`/root/pdp-varnish-backups/before-auto-20260921`. Do not commit runtime state.

Persistent manual controls on caddy: `pdp-backend auto`, `26`, `29`, `status`.
See `config/README.varnish.md` for service setup, restoration, counter backups,
health timing, and cache behavior. Both PDP source trees are installed at
`/usr/src/local/webtop` from this repository's complete source bundle.

Validation: HTML/minification and refresh deduplication tests; concurrent,
durable shared-counter tests; isolated real-Varnish tests for health probes,
failover, failback, cache identity, bounded retries, both-down grace, and manual
modes. Physical machines are verified with serial requests, not load tests.

Live verification completed: both PDP homepages and public HTTPS were byte-for-
byte identical to the 15,484-byte generated page. Both native read helpers
returned the central total. A reversible Varnish health override demonstrated
.29 → .26 → .29 with the correct frame identities. Manual .26 and auto modes
were exercised; final active/saved mode is auto, with both probes healthy.
The final VCL is `pdp_auto_20260921T200836Z_fe4a94`. The browser displayed
TMOG-11, the serving PDP, and the shared count. Before/after native runtime
backups and an online SQLite backup have been copied off the machines.
