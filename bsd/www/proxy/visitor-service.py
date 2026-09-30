#!/usr/bin/env python3
"""Persistent website totals and PDP-Gary presence on the proxy's loopback."""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import sqlite3
import sys
import time

DEFAULT_DB = Path('/var/lib/pdp-visitors/visitors.sqlite')
MAXIMUM = 2147483647
ACTIVE_SECONDS = 90
SESSION_SECONDS = 86400
ORIGINS = frozenset(scheme + '://' + host
                    for scheme in ('http', 'https')
                    for host in ('pdp1173.com', 'www.pdp1173.com',
                                 'davepl.dyndns.org', '192.168.1.26', '192.168.1.29'))
STATS = '/pdp-visitors/stats'
HEARTBEAT = '/pdp-visitors/heartbeat'


def connect(path):
    db = sqlite3.connect('file:' + str(path) + '?mode=rw', uri=True, timeout=5)
    db.execute('PRAGMA busy_timeout=5000')
    db.execute('PRAGMA synchronous=FULL')
    return db


def value(path, increment=False):
    db = connect(path)
    try:
        if increment:
            db.execute('BEGIN IMMEDIATE')
        row = db.execute('SELECT total FROM counter WHERE id=1').fetchone()
        if row is None or type(row[0]) is not int or not 0 <= row[0] <= MAXIMUM:
            raise ValueError('Invalid visitor total')
        total = row[0]
        if increment:
            if total == MAXIMUM:
                raise ValueError('Visitor total overflow')
            total += 1
            db.execute('UPDATE counter SET total=? WHERE id=1', (total,))
            db.commit()
        return total
    finally:
        db.close()


def initialize(path, total):
    if not 0 <= total <= MAXIMUM:
        raise ValueError('Invalid initial total')
    # Exclusive creation: a restore must never silently reset an existing DB.
    with path.open('xb'):
        pass
    path.chmod(0o600)
    db = connect(path)
    try:
        db.execute('PRAGMA journal_mode=WAL')
        db.executescript('''
            CREATE TABLE counter (id INTEGER PRIMARY KEY CHECK(id=1),
                total INTEGER NOT NULL CHECK(total BETWEEN 0 AND 2147483647));
            CREATE TABLE adjustments (name TEXT PRIMARY KEY, delta INTEGER NOT NULL);
        ''')
        db.execute('INSERT INTO counter VALUES (1, ?)', (total,))
        db.commit()
    finally:
        db.close()


def adjust(path, name, delta):
    if delta < 0:
        raise ValueError('Migration adjustment must not lower the count')
    db = connect(path)
    try:
        db.execute('BEGIN IMMEDIATE')
        previous = db.execute('SELECT delta FROM adjustments WHERE name=?', (name,)).fetchone()
        if previous is not None:
            if previous[0] != delta:
                raise ValueError('Adjustment name already has a different delta')
        else:
            db.execute('INSERT INTO adjustments VALUES (?, ?)', (name, delta))
            db.execute('UPDATE counter SET total=total+? WHERE id=1', (delta,))
        db.commit()
    finally:
        db.close()
    return value(path)


def prepare_sessions(path):
    value(path)  # Never create or replace the authoritative counter.
    db = connect(path)
    try:
        db.execute('BEGIN IMMEDIATE')
        exists = db.execute("SELECT 1 FROM sqlite_master WHERE name='gary_counter'").fetchone()
        if not exists:
            db.execute('''CREATE TABLE gary_counter
                      (id INTEGER PRIMARY KEY CHECK(id=1),
                       total INTEGER NOT NULL CHECK(total BETWEEN 0 AND 2147483647))''')
            db.execute('INSERT INTO gary_counter VALUES (1, 0)')
        db.execute('''CREATE TABLE IF NOT EXISTS sessions
                      (token TEXT PRIMARY KEY, last_seen INTEGER NOT NULL)''')
        db.execute('CREATE INDEX IF NOT EXISTS session_seen ON sessions(last_seen)')
        db.commit()
    finally:
        db.close()
    visitors(path)  # An existing, invalid Gary total must never be reset.


def visitors(path, session=None, increment=False, now=None):
    """Count each anonymous session once; visibility heartbeats last 90 seconds."""
    now = int(time.time()) if now is None else now
    db = connect(path)
    try:
        # One transaction serializes initial heartbeats and retries.
        db.execute('BEGIN IMMEDIATE' if session else 'BEGIN')
        row = db.execute('SELECT total FROM gary_counter WHERE id=1').fetchone()
        if row is None or type(row[0]) is not int or not 0 <= row[0] <= MAXIMUM:
            raise ValueError('Invalid visitor total')
        total = row[0]
        if session:
            token = hashlib.sha256(session.encode('ascii')).hexdigest()
            db.execute('DELETE FROM sessions WHERE last_seen < ?', (now - SESSION_SECONDS,))
            previous = db.execute('SELECT 1 FROM sessions WHERE token=?', (token,)).fetchone()
            if previous is None and increment:
                if total == MAXIMUM:
                    raise ValueError('Visitor total overflow')
                total += 1
                db.execute('UPDATE gary_counter SET total=? WHERE id=1', (total,))
            db.execute('INSERT OR REPLACE INTO sessions VALUES (?, ?)', (token, now))
        active = db.execute('SELECT count(*) FROM sessions WHERE last_seen >= ?',
                            (now - ACTIVE_SECONDS,)).fetchone()[0]
        db.commit()
        return {'total': total, 'current': active}
    finally:
        db.close()


