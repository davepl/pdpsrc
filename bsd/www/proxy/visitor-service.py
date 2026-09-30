#!/usr/bin/env python3
"""Persistent public visitor total, served only on the proxy's loopback interface."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sqlite3
import sys

DEFAULT_DB = Path('/var/lib/pdp-visitors/visitors.sqlite')
MAXIMUM = 2147483647


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


def handler(path):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def log_message(self, *args):
            pass  # No visitor identifiers or request logs are retained.

        def reply(self, code, body):
            self.send_response(code)
            self.send_header('Content-Type', 'text/plain; charset=us-ascii')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.close_connection = True
            if self.command != 'HEAD':
                self.wfile.write(body)

        def do_GET(self):
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

        def do_POST(self):
            self.reply(405, b'Only GET and HEAD are supported.\n')

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
