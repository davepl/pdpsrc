# PDP proxy, automatic failover and shared counter

Caddy on `192.168.1.45` forwards the three PDP hostnames to Varnish on
`127.0.0.1:6081`. The router's public port 80 must point to `.45`.
The saved VCL prefers **192.168.1.29**, falls back to **192.168.1.26**, and
returns to `.29` when its health checks recover. Caddy's other sites are separate.

## Select a mode

From the Mac, using the existing SSH key:

```
ssh root@192.168.1.45 pdp-backend status
ssh root@192.168.1.45 pdp-backend auto
ssh root@192.168.1.45 pdp-backend 26
ssh root@192.168.1.45 pdp-backend 29
```

`auto` is the normal setting. `26` and `29` force that machine and disable
fallback. Add `--check` to check health without changing the mode. The helper
backs up, validates, warms probes, activates, and atomically saves the VCL;
it rolls back a failed activation. No Caddy/Varnish restart or counter copy
is needed. Homepage/image caches survive. Retired VCLs are discarded to stop
unused probes; previous configuration files remain in `/root/pdp-varnish-backups`.

The tiny static `/health.txt` is fetched once every ten seconds per PDP.
Two successes in the last three probes are required; timeout is three seconds.
Detection/recovery therefore takes roughly 10–25 seconds. Probes test HTTP,
not the sampler. Each origin is limited to two concurrent ordinary backend
connections; probes are additional. Backend connect timeout is two seconds,
and first-byte/between-byte timeouts are eight seconds.

A failed foreground GET/HEAD for static content or the exact snapshot endpoint
may retry once on the other healthy PDP in automatic mode. Other CGI calls,
especially visitor increments, are never retried. This protects against a
failure between probes without blindly repeating state-changing requests.

## Cache and identity

Successful static pages default to five minutes of freshness and 24 hours of
grace. A cached homepage can survive both PDPs being down. Uncached content
cannot. Homepage aliases, Facebook query strings, and cookies normalize to the
same cache key. The static `/pdp-ai.html` page also drops query strings and
cookies before origin lookup and caching. This is required because the native
PDP HTTP server otherwise treats `?fbclid=...` as part of the filename and
returns an error that Varnish reports as 503. Authorization still bypasses
caching; other paths retain their query strings and cookie behavior.

The exact `/cgi-bin/webtop` snapshot is cached for five seconds with 15 seconds
of grace, separately per selected PDP. Its response identifies the actual
backend with `X-PDP-Node`; the TMOG-11 panel displays that identity. A foreground
retry's frame is not stored under the other PDP's key. Failed background
refreshes preserve the last good frame. `X-Snapshot-Age` includes proxy age,
and browsers/downstream caches receive `Cache-Control: no-store`.

## Shared visitor total

`pdp-visitors.service` runs `proxy/visitor-service.py` as the dedicated
`pdp-visitors` user, bound only to `127.0.0.1:6083`. Durable state is
`/var/lib/pdp-visitors/visitors.sqlite` (plus SQLite WAL files while active).
Varnish routes these exact paths directly to it, always uncached:

- `/cgi-bin/visit`: GET increments once; HEAD reads only.
- `/cgi-bin/visit-total` and `/visits.txt`: read only.

Each PDP's native `visit-proxy.c` helpers forward to `.45:80` with the public
Host header, so direct LAN pages share this same total. They do not retry.
The current HTML reads `/cgi-bin/visit-total`; the old native `/visits.txt`
symlink and local totals remain as historical rollback state, not live counters.
Public `/visits.txt` is served by the central service.

Browser session deduplication remains unchanged: refreshes and snapshot polling
do not increment. This is an approximate session count, not unique people or
fraud-resistant analytics. The homepage counter stores no visitor identifiers.
If the proxy/counter fails, neither PDP silently starts a divergent local count.

## PDP-Gary visitors

`pdp-ai.html` has its own total, independent of the homepage. The service adds
`gary_counter` and `sessions` tables to the existing database on first startup;
Gary starts at zero, while the existing homepage total stays unchanged. Both
totals are included in the same online SQLite backup. Never remove/reinitialize
the database during deployment. Invalid existing totals fail closed.

Install `config/pdp-gary-route.caddy` as `/etc/caddy/pdp-gary-route.caddy` and
import it inside the existing PDP hostname block. Preserve the separate private
`pdp-ai-route.caddy` import and its chat credentials. Validate and reload Caddy.
The new import sends `/pdp-visitors/*` directly to the loopback counter service,
bypassing Varnish and the PDPs, and sets PNG types for the renamed Gary icons.

