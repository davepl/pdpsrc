#!/usr/bin/env python3
"""Publish UNIX-Gary's static page and images through an existing PDP LAN FTP service."""
import argparse
from datetime import datetime, timezone
import ftplib
import getpass
import io
import json
from pathlib import Path
import sys
import urllib.request

ROOT = Path(__file__).resolve().parent
# Publish every dependency before the HTML that references it.
FILES = ('gary-green-v1.jpg', 'unix-gary-apple-touch-icon-v1.png',
         'unix-gary-favicon-v1.png', 'unix-gary-preview-v1.jpg', 'pdp-ai.html')


def read_ftp(ftp, name):
    data = io.BytesIO()
    ftp.retrbinary('RETR ' + name, data.write)
    return data.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', help='PDP LAN address, normally 192.168.1.29 or 192.168.1.26')
    parser.add_argument('--password-stdin', action='store_true')
    args = parser.parse_args()
    files = {name: (ROOT / 'site' / name).read_bytes() for name in FILES}
    password = (sys.stdin.readline().rstrip('\r\n') if args.password_stdin
                else getpass.getpass('root@' + args.host + ' FTP password: '))
    if not password:
        parser.error('A password is required')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    backup = ROOT / 'backups' / ('unix-gary-' + stamp)
    backup.mkdir(mode=0o700, parents=True)
    http = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with ftplib.FTP(args.host, timeout=25) as ftp:
        ftp.login('root', password)
        ftp.cwd('/home/www')
        existing = set(ftp.nlst())
        if any(name + '.new' in existing or name + '.rollback' in existing for name in FILES):
            raise RuntimeError('An earlier temporary upload exists; inspect it before retrying')
        previous = {}
        for name in FILES:
            if name in existing:
                previous[name] = read_ftp(ftp, name)
                (backup / name).write_bytes(previous[name])
        (backup / 'manifest.json').write_text(json.dumps({
            'host': args.host, 'document_root': '/home/www',
            'replaced': list(previous), 'added': [n for n in FILES if n not in previous],
        }, indent=2) + '\n')
        print('Previous files and manifest saved to', backup, flush=True)
        published = []
        try:
            for name, content in files.items():
                if previous.get(name) == content:
                    continue
                ftp.storbinary('STOR ' + name + '.new', io.BytesIO(content))
                ftp.sendcmd('SITE CHMOD 644 ' + name + '.new')
                if read_ftp(ftp, name + '.new') != content:
                    raise RuntimeError('FTP readback differs: ' + name)
                ftp.rename(name + '.new', name)
                published.append(name)
            for name, content in files.items():
                with http.open('http://' + args.host + '/' + name, timeout=25) as response:
                    if response.status != 200 or response.read() != content:
                        raise RuntimeError('HTTP readback differs: ' + name)
                print('Verified', name, len(content), 'bytes', flush=True)
        except Exception:
            # Keep the backup even if the FTP connection is lost during rollback.
            for name in reversed(published):
                if name in previous:
                    ftp.storbinary('STOR ' + name + '.rollback', io.BytesIO(previous[name]))
                    ftp.sendcmd('SITE CHMOD 644 ' + name + '.rollback')
                    ftp.rename(name + '.rollback', name)
                else:
                    ftp.delete(name)
            raise
    print('On caddy, invalidate only the updated URLs with varnishadm ban; see README.pdp-ai.md.')


if __name__ == '__main__':
    main()
