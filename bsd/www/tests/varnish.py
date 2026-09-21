#!/usr/bin/env python3
"""Exercise the actual VCL with a fake origin; never contacts the PDP."""
from pathlib import Path
import subprocess
import tempfile


vcl = (Path(__file__).resolve().parents[1] / "config/varnish.vcl").read_text()
assert vcl.count('"192.168.1.26"') == 1
assert vcl.count('.port = "80";') == 1
vcl = vcl.replace('"192.168.1.26"', '"${s1_addr}"')
vcl = vcl.replace('.port = "80";', '.port = "${s1_port}";')

scenario = r'''
varnishtest "Tracking links share the static homepage; CGI and auth still pass"
server s1 {
    rxreq
    expect req.url == "/"
    expect req.http.Cookie == <undef>
    txresp -body "homepage"
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
    expect req.url == "/"
    expect req.http.Authorization == "Bearer test-only"
    txresp -hdr "Cache-Control: private" -body "authenticated"
    accept
    rxreq
    expect req.url == "/index.html/other?fbclid=keep"
    expect req.http.Cookie == "other=preserved"
    txresp -hdr "Cache-Control: private" -body "other-path"
} -start
varnish v1 -vcl {
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

    txreq -url "/cgi-bin/webtop?mode=test" -hdr "Host: pdp1173.com" -hdr "Cookie: pdp11_visit_v1=1"
    rxresp
    expect resp.body == "snapshot"
    expect resp.http.X-Cache == "PASS"

    txreq -url "/cgi-bin/visit" -hdr "Host: pdp1173.com"
    rxresp
    expect resp.body == "counter"
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
with tempfile.TemporaryDirectory(prefix="webtop-vcl-test-") as directory:
    path = Path(directory) / "tracking.vtc"
    path.write_text(scenario.replace("VCL_SOURCE", vcl))
    subprocess.run(["varnishtest", "-q", str(path)], check=True)
print("PASS: homepage normalization, cache reuse, CGI, authorization, host/method guards")
