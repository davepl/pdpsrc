#!/usr/bin/env python3
"""Exercise the actual VCL with a fake origin; never contacts the PDP."""
from pathlib import Path
import re
import subprocess
import tempfile


vcl = (Path(__file__).resolve().parents[1] / "config/varnish.vcl").read_text()
vcl = re.sub(r'probe pdp_health\s*\{[^}]*\}', '', vcl)
# Keep the regression's deterministic single fake server, without probes.
# Actual independent probes and failover are exercised in failover.py.
for name in ('pdp29', 'pdp26', 'visitors'):
    vcl, count = re.subn(r'backend ' + name + r'\s*\{[^}]*\}',
        'backend ' + name + ' { .host = "${s1_addr}"; .port = "${s1_port}"; }', vcl)
    assert count == 1

scenario = r'''
varnishtest "Homepage and public TOP share cache; visitor increments and auth pass"
server s1 {
    rxreq
    expect req.url == "/"
    expect req.http.Cookie == <undef>
    txresp -body "homepage"
    accept
    rxreq
    expect req.url == "/cgi-bin/webtop"
    expect req.http.Cookie == <undef>
    txresp -hdr "Cache-Control: no-store" -hdr "X-Snapshot-Age: 2" -body "shared snapshot"
    accept
    rxreq
    expect req.url == "/cgi-bin/webtop?mode=test"
    expect req.http.Cookie == "pdp11_visit_v1=1"
    txresp -hdr "Cache-Control: no-store" -body "snapshot"
    accept
    rxreq
    expect req.url == "/cgi-bin/visit"
    txresp -hdr "Cache-Control: no-store" -body "counter"
    accept
    rxreq
    expect req.url == "/cgi-bin/visit"
    txresp -hdr "Cache-Control: no-store" -body "next counter"
    accept
    rxreq
    expect req.url == "/visits.txt"
    txresp -body "0000005529\n"
    accept
    rxreq
    expect req.url == "/visits.txt"
    txresp -body "0000005530\n"
    accept
    rxreq
    expect req.url == "/cgi-bin/webtop"
    expect req.http.Authorization == "Bearer test-only"
    txresp -hdr "Cache-Control: private" -body "authenticated snapshot"
    accept
    rxreq
    expect req.url == "/"
    expect req.http.Authorization == "Bearer test-only"
    txresp -hdr "Cache-Control: private" -body "authenticated"
    accept
    rxreq
    expect req.url == "/index.html/other?fbclid=keep"
    expect req.http.Cookie == "other=preserved"
    txresp -hdr "Cache-Control: private" -body "other-path"
} -start
varnish v1 -arg "-p timeout_idle=20" -vcl {
VCL_SOURCE
} -start
client c1 {
    txreq -url "/" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.status == 200
    expect resp.body == "homepage"
    expect resp.http.X-Cache == "MISS"

    txreq -url "/?fbclid=first" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.status == 200
    expect resp.body == "homepage"
    expect resp.http.X-Cache == "HIT"

    txreq -url "/?utm_source=facebook&fbclid=second" -hdr "Host: pdp1173.com" -hdr "Cookie: pdp11_visit_v1=1"
    rxresp
    expect resp.http.X-Cache == "HIT"
    expect resp.body == "homepage"

    txreq -url "/index.html?fbclid=third" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.http.X-Cache == "HIT"
    expect resp.body == "homepage"

    txreq -url "/?" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.http.X-Cache == "HIT"

    txreq -url "/cgi-bin/webtop" -hdr "Host: pdp1173.com" -hdr "Cookie: first=1"
    rxresp
    expect resp.body == "shared snapshot"
    expect resp.http.X-Cache == "MISS"
    expect resp.http.Cache-Control == "no-store"
    expect resp.http.X-Snapshot-Age == "2"

    delay 1.1
    txreq -url "/cgi-bin/webtop" -hdr "Host: pdp1173.com" -hdr "Cookie: other=2" -hdr "Cache-Control: no-cache"
    rxresp
    expect resp.body == "shared snapshot"
    expect resp.http.X-Cache == "HIT"
    expect resp.http.X-Snapshot-Age >= 3
    expect resp.http.Cache-Control == "no-store"

    txreq -url "/cgi-bin/webtop" -hdr "Host: davepl.dyndns.org:80"
    rxresp
    expect resp.body == "shared snapshot"
    expect resp.http.X-Cache == "HIT"

    txreq -url "/?fbclid=alias" -hdr "Host: www.pdp1173.com"
    rxresp
    expect resp.body == "homepage"
    expect resp.http.X-Cache == "HIT"

    txreq -url "/cgi-bin/webtop?mode=test" -hdr "Host: pdp1173.com" -hdr "Cookie: pdp11_visit_v1=1"
    rxresp
    expect resp.body == "snapshot"
    expect resp.http.X-Cache == "PASS"

    txreq -url "/cgi-bin/visit" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.body == "counter"
    expect resp.http.X-Cache == "PASS"

    txreq -url "/cgi-bin/visit" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.body == "next counter"
    expect resp.http.X-Cache == "PASS"

    txreq -url "/visits.txt" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.body == "0000005529\n"
    expect resp.http.X-Cache == "PASS"
    expect resp.http.Cache-Control == "no-store"

    txreq -url "/visits.txt" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.body == "0000005530\n"
    expect resp.http.X-Cache == "PASS"
    expect resp.http.Cache-Control == "no-store"

    txreq -url "/cgi-bin/webtop" -hdr "Host: pdp1173.com" -hdr "Authorization: Bearer test-only"
    rxresp
    expect resp.body == "authenticated snapshot"
    expect resp.http.X-Cache == "PASS"

    txreq -url "/?fbclid=auth" -hdr "Host: pdp1173.com" -hdr "Authorization: Bearer test-only"
    rxresp
    expect resp.body == "authenticated"
    expect resp.http.X-Cache == "PASS"

    txreq -url "/index.html/other?fbclid=keep" -hdr "Host: pdp1173.com" -hdr "Cookie: other=preserved"
    rxresp
    expect resp.body == "other-path"
    expect resp.http.X-Cache == "PASS"

    txreq -req POST -url "/?fbclid=no-post" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.status == 405

    txreq -url "/?fbclid=wrong-host" -hdr "Host: unrelated.invalid"
    rxresp
    expect resp.status == 421
} -run
server s1 -wait
'''
refresh_scenario = r'''
varnishtest "TOP refreshes after five seconds and preserves its last frame on background failure"
server s1 {
    rxreq
    expect req.url == "/cgi-bin/webtop"
    txresp -hdr "Cache-Control: no-store" -hdr "X-Snapshot-Age: 0" -body "first frame"
    accept
    rxreq
    expect req.url == "/cgi-bin/webtop"
    txresp -hdr "Cache-Control: no-store" -hdr "X-Snapshot-Age: 0" -body "second frame"
    accept
    rxreq
    txresp -status 503 -body "origin temporarily unavailable"
} -start
varnish v1 -arg "-p timeout_idle=20" -vcl {
VCL_SOURCE
} -start
client c1 {
    txreq -url "/cgi-bin/webtop" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.body == "first frame"
    delay 5.1
    txreq -url "/cgi-bin/webtop" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.body == "first frame"
    expect resp.http.X-Cache == "STALE"
    expect resp.http.X-Snapshot-Age >= 5
} -run
varnish v1 -expect MAIN.fetch_length == 2
client c2 {
    txreq -url "/cgi-bin/webtop" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.body == "second frame"
    expect resp.http.X-Cache == "HIT"
    delay 5.1
    txreq -url "/cgi-bin/webtop" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.body == "second frame"
    expect resp.http.X-Cache == "STALE"
} -run
server s1 -wait
client c3 {
    txreq -url "/cgi-bin/webtop" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.status == 200
    expect resp.body == "second frame"
    expect resp.http.X-Cache == "STALE"
    expect resp.http.X-Snapshot-Age >= 5
} -run
'''
gary_scenario = r'''
varnishtest "Gary tracking links and session cookies share the static page cache"
server s1 {
    rxreq
    expect req.url == "/pdp-ai.html"
    expect req.http.Cookie == <undef>
    txresp -body "PDP-Gary"
    accept
    rxreq
    expect req.url == "/pdp-ai.html"
    expect req.http.Authorization == "Bearer test-only"
    txresp -hdr "Cache-Control: private" -body "authorized Gary"
    accept
    rxreq
    expect req.url == "/pdp-ai.html/other?fbclid=keep"
    expect req.http.Cookie == "other=preserved"
    txresp -hdr "Cache-Control: private" -body "other-path"
} -start
varnish v1 -arg "-p timeout_idle=20" -vcl {
VCL_SOURCE
} -start
client c1 {
    txreq -url "/pdp-ai.html?fbclid=first" -hdr "Host: pdp1173.com" -hdr "Cookie: pdp_gary_visit_v1=1"
    rxresp
    expect resp.status == 200
    expect resp.body == "PDP-Gary"
    expect resp.http.X-Cache == "MISS"

    txreq -url "/pdp-ai.html" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.body == "PDP-Gary"
    expect resp.http.X-Cache == "HIT"

    txreq -url "/pdp-ai.html?utm_source=facebook&fbclid=second" -hdr "Host: www.pdp1173.com" -hdr "Cookie: pdp_gary_session_v1=another"
    rxresp
    expect resp.body == "PDP-Gary"
    expect resp.http.X-Cache == "HIT"

    txreq -req HEAD -url "/pdp-ai.html?fbclid=head" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.status == 200
    expect resp.http.X-Cache == "HIT"

    txreq -url "/pdp-ai.html?fbclid=auth" -hdr "Host: pdp1173.com" -hdr "Authorization: Bearer test-only"
    rxresp
    expect resp.body == "authorized Gary"
    expect resp.http.X-Cache == "PASS"

    txreq -url "/pdp-ai.html/other?fbclid=keep" -hdr "Host: pdp1173.com" -hdr "Cookie: other=preserved"
    rxresp
    expect resp.body == "other-path"
    expect resp.http.X-Cache == "PASS"
} -run
server s1 -wait
'''
with tempfile.TemporaryDirectory(prefix="webtop-vcl-test-") as directory:
    for name, test in (("tracking", scenario), ("refresh", refresh_scenario), ("gary", gary_scenario)):
        path = Path(directory) / (name + ".vtc")
        path.write_text(test.replace("VCL_SOURCE", vcl))
        result = subprocess.run(["varnishtest", "-v", str(path)], capture_output=True, text=True)
        if result.returncode:
            print(result.stdout + result.stderr)
        result.check_returncode()
print("PASS: homepage and Gary tracking normalization, shared TOP and host aliases, refresh/failure handling, snapshot age, uncached increments and counter reads, CGI queries, authorization, host/method guards")
