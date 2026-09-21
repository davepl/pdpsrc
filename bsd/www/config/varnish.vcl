vcl 4.1;

backend pdp {
    .host = "192.168.1.26";
    .port = "80";
    .connect_timeout = 5s;
    .first_byte_timeout = 30s;
    .between_bytes_timeout = 30s;
    .max_connections = 2;
}

sub vcl_recv {
    if (req.http.host !~ "(?i)^(www\.)?pdp1173\.com(:80)?$") {
        return (synth(421, "Unknown host"));
    }
    if (req.method != "GET" && req.method != "HEAD") {
        return (synth(405, "Only GET and HEAD are supported"));
    }
    # The homepage is static: tracking parameters and browser cookies do not
    # change its contents. Normalize before hashing so Facebook links and
    # returning visitors share the existing / cache object (including grace).
    # CGI URLs, other paths, and the built-in Authorization exclusion remain.
    if (req.url ~ "^/(index[.]html)?([?]|$)") {
        set req.url = "/";
        unset req.http.Cookie;
    }
    # Fall through to the built-in cookie and authorization exclusions.
}

sub vcl_backend_fetch {
    # Avoid holding a connection open on the small legacy web server.
    set bereq.http.Connection = "close";
}

sub vcl_backend_response {
    # A failed refresh must not replace the last working copy.
    if (bereq.is_bgfetch && beresp.status >= 500) {
        return (abandon);
    }
    if (bereq.uncacheable) {
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

sub vcl_backend_error {
    if (bereq.is_bgfetch) {
        return (abandon);
    }
    set beresp.ttl = 0s;
    set beresp.uncacheable = true;
}

sub vcl_deliver {
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