- `GET`/`HEAD /pdp-visitors/stats`: read Gary's total and active count as JSON.
- `POST /pdp-visitors/heartbeat`: a 32-character random hexadecimal `session`
  and boolean `increment`; each token can increment Gary's total only once.
- Visible pages send a heartbeat every 30 seconds. **ONLINE NOW** means distinct
  browser sessions seen on this page within 90 seconds, including readers who
  haven't sent a chat. Hidden/closed pages expire, and multiple tabs share a
  session cookie. Hostnames and cookie-blocked tabs may count separately.

Session cookies and a separate `pdp_gary_visit_v1` marker prevent refreshes or
new tabs from adding visits. sessionStorage is the fallback; if storage is
blocked, only read the counters. Interrupted first visits can be missed, and
simultaneous first loads can race while creating their browser cookie. This is
an approximate session count, not unique people or fraud-resistant analytics.
The service stores only SHA-256 hashes of random session tokens, with no IPs or
chat content; rows inactive for 24 hours are pruned on the next heartbeat.
Retries and restarts reuse these records and do not add another visit.

Direct `.26` and `.29` pages use the canonical HTTPS counter endpoint. Its CORS
allowlist covers these LAN origins and the three public hostnames. Requests and
responses are uncached; failure displays a dash rather than a guessed count.
Tests use private databases: `python3 tests/gary-counter.py` and
`node tests/gary-visitors.js`.

## Restore the Linux service

Install `proxy/visitor-service.py` as root-owned mode 755 at
`/usr/local/libexec/pdp-visitors.py`, the unit at
`/etc/systemd/system/pdp-visitors.service`, and `config/pdp-backend.py` at
`/usr/local/sbin/pdp-backend` (mode 755). Use Python 3 and Varnish 7.1 or later.
Create a system user/group `pdp-visitors`, with no login, and its mode-700
state directory owned by that user. Restore a database backup with mode 600
and the same ownership before starting the service. Then:

```
systemctl daemon-reload
systemctl enable --now pdp-visitors
curl http://127.0.0.1:6083/visits.txt
```

For an actual first migration only, initialize an absent database with the
current authoritative count using `runuser -u pdp-visitors --
/usr/local/libexec/pdp-visitors.py init NUMBER`. Never initialize over existing
state; the tool refuses. To preserve concurrent visits during a migration,
seed from snapshot S, activate counter routing, allow old requests to drain,
read final old total F, then apply the delta once:

```
runuser -u pdp-visitors -- /usr/local/libexec/pdp-visitors.py adjust UNIQUE-MIGRATION-NAME DELTA
```

The adjustment is additive and idempotent, so visits already counted by the
new service are retained. Do not add the two PDPs' historical totals together.
Only install native forwarding CGIs **after** central routing is live, to avoid
a forwarding loop. Native `install.sh` verifies the read route first.

## Preserve and restore runtime state

Take an online consistent SQLite backup on caddy, then copy it off the machine:

```
runuser -u pdp-visitors -- /usr/local/libexec/pdp-visitors.py backup /var/lib/pdp-visitors/backup-YYYYMMDD.sqlite
```

Run backups as the service user so SQLite sidecar files retain its ownership.
Use a new filename; overwriting is refused. For restoration, stop
`pdp-visitors`, archive the existing database and WAL/SHM files together, install
the backup as `visitors.sqlite` with `pdp-visitors` ownership and mode 600, and
restart. Do not leave stale WAL/SHM files beside a restored database. Git stores
code and configuration, not changing visitor totals. Normal host backups must
include the proxy state as well as both PDP disks.

## Validate and activate VCL without emptying caches

On a modern Linux host with Varnish/varnishtest:

```
python3 tests/shared-counter.py
python3 tests/varnish.py
python3 tests/failover.py
```

The tests use local fake origins, never the physical PDPs. They cover cache
behavior, real health probes, failover/recovery, both-down grace, origin-specific
frames, retries, manual modes, and durable/concurrent visitor accounting.

Back up `/etc/varnish/default.vcl`, record `varnishadm vcl.list`, and copy the
candidate under `/etc/varnish` (the service has a private `/tmp`). Load it with a
unique name, wait for `varnishadm backend.list` to show the PDPs healthy, then
activate with `varnishadm vcl.use NAME` and atomically replace the saved file.
Keep the previous VCL until live verification succeeds, then discard inactive
configurations so they do not keep probing. On failure use the old VCL and
restore its file. Avoid a restart that would empty the cache.

When changing homepage content, publish it to both PDPs before banning the
single normalized `/` cache object. Counter paths must never be cached.
