# Public website cache

`varnish.vcl` is the complete Varnish configuration deployed on `caddy`
(`192.168.1.45`). Varnish 7.1 listens on `127.0.0.1:6081`; Caddy forwards
the public `pdp1173.com` site to it. Its origin is `192.168.1.26:80`.
This configuration belongs on the Linux proxy, not on the PDP.

The router's public TCP port 80 must forward to **192.168.1.45:80**, so
visitors cannot bypass the proxy by reaching `.26` directly. The operator
corrected that rule on the UDM Pro during the September 20 diagnosis.
Merge `Caddyfile.pdp` into caddy's existing configuration to route
`pdp1173.com`, `www.pdp1173.com`, and the legacy `davepl.dyndns.org` hostname
through Varnish. Accepted hostnames share a canonical cache key, so their TOP
requests share the same snapshot. Do not replace caddy's unrelated site blocks.

## Homepage query strings and cookies

The PDP's preserved HTTP server includes a request's query string in its
filesystem lookup. Requests such as `/?fbclid=...` therefore fail, and its
incomplete HTTP error headers cause Varnish to report `HTC eof` and return
503. Tracking values also create distinct cache keys unless normalized.

Before hashing, Varnish maps `/`, `/?...`, `/index.html`, and
`/index.html?...` to `/`. The homepage is static and uses no query parameters
or server-side personalization, so all its query parameters and request
cookies are ignored. Cookies remain in the browser: client-side visitor
deduplication still works. Authorization retains the built-in cache bypass.
Other paths and CGI query strings retain their existing behavior, with the
exact public TOP endpoint treated separately below. If personalized or query-dependent homepage
content is introduced, this normalization must be revisited.

The origin server itself is unchanged. Query strings on other origin paths
and malformed origin error responses are not fixed by this homepage rule.

## Shared public TOP snapshot

The exact `/cgi-bin/webtop` endpoint is the same public system snapshot for
every viewer. Request cookies are ignored for this endpoint, and successful
responses without Set-Cookie are cached **inside Varnish for five seconds**.
Authorization still bypasses the cache. CGI URLs with query strings retain
their previous bypass behavior.

The origin's `Cache-Control: no-store` remains on the delivered response, so
browsers and downstream caches do not add another caching layer. This endpoint
is an explicit exception to Varnish's ordinary no-store handling. Its grace
period is only 15 seconds, and failed background refreshes cannot replace the
last working frame. Varnish adds its object age to `X-Snapshot-Age`, allowing
the existing browser display to show the actual age of a cached frame.

`/cgi-bin/visit` is never included in this rule: increments remain uncached.
This shared cache bounds normal TOP fetches to roughly one per five seconds
per host/cache key, instead of one per viewer. It complements the explicit
inetd startup rate documented in [`README.inetd.md`](README.inetd.md).

## Validate and deploy without losing the cache

Run `python3 tests/varnish.py` on a Linux machine with Varnish and
`varnishtest` installed. It uses an isolated fake origin; it does not contact
the PDP. It tests the actual VCL, including cache hits across tracking URLs
and returning-visitor cookies, shared TOP responses, snapshot age, expiry,
failed refreshes, uncached counter increments, CGI query bypass, authorization,
and guards for unsupported methods and unrelated hosts.

Back up `/etc/varnish/default.vcl` and record `varnishadm vcl.list` first.
Copy the candidate into `/etc/varnish/default.vcl.candidate`, then run:

```
varnishadm vcl.load pdp_candidate /etc/varnish/default.vcl.candidate
varnishadm vcl.use pdp_candidate
mv /etc/varnish/default.vcl.candidate /etc/varnish/default.vcl
```

Use a new VCL name for each deployment. Loading validates the candidate
before activation. Keep the previous VCL available for rollback and restore
its configuration file if reverting. This procedure preserves cached pages;
restarting Varnish would empty the in-memory cache. The service has
`PrivateTmp=true`, so a candidate in an ordinary SSH session's `/tmp` is not
visible to its compiler. Place the candidate under `/etc/varnish` instead.

Successful static responses default to a five-minute TTL with 24-hour grace.
Existing restrictions such as private/no-store responses remain enforced
except for the explicit public TOP rule above.
Grace permits a previously cached homepage to remain available during an
origin outage; it cannot supply uncached pages or fresh CGI results. TOP may
briefly retain its last frame, with its advancing age visible to the viewer.

## September 20 deployment

Activated `pdp_tracking_20260921` at approximately 2026-09-21 00:54 UTC.
The previous `boot` VCL remains available; its file is backed up at
`/root/pdp-varnish-backups/default.before-tracking-20260921.vcl` on caddy.
The exact reported Facebook URL changed from 503 to 200, using the existing
cached homepage. Plain, tracked-with-cookie, and tracked `/index.html`
responses were byte-identical. The origin's HTTP port was refusing
connections during final verification, so these were cached grace responses.

## TOP traffic reduction (September 20, later that evening)

Activated `pdp_top_20260921` at approximately 2026-09-21 01:17 UTC, without
restarting Varnish or evicting the homepage. The previous VCL is
`pdp_tracking_20260921`; its file is preserved at
`/root/pdp-varnish-backups/default.before-top-20260921.vcl` on caddy.
Only the exact TOP cache entry was invalidated to remove the earlier
uncacheable response marker. Both fake-origin test scenarios passed on the
deployed Varnish 7.1.1 runtime, including expiry and failed background refresh.

The subsequent `pdp_alias_20260921` VCL is the active version, adding the
DynDNS alias and a shared hostname cache key. Caddy's PDP site block includes
that alias too. Rollback files on caddy are
`/root/pdp-varnish-backups/default.before-alias-20260921.vcl` and
`/root/pdp-varnish-backups/Caddyfile.before-pdp-alias-20260921`.
Both public hostname homepages were verified byte-for-byte after activation.
At that point the PDP was still intermittently unreachable on the LAN; see
`VERIFICATION.md` for the distinction between cached availability and live TOP.
