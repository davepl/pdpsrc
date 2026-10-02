#!/usr/bin/env python3
"""Exercise Gary presence against a private database; never touch live counts."""
import concurrent.futures
import hashlib
import http.client
import importlib.util
import json
from pathlib import Path
import sqlite3
import tempfile
import threading

source = Path(__file__).resolve().parents[1] / 'proxy/visitor-service.py'
spec = importlib.util.spec_from_file_location('visitors', source)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as directory:
    db = Path(directory) / 'visitors.sqlite'
    m.initialize(db, 12345)
    server = m.Server(('127.0.0.1', 0), m.handler(db))
    threading.Thread(target=server.serve_forever, daemon=True).start()

    def request(method='GET', path=m.STATS, data=None, origin='http://192.168.1.26'):
        c = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=15)
        headers = {'Origin': origin, 'Content-Type': 'application/json'}
        c.request(method, path, json.dumps(data) if data is not None else None, headers)
        response = c.getresponse()
        body, code = response.read(), response.status
        assert response.getheader('Cache-Control') == 'no-store'
        assert response.getheader('Vary') == 'Origin'
        if origin in m.ORIGINS:
            assert response.getheader('Access-Control-Allow-Origin') == origin
        else:
            assert response.getheader('Access-Control-Allow-Origin') is None
        c.close()
        return code, json.loads(body) if code == 200 and body else body

    def beat(token='a' * 32, increment=True):
        return request('POST', m.HEARTBEAT, {'session': token, 'increment': increment})

    try:
        assert request() == (200, {'total': 0, 'current': 0})
        assert request('HEAD') == (200, b'')
        assert request('OPTIONS', m.HEARTBEAT)[0] == 204
        assert request(origin='https://evil.example')[0] == 403
        assert request('POST', m.HEARTBEAT, {'session': 'a'*32, 'increment': True}, 'https://evil.example')[0] == 403
        assert request('POST', m.HEARTBEAT, {'session': 'invalid', 'increment': True})[0] == 400
        assert request('POST', m.HEARTBEAT, {'session': 'a'*32, 'increment': 'yes'})[0] == 400
        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
            replies = list(pool.map(lambda _: beat(), range(40)))
        assert all(reply == (200, {'total': 1, 'current': 1}) for reply in replies)
        assert beat('b'*32) == (200, {'total': 2, 'current': 2})
        assert beat('c'*32, False) == (200, {'total': 2, 'current': 3})
        assert beat('c'*32) == (200, {'total': 2, 'current': 3})
        assert m.value(db) == 12345, 'Gary must never change the homepage total'
        m.prepare_sessions(db)  # Restart/migration preserves totals and deduplication.
        assert beat() == (200, {'total': 2, 'current': 3})
        with sqlite3.connect(db) as connection:
            connection.execute('UPDATE sessions SET last_seen=1000')
            tokens = [row[0] for row in connection.execute('SELECT token FROM sessions')]
            assert 'a'*32 not in tokens
            assert hashlib.sha256(('a'*32).encode()).hexdigest() in tokens
        assert m.visitors(db, now=1090) == {'total': 2, 'current': 3}
        assert m.visitors(db, now=1091) == {'total': 2, 'current': 0}
        assert m.visitors(db, 'a'*32, True, now=1091) == {'total': 2, 'current': 1}
        assert m.visitors(db, 'd'*32, True, now=90000) == {'total': 3, 'current': 1}
        with sqlite3.connect(db) as connection:
            assert connection.execute('SELECT count(*) FROM sessions').fetchone()[0] == 1
            connection.execute('DELETE FROM gary_counter')
        assert request()[0] == 503
        try:
            m.prepare_sessions(db)
            raise AssertionError('Invalid Gary state silently reset')
        except ValueError:
            pass
        assert m.value(db) == 12345
    finally:
        server.shutdown()
        server.server_close()
print('PASS: independent Gary total, concurrent deduplication, CORS, validation, presence expiry, restart, fail-closed state')
