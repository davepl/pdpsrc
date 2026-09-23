vcl 4.1;

import std;

probe pdp_health {
    .request = "GET /health.txt HTTP/1.0"
        "Host: pdp1173.com"
        "Connection: close";
    .timeout = 3s;
    .interval = 10s;
    .window = 3;
    .threshold = 2;
    .initial = 0;
}

backend pdp29 {
    .host = "192.168.1.29";
    .port = "80";
    .connect_timeout = 2s;
    .first_byte_timeout = 8s;
    .between_bytes_timeout = 8s;
    .max_connections = 2;
    .probe = pdp_health;
}

backend pdp26 {
    .host = "192.168.1.26";
    .port = "80";
    .connect_timeout = 2s;
    .first_byte_timeout = 8s;
    .between_bytes_timeout = 8s;
    .max_connections = 2;
    .probe = pdp_health;
}

backend visitors {
    .host = "127.0.0.1";
    .port = "6083";
    .connect_timeout = 1s;
    .first_byte_timeout = 6s;
    .between_bytes_timeout = 3s;
    .max_connections = 32;
}

sub vcl_recv {
    if (req.http.host !~ "(?i)^((www\.)?pdp1173\.com|davepl\.dyndns\.org)(:80)?$") {
        return (synth(421, "Unknown host"));
    }
    # All accepted names serve this same public site. Use one cache key so
    # the old DynDNS address cannot multiply TOP polling against the PDP.
    set req.http.host = "pdp1173.com";
    if (req.method != "GET" && req.method != "HEAD") {
        return (synth(405, "Only GET and HEAD are supported"));
    }
    # This one line is the persistent setting managed by pdp-backend.
    set req.http.X-PDP-Mode = "auto";
    unset req.http.X-PDP-Node;
    # The counter is independent of both PDPs. Never retry an increment.
    if (req.url == "/visits.txt" || req.url == "/cgi-bin/visit" ||
        req.url == "/cgi-bin/visit-total") {
        set req.backend_hint = visitors;
        return (pass);
    }
    # Choose once per request, so the TOP cache key describes its origin.
    if (req.http.X-PDP-Mode == "26" ||
        (req.http.X-PDP-Mode == "auto" && !std.healthy(pdp29) && std.healthy(pdp26))) {
        set req.backend_hint = pdp26;
        set req.http.X-PDP-Node = "PDP .26";
    } else {
        set req.backend_hint = pdp29;
        set req.http.X-PDP-Node = "PDP .29";
    }
    # The homepage is static: tracking parameters and browser cookies do not
    # change its contents. Normalize before hashing so Facebook links and
    # returning visitors share the existing / cache object (including grace).
    # CGI URLs, other paths, and the built-in Authorization exclusion remain.
    if (req.url ~ "^/(index[.]html)?([?]|$)") {
        set req.url = "/";
        unset req.http.Cookie;
    }
    # Gary is also static. The native HTTP server treats query strings as
    # filenames, so Facebook links must reach it without tracking parameters.
    # Share the page cache even when the browser has a Gary session cookie.
    if (req.url ~ "^/pdp-ai[.]html([?]|$)") {
        set req.url = "/pdp-ai.html";
        unset req.http.Cookie;
    }
    # This exact endpoint is one public system snapshot, independent of the
    # visitor. Share it across viewers; authenticated requests still pass.
    if (req.url == "/cgi-bin/webtop") {
        unset req.http.Cookie;
    }
    # Fall through to the built-in cookie and authorization exclusions.
}

sub vcl_hash {
    if (req.url == "/cgi-bin/webtop") {
        hash_data(req.http.X-PDP-Node);
    }
    # Built-in hashing still includes the URL and canonical hostname.
}

sub vcl_backend_fetch {
    # Avoid holding a connection open on the small legacy web server.
    set bereq.http.Connection = "close";
}

