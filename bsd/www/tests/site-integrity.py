#!/usr/bin/env python3
"""Verify unchanged static content after page or sampler deployments."""
import argparse
import hashlib
from pathlib import Path
import subprocess
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1] / 'site'
FILES = ('index.html', 'pdp11.jpg', 'pdp1183-web.jpg',
         'tmog-banner-v2.jpg', 'pdp1173-tmog-preview-v1.jpg',
         'gary-green-v1.jpg', 'glass-tty-vt220-v1.woff2', 'pdp-ai.html')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('origins', nargs='+', help='Explicit HTTP(S) origins to verify')
    args = parser.parse_args()
    for origin in args.origins:
        parsed = urlsplit(origin)
        if parsed.scheme not in ('http', 'https') or not parsed.netloc or \
                parsed.path not in ('', '/') or parsed.query or parsed.fragment:
            parser.error('Use a bare HTTP(S) origin: ' + origin)
    failures = []
    for origin in args.origins:
        for name in FILES:
            url = origin.rstrip('/') + '/' + ('' if name == 'index.html' else name)
            # curl uses the host's configured CA trust; never disable TLS checks.
            reply = subprocess.run(['curl', '--fail', '--silent', '--show-error',
                                    '--max-time', '30', url], capture_output=True)
            expected = (ROOT / name).read_bytes()
            if reply.returncode or reply.stdout != expected:
                failures.append(url)
                print('FAIL', url, 'received SHA256', hashlib.sha256(reply.stdout).hexdigest())
            else:
                print('PASS', url, len(expected), 'bytes', flush=True)
    if failures:
        raise SystemExit('Static integrity check failed: ' + ', '.join(failures))


if __name__ == '__main__':
    main()
