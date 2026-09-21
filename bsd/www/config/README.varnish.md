# Public website cache

`varnish.vcl` is the complete Varnish configuration deployed on `caddy`
(`192.168.1.45`). Varnish 7.1 listens on `127.0.0.1:6081`; Caddy forwards
the public `pdp1173.com` site to it. Its origin is `192.168.1.26:80`.
This configuration belongs on the Linux proxy, not on the PDP.

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
Other paths, CGI requests, their query strings, and their cookies retain
their existing behavior. If personalized or query-dependent homepage
content is introduced, this normalization must be revisited.

The origin server itself is unchanged. Query strings on other origin paths
and malformed origin error responses are not fixed by this homepage rule.

## Validate and deploy without losing the cache

Run `python3 tests/varnish.py` on a Linux machine with Varnish and
`varnishtest` installed. It uses an isolated fake origin; it does not contact
the PDP. It tests the actual VCL, including cache hits across tracking URLs
and returning-visitor cookies, CGI cache bypass, authorization, and guards
for unsupported methods and unrelated hosts.

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
Existing restrictions such as private/no-store responses remain enforced.
Grace permits a previously cached homepage to remain available during an
origin outage; it cannot supply uncached pages or fresh CGI results.

## September 20 deployment

Activated `pdp_tracking_20260921` at approximately 2026-09-21 00:54 UTC.
The previous `boot` VCL remains available; its file is backed up at
`/root/pdp-varnish-backups/default.before-tracking-20260921.vcl` on caddy.
The exact reported Facebook URL changed from 503 to 200, using the existing
cached homepage. Plain, tracked-with-cookie, and tracked `/index.html`
responses were byte-identical. The origin's HTTP port was refusing
connections during final verification, so these were cached grace responses.