sub vcl_backend_response {
    if (bereq.backend == visitors) {
        set beresp.http.Cache-Control = "no-store";
        set beresp.uncacheable = true;
        set beresp.ttl = 0s;
        return (deliver);
    }
    if (bereq.backend == pdp29) {
        set beresp.http.X-PDP-Node = "PDP .29";
    } elsif (bereq.backend == pdp26) {
        set beresp.http.X-PDP-Node = "PDP .26";
    }
    # A failed refresh must not replace the last working copy.
    if (bereq.is_bgfetch && beresp.status >= 500) {
        return (abandon);
    }
    if (beresp.status >= 500) {
        call retry_other_pdp;
    }
    if (bereq.uncacheable) {
        return (deliver);
    }
    if (bereq.url == "/cgi-bin/webtop" && beresp.status == 200 &&
        !beresp.http.Set-Cookie) {
        # Cache only in this proxy. Retain no-store for browsers/downstream
        # caches, but skip the built-in rule that would prevent our own cache.
        set beresp.http.Cache-Control = "no-store";
        # A one-request retry must not put .26's frame in .29's cache key.
        if (bereq.http.X-PDP-Node != beresp.http.X-PDP-Node) {
            set beresp.uncacheable = true;
            set beresp.ttl = 0s;
            return (deliver);
        }
        set beresp.ttl = 5s;
        set beresp.grace = 15s;
        set beresp.keep = 0s;
        return (deliver);
    }
    if (beresp.status != 200 ||
        beresp.http.Set-Cookie ||
        beresp.http.Cache-Control ~ "(?i)(private|no-cache|no-store)" ||
        beresp.http.Surrogate-Control ~ "(?i)no-store" ||
        beresp.http.Pragma ~ "(?i)no-?cache" ||
        beresp.http.Vary == "*") {
        set beresp.uncacheable = true;
        set beresp.ttl = 120s;
        return (deliver);
    }
    # The PDP currently sends no freshness headers. Supply a shared-cache TTL
    # while allowing browsers to check for updates on each navigation.
    if (!beresp.http.Cache-Control && !beresp.http.Expires) {
        set beresp.ttl = 5m;
        set beresp.http.Cache-Control = "public, max-age=0, s-maxage=300";
    }
    set beresp.grace = 24h;
    if (beresp.http.Cache-Control ~ "(?i)(must-revalidate|proxy-revalidate)") {
        set beresp.grace = 0s;
    }
    # Built-in VCL still checks explicit zero TTL and other cache restrictions.
}

sub retry_other_pdp {
    if (bereq.http.X-PDP-Mode == "auto" && bereq.retries == 0 &&
        (bereq.method == "GET" || bereq.method == "HEAD") &&
        (bereq.url !~ "^/cgi-bin/" || bereq.url == "/cgi-bin/webtop")) {
        if (bereq.backend == pdp29 && std.healthy(pdp26)) {
            set bereq.backend = pdp26;
            return (retry);
        } elsif (bereq.backend == pdp26 && std.healthy(pdp29)) {
            set bereq.backend = pdp29;
            return (retry);
        }
    }
}

sub vcl_backend_error {
    if (bereq.is_bgfetch) {
        return (abandon);
    }
    call retry_other_pdp;
    set beresp.ttl = 0s;
    set beresp.uncacheable = true;
}

sub vcl_deliver {
    if (req.url == "/cgi-bin/webtop" && resp.status == 200 &&
        !obj.uncacheable) {
        # The browser displays this age; include time spent in Varnish so a
        # grace response can never masquerade as a newly sampled frame.
        set resp.http.X-Snapshot-Age =
            std.integer(resp.http.X-Snapshot-Age, 0) +
            std.integer(duration=obj.age, fallback=0);
    }
    if (obj.uncacheable) {
        set resp.http.X-Cache = "PASS";
    } elsif (obj.hits > 0) {
        if (obj.ttl <= 0s) {
            set resp.http.X-Cache = "STALE";
        } else {
            set resp.http.X-Cache = "HIT";
        }
    } else {
        set resp.http.X-Cache = "MISS";
    }
    set resp.http.X-Cache-Proxy = "pdp-varnish";
}