def handler(path):
    prepare_sessions(path)

    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def log_message(self, *args):
            pass  # No visitor identifiers or request logs are retained.

        def reply(self, code, body, content_type='text/plain; charset=us-ascii'):
            self.send_response(code)
            self.send_header('Content-Type', content_type)
            if self.path in (STATS, HEARTBEAT):
                self.send_header('Vary', 'Origin')
                origin = self.headers.get('Origin')
                if origin in ORIGINS:
                    self.send_header('Access-Control-Allow-Origin', origin)
                    self.send_header('Access-Control-Allow-Methods', 'GET, HEAD, POST, OPTIONS')
                    self.send_header('Access-Control-Allow-Headers', 'Content-Type')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.close_connection = True
            if self.command != 'HEAD':
                self.wfile.write(body)

        def do_GET(self):
            if self.path == STATS:
                self.stats()
                return
            if self.path not in ('/visits.txt', '/cgi-bin/visit', '/cgi-bin/visit-total'):
                self.reply(404, b'Not found.\n')
                return
            try:
                total = value(path, self.path == '/cgi-bin/visit' and self.command == 'GET')
            except (OSError, sqlite3.Error, ValueError):
                self.reply(503, b'Count unavailable.\n')
                return
            self.reply(200, ('%010d\n' % total).encode('ascii'))

        do_HEAD = do_GET

        def stats(self, session=None, increment=False):
            if self.headers.get('Origin') not in ORIGINS and self.headers.get('Origin') is not None:
                self.reply(403, b'Origin not allowed.\n')
                return
            try:
                body = visitors(path, session, increment)
            except (OSError, sqlite3.Error, ValueError):
                self.reply(503, b'Count unavailable.\n')
                return
            self.reply(200, json.dumps(body).encode('ascii'), 'application/json')

        def do_OPTIONS(self):
            if self.path not in (STATS, HEARTBEAT):
                self.reply(404, b'Not found.\n')
            elif self.headers.get('Origin') not in ORIGINS:
                self.reply(403, b'Origin not allowed.\n')
            else:
                self.reply(204, b'')

        def do_POST(self):
            if self.path != HEARTBEAT:
                self.reply(405, b'Only GET and HEAD are supported.\n')
                return
            if self.headers.get('Origin') not in ORIGINS and self.headers.get('Origin') is not None:
                self.reply(403, b'Origin not allowed.\n')
                return
            if self.headers.get_content_type() != 'application/json':
                self.reply(415, b'JSON required.\n')
                return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if self.headers.get('Transfer-Encoding') or not 0 < size <= 256:
                    raise ValueError('Invalid body length')
                data = json.loads(self.rfile.read(size))
                if (not isinstance(data, dict) or
                        not isinstance(data.get('session'), str) or
                        not re.fullmatch('[0-9a-f]{32}', data['session']) or
                        type(data.get('increment')) is not bool):
                    raise ValueError('Invalid heartbeat')
            except (ValueError, UnicodeError):
                self.reply(400, b'Invalid heartbeat.\n')
                return
            self.stats(data['session'], data['increment'])

    return Handler


class Server(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 64

    def get_request(self):
        sock, address = super().get_request()
        sock.settimeout(10)
        return sock, address


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, default=DEFAULT_DB)
    sub = parser.add_subparsers(dest='command', required=True)
    init = sub.add_parser('init')
    init.add_argument('total', type=int)
    sub.add_parser('read')
    change = sub.add_parser('adjust')
    change.add_argument('name')
    change.add_argument('delta', type=int)
    backup = sub.add_parser('backup')
    backup.add_argument('output', type=Path)
    serve = sub.add_parser('serve')
    serve.add_argument('--port', type=int, default=6083)
    args = parser.parse_args()
    if args.command == 'init':
        initialize(args.database, args.total)
        print(value(args.database))
    elif args.command == 'adjust':
        print(adjust(args.database, args.name, args.delta))
    elif args.command == 'read':
        print(value(args.database))
    elif args.command == 'backup':
        with args.output.open('xb'):
            pass
        args.output.chmod(0o600)
        source = connect(args.database)
        destination = sqlite3.connect(args.output)
        try:
            source.backup(destination)
        finally:
            destination.close()
            source.close()
        print(args.output)
    else:
        value(args.database)  # Refuse to serve a missing or corrupt database.
        Server(('127.0.0.1', args.port), handler(args.database)).serve_forever()


if __name__ == '__main__':
    try:
        main()
    except (OSError, sqlite3.Error, ValueError) as error:
        sys.exit(str(error))
