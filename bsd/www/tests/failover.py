#!/usr/bin/env python3
"""Isolated real-Varnish probe/failover tests; all origins are local fake servers."""
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('visitors', ROOT / 'proxy/visitor-service.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

class Origin(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        state = self.server.state
        if self.path == '/health.txt':
            code = 200 if state['healthy'] else 503
            body = b'health\n'
        else:
            state['requests'] += 1
            code = 503 if state['fail'] or not state['healthy'] else 200
            body = (state['node'] + ' ' + self.path).encode()
        self.send_response(code)
        self.send_header('Content-Length', str(len(body)))
        if self.path == '/cgi-bin/webtop': self.send_header('X-Snapshot-Age', '0')
        self.end_headers(); self.wfile.write(body)


def start(server):
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0)); return sock.getsockname()[1]

with tempfile.TemporaryDirectory(prefix='pdp-failover-') as directory:
    root = Path(directory); origins = {}
    for node in ('29', '26'):
        s = ThreadingHTTPServer(('127.0.0.1', 0), Origin)
        s.state = {'node': node, 'healthy': True, 'fail': False, 'requests': 0}
        origins[node] = start(s)
    db = root / 'count.sqlite'; m.initialize(db, 5529)
    counter = start(m.Server(('127.0.0.1', 0), m.handler(db)))
    vcl = (ROOT / 'config/varnish.vcl').read_text()
    for name, server in [('pdp29', origins['29']), ('pdp26', origins['26']), ('visitors', counter)]:
        pattern = r'(backend '+name+r'\s*\{).*?(\n\})'
        original = re.search(pattern, vcl, re.S).group()
        replacement = re.sub(r'\.host = "[^"]+";', '.host = "127.0.0.1";', original)
        replacement = re.sub(r'\.port = "[^"]+";', '.port = "%s";' % server.server_port, replacement)
        vcl = vcl.replace(original, replacement)
    # Accelerate probe/TTL timing only; production logic remains unchanged.
    vcl = vcl.replace('.interval = 10s;', '.interval = 0.2s;').replace('.timeout = 3s;', '.timeout = 0.1s;')
    vcl = vcl.replace('set beresp.ttl = 5m;', 'set beresp.ttl = 1s;')
    config = root / 'test.vcl'; config.write_text(vcl)
    port = free_port(); state = root / 'varnish'
    output = (root / 'varnish.log').open('w+')
    process = subprocess.Popen(['varnishd', '-F', '-j', 'none', '-n', str(state), '-a', '127.0.0.1:'+str(port), '-f', str(config), '-s', 'malloc,32m', '-p', 'thread_pool_min=10'], stdout=output, stderr=output)
    def adm(*args):
        return subprocess.check_output(['varnishadm', '-n', str(state), *args], text=True, stderr=subprocess.DEVNULL)
    def fetch(path, method='GET', headers=None):
        c = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
        c.request(method, path, headers={'Host': 'pdp1173.com', **(headers or {})})
        r = c.getresponse(); result = (r.status, dict(r.getheaders()), r.read()); c.close()
        return result
    def healthy(node, expected):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                rows = adm('backend.list')
                row = next(x for x in rows.splitlines() if 'boot.pdp'+node+' ' in x)
                if ('healthy' in row.split()) == expected: return
            except (subprocess.CalledProcessError, StopIteration): pass
            time.sleep(.1)
        raise AssertionError('Health timeout '+node+' '+rows)
    def chat_files(node):
        # Skip existing cached copies so every path proves origin selection.
        for path in ('/pdp-ai.html', '/gary-green-v1.jpg',
                     '/unix-gary-apple-touch-icon-v2.png',
                     '/unix-gary-favicon-v2.png', '/unix-gary-preview-v1.jpg'):
            status, headers, body = fetch(path, headers={'Cookie': 'route_check=1'})
            assert status == 200 and body == (node + ' ' + path).encode(), (path, status, body)
            assert headers['X-PDP-Node'] == 'PDP .' + node, headers
            assert headers['X-Cache'] == 'PASS', headers
    try:
        healthy('29', True); healthy('26', True)
        chat_files('29')
        a = fetch('/cgi-bin/webtop'); assert a[2] == b'29 /cgi-bin/webtop', a
        assert a[1]['X-PDP-Node'] == 'PDP .29'
        # Forged selection headers cannot override primary preference.
        assert fetch('/cgi-bin/webtop', headers={'X-PDP-Mode':'26','X-PDP-Node':'PDP .26'})[2] == a[2]
        assert fetch('/cgi-bin/visit')[2] == b'0000005530\n'
        origins['29'].state['healthy'] = False; healthy('29', False)
        chat_files('26')
        b = fetch('/cgi-bin/webtop'); assert b[2] == b'26 /cgi-bin/webtop', b
        assert b[1]['X-PDP-Node'] == 'PDP .26'
        assert fetch('/visits.txt')[2] == b'0000005530\n'
        assert fetch('/cgi-bin/visit')[2] == b'0000005531\n'
        origins['29'].state['healthy'] = True; healthy('29', True)
        chat_files('29')
        assert fetch('/cgi-bin/webtop')[2] == a[2]
        # A failure before probes notice retries once on the other origin.
        adm('ban', 'req.url == /cgi-bin/webtop')
        origins['29'].state['fail'] = True
        chat_files('26')
        b = fetch('/cgi-bin/webtop'); assert b[2] == b'26 /cgi-bin/webtop', b
        assert b[1]['X-PDP-Node'] == 'PDP .26'
        origins['29'].state['fail'] = False
        assert fetch('/cgi-bin/webtop')[2] == a[2], 'Fallback frame polluted primary cache'
        homepage = fetch('/')[2]; time.sleep(1.2)
        origins['29'].state['healthy'] = False; origins['26'].state['healthy'] = False
        healthy('29', False); healthy('26', False)
        assert fetch('/')[2] == homepage, 'Both-down grace lost homepage'
        assert fetch('/cgi-bin/visit')[2] == b'0000005532\n'
        assert fetch('/cgi-bin/visit', 'HEAD')[0] == 200
        assert m.value(db) == 5532
        # Counter failure never falls through to either PDP and is never retried.
        before = sum(s.state['requests'] for s in origins.values())
        counter.shutdown(); counter.server_close()
        assert fetch('/cgi-bin/visit')[0] == 503
        assert sum(s.state['requests'] for s in origins.values()) == before
        origins['29'].state['healthy'] = True; origins['26'].state['healthy'] = True
        healthy('29', True); healthy('26', True)
        # Verify persistent manual modes using the same candidate VCL.
        for target, expected in [('26', b'26 /cgi-bin/webtop'), ('29', a[2]), ('auto', a[2])]:
            candidate = root / ('mode-'+target+'.vcl')
            candidate.write_text(vcl.replace('X-PDP-Mode = "auto";', 'X-PDP-Mode = "'+target+'";'))
            adm('vcl.load', 'mode'+target, str(candidate)); time.sleep(.6)
            adm('vcl.use', 'mode'+target)
            assert fetch('/cgi-bin/webtop')[2] == expected
        print('PASS: real probes, .29 preference, .26 failover, recovery, UNIX-Gary page/assets, origin-specific frames, bounded retry, both-down grace, independent counter and manual overrides')
    except Exception:
        output.flush(); output.seek(0); print(output.read())
        raise
    finally:
        process.terminate()
        try: process.wait(timeout=10)
        except subprocess.TimeoutExpired: process.kill(); process.wait()
        for s in origins.values(): s.shutdown(); s.server_close()
        counter.shutdown(); counter.server_close(); output.close()
