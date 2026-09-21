#!/usr/bin/env python3
"""Publish the static homepage and photo over the PDP's LAN FTP service."""
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
    'tests/visitors.js', 'site/pdp1183-web.jpg', 'install.sh', 'VERIFICATION.md',
    'archive/README.md', 'archive/pre-webtop-192.168.1.26/index.html',
    'archive/pre-webtop-192.168.1.26/pdp1183.jpg',
    'archive/amber-webtop/index.html', 'archive/amber-webtop/index.source.html',
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
    if len(files['site/pdp1183-web.jpg']) >= 105000:
        parser.error('The homepage photograph must be below 105,000 bytes')
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
            backup = SOURCE + '/index.before-page.' + stamp + '.html'
            ftp.storbinary('STOR ' + backup, io.BytesIO(previous))
            print('Previous homepage saved to ' + backup, flush=True)
        # Keep the on-machine restore source in sync with the served page.
        for directory in ('archive', 'archive/pre-webtop-192.168.1.26',
                          'archive/amber-webtop'):
            target = SOURCE + '/' + directory
            try:
                ftp.mkd(target)
            except ftplib.error_perm:
                # Existing directories are fine; any other failure must stop
                # publication rather than leaving the restore source incomplete.
                ftp.cwd(target)
        for name, content in files.items():
            target = SOURCE + '/' + name
            ftp.storbinary('STOR ' + target + '.upload', io.BytesIO(content))
            ftp.rename(target + '.upload', target)
        # Publish the photograph before the page that references it. Keep any
        # older public copy in the private source directory before replacement.
        photo = files['site/pdp1183-web.jpg']
        photo_target = DOCROOT + '/pdp1183-web.jpg'
        try:
            old_photo = read_ftp(ftp, photo_target)
        except ftplib.error_perm as error:
            if not str(error).startswith('550'):
                raise
            old_photo = None
        if old_photo != photo:
            if old_photo is not None:
                stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
                backup = SOURCE + '/pdp1183-web.before-page.' + stamp + '.jpg'
                ftp.storbinary('STOR ' + backup, io.BytesIO(old_photo))
            ftp.storbinary('STOR ' + photo_target + '.new', io.BytesIO(photo))
            ftp.sendcmd('SITE CHMOD 644 ' + photo_target + '.new')
            ftp.rename(photo_target + '.new', photo_target)
        if read_ftp(ftp, photo_target) != photo:
            raise RuntimeError('FTP photo readback does not match the prepared photo')
        if previous != page:
            temporary = DOCROOT + '/index.page.new'
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
    with urllib.request.urlopen('http://' + args.host + '/pdp1183-web.jpg', timeout=20) as response:
        if response.status != 200 or response.read() != photo:
            raise RuntimeError('HTTP photo readback does not match the prepared photo')
    print('Verified homepage and photo over FTP and HTTP: ' + str(len(page)) +
          ' bytes HTML, ' + str(len(photo)) + ' bytes JPEG.')


if __name__ == '__main__':
    main()
