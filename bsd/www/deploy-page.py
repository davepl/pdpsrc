#!/usr/bin/env python3
"""Publish only the static homepage over the PDP's existing LAN FTP service."""
import argparse
import ftplib
import getpass
import io
from datetime import datetime, timezone
from pathlib import Path
import sys
import urllib.request

ROOT = Path(__file__).resolve().parent
DOCROOT = '/home/www'
SOURCE = '/usr/src/local/webtop'
SOURCE_FILES = (
    'site/index.html', 'site/index.source.html', 'minify.mjs', 'package.json',
    'package-lock.json', 'deploy-page.py', 'deploy.py', 'README.md',
    'tests/visitors.js',
)


def read_ftp(ftp, path):
    output = io.BytesIO()
    ftp.retrbinary('RETR ' + path, output.write)
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', help='PDP LAN address')
    parser.add_argument('--password-stdin', action='store_true',
                        help='Read the password from standard input instead of prompting')
    args = parser.parse_args()
    # Load every file before touching the remote machine.
    files = {name: (ROOT / name).read_bytes() for name in SOURCE_FILES}
    page = files['site/index.html']
    if b'<img' in page.lower() or b'pdp11.jpg' in page:
        parser.error('The compact homepage still references the photograph')
    password = (sys.stdin.readline().rstrip('\r\n') if args.password_stdin
                else getpass.getpass('root@' + args.host + ' FTP password: '))
    if not password:
        parser.error('A password is required')

    with ftplib.FTP() as ftp:
        ftp.connect(args.host, timeout=20)
        ftp.login('root', password)
        previous = read_ftp(ftp, DOCROOT + '/index.html')
        if previous != page:
            stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
            backup = SOURCE + '/index.before-compact.' + stamp + '.html'
            ftp.storbinary('STOR ' + backup, io.BytesIO(previous))
            print('Previous homepage saved to ' + backup, flush=True)
        # Keep the on-machine restore source in sync with the served page.
        for name, content in files.items():
            target = SOURCE + '/' + name
            ftp.storbinary('STOR ' + target + '.upload', io.BytesIO(content))
            ftp.rename(target + '.upload', target)
        if previous != page:
            temporary = DOCROOT + '/index.compact.new'
            ftp.storbinary('STOR ' + temporary, io.BytesIO(page))
            ftp.sendcmd('SITE CHMOD 644 ' + temporary)
            ftp.rename(temporary, DOCROOT + '/index.html')
        if read_ftp(ftp, DOCROOT + '/index.html') != page:
            raise RuntimeError('FTP readback does not match the prepared page')

    request = urllib.request.Request('http://' + args.host + '/',
                                     headers={'Cache-Control': 'no-cache'})
    with urllib.request.urlopen(request, timeout=20) as response:
        if response.status != 200 or response.read() != page:
            raise RuntimeError('HTTP readback does not match the prepared page')
    print('Verified compact homepage over FTP and HTTP: ' + str(len(page)) + ' bytes.')


if __name__ == '__main__':
    main()
