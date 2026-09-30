# Public site switched to 192.168.1.29

On September 21, 2026 at approximately 19:48 UTC, the public PDP website was
switched from `.26` to `.29`. The route remains Cloudflare (where applicable),
Caddy on `192.168.1.45`, local Varnish, then the selected PDP on port 80.

The installed `/usr/local/sbin/pdp-backend` helper selects either PDP and saves
the selection across restarts. See `../config/README.varnish.md` for commands
and restoration. Switching back from the Mac is:

```
ssh root@192.168.1.45 pdp-backend 26
```

Before cutover, `.29`'s counter was set from 1 to 5,529 under an exclusive lock
on its existing visitor lock file. The replacement was written and synced,
assigned the previous total's ownership, then renamed atomically. The old total
is preserved on that PDP as `/home/www-visits/total.before-5529`. The temporary
reset program was removed after successful execution. Subsequent public visits
advanced the count normally; it was 5,535 during verification.

The active VCL at completion was `pdp_29_20260921T194805Z_5c5ee2`. Its preceding
configuration is stored on caddy at:

`/root/pdp-varnish-backups/pdp_29_20260921T194805Z_5c5ee2.before.vcl`

The original pre-change VCL is also preserved as
`/root/pdp-varnish-backups/default.before-switch-command-20260921.vcl`.
Only the explanatory PDP comment in the Caddyfile changed; unrelated site
routes and the router's forwarding configuration were retained.

Verification covered healthy application endpoints on both PDPs; VCL tests
with an isolated fake origin, including uncached consecutive counter reads;
the active and saved origin both being `.29`; and successful public homepage,
TOP, counter, and legacy DynDNS requests. Public and direct counter reads
matched at 5,535, with the public read marked `X-Cache: PASS` and
`Cache-Control: no-store`. Public TOP showed the target's current uptime.

The post-cutover native runtime backup is
`/usr/src/local/webtop/backups/Sat_Sep_19_23_14_35_EDT_2026-729/runtime.tar`.
That name reflects the PDP's clock. Private off-machine copies, active VCL,
HTTP verification, and the one-time counter reset source are retained under
`work/pdp-cutover-20260921` in the Codex workspace, outside the source repository.
