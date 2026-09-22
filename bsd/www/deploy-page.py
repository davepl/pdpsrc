#!/usr/bin/env python3
"""Publish the static homepage and images over the PDP's LAN FTP service."""
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
    'site/tmog-banner-v2.jpg', 'archive/tmog-banner.original.png',
    'archive/tmog-banner.first.png',
    'tests/virtual-panel.js', 'archive/virtual-panel/UPSTREAM.md',
    'archive/virtual-panel/pdp11-70.svg',
    'site/pdp1173-tmog-preview-v1.jpg', 'archive/link-preview/tmog.original.png',
)
PUBLIC_IMAGES = ('pdp1183-web.jpg', 'tmog-banner-v2.jpg', 'pdp1173-tmog-preview-v1.jpg')


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
                          'archive/amber-webtop', 'archive/virtual-panel',
                          'archive/link-preview'):
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
        # Publish images before the page that references them. Keep any
        # older public copy in the private source directory before replacement.
        for name in PUBLIC_IMAGES:
            content = files['site/' + name]
            target = DOCROOT + '/' + name
            try:
                previous_image = read_ftp(ftp, target)
            except ftplib.error_perm as error:
                if not str(error).startswith('550'):
                    raise
                previous_image = None
            if previous_image != content:
                if previous_image is not None:
                    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
                    backup = SOURCE + '/' + Path(name).stem + '.before-page.' + stamp + Path(name).suffix
                    ftp.storbinary('STOR ' + backup, io.BytesIO(previous_image))
                ftp.storbinary('STOR ' + target + '.new', io.BytesIO(content))
                ftp.sendcmd('SITE CHMOD 644 ' + target + '.new')
                ftp.rename(target + '.new', target)
            if read_ftp(ftp, target) != content:
                raise RuntimeError('FTP image readback does not match: ' + name)
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
    for name in PUBLIC_IMAGES:
        with urllib.request.urlopen('http://' + args.host + '/' + name, timeout=20) as response:
            if response.status != 200 or response.read() != files['site/' + name]:
                raise RuntimeError('HTTP image readback does not match: ' + name)
    print('Verified homepage and images over FTP and HTTP: ' + str(len(page)) + ' bytes HTML.')


if __name__ == '__main__':
    main()
