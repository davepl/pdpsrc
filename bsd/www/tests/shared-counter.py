#!/usr/bin/env python3
"""Exercise durable counter state and HTTP behavior without touching production."""
import concurrent.futures
import http.client
import importlib.util
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import threading

source = Path(__file__).resolve().parents[1] / 'proxy/visitor-service.py'
spec = importlib.util.spec_from_file_location('visitors', source)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as directory:
    db = Path(directory) / 'counter.sqlite'
    m.initialize(db, 5529)
    try:
        m.initialize(db, 0)
        raise AssertionError('Existing counter reset')
    except FileExistsError:
        pass
    server = m.Server(('127.0.0.1', 0), m.handler(db))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    def request(path='/cgi-bin/visit', method='GET'):
        c = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=15)
        c.request(method, path)
        r = c.getresponse(); body = r.read(); code = r.status
        assert r.getheader('Cache-Control') == 'no-store'
        c.close()
        return code, body
    try:
        assert request('/visits.txt') == (200, b'0000005529\n')
        assert request(method='HEAD') == (200, b'')
        assert m.value(db) == 5529
        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
            replies = list(pool.map(lambda _: request(), range(80)))
        assert all(code == 200 for code, _ in replies)
        assert sorted(int(body) for _, body in replies) == list(range(5530, 5610))
        assert request('/cgi-bin/visit-total') == (200, b'0000005609\n')
        assert request(method='POST')[0] == 405
        assert request('/cgi-bin/visit?x=1')[0] == 404
        assert m.adjust(db, 'migration-test', 7) == 5616
        assert m.adjust(db, 'migration-test', 7) == 5616
        try:
            m.adjust(db, 'migration-test', 8)
            raise AssertionError('Conflicting adjustment accepted')
        except ValueError:
            pass
        # A new process observes the committed total; backup restores it too.
        assert subprocess.check_output([sys.executable, str(source), '--database', str(db), 'read']).strip() == b'5616'
        backup = Path(directory) / 'backup.sqlite'
        subprocess.check_call([sys.executable, str(source), '--database', str(db), 'backup', str(backup)], stdout=subprocess.DEVNULL)
        assert m.value(backup) == 5616
        missing = Path(directory) / 'missing.sqlite'
        try:
            m.value(missing)
            raise AssertionError('Missing database accepted')
        except sqlite3.Error:
            assert not missing.exists()
        with sqlite3.connect(db) as connection:
            connection.execute('DELETE FROM counter')
        assert request()[0] == 503
    finally:
        server.shutdown(); server.server_close()
print('PASS: concurrent increments, read/HEAD semantics, restart, backup, idempotent migration, fail-closed missing/invalid state')
