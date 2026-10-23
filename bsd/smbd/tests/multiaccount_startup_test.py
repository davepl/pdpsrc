#!/usr/bin/env python3
"""Independent whole-executable account-configuration rejection checks."""
import os
from pathlib import Path
import pwd
import socket
import subprocess
import tempfile
import time

from impacket.ntlm import compute_nthash


binary = Path(__file__).resolve().parents[1] / 'smbd'
with tempfile.TemporaryDirectory(prefix='smb-account-startup-') as temporary:
    work = Path(temporary).resolve()
    work.chmod(0o700)
    share = work / 'share'
    share.mkdir()
    unixuser = pwd.getpwuid(os.getuid()).pw_name
    hashed = compute_nthash('independent-private-test-fixture').hex()
    valid = 'dave:' + unixuser + ':' + hashed + '\nroot:' + unixuser + ':' + hashed + '\n'
    accounts = work / 'accounts'

    def rejection(text=valid, extra=(), expected=1, mode=0o600, source=None):
        if accounts.exists():
            accounts.chmod(0o600)
        accounts.write_text(text)
        accounts.chmod(mode)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        result = subprocess.run([str(binary), '-r', str(share), '-F', str(source or accounts),
                                 '-T', str(work), '-a', '127.0.0.1', '-p', str(port), *extra],
                                capture_output=True, timeout=5)
        assert result.returncode == expected, 'configuration did not fail at the expected validation boundary'

    for extra in (('-u', 'dave'), ('-U', unixuser), ('-H', str(work/'unused.hash')),
                  ('-P', str(work/'unused.password')), ('-g',)):
        rejection(extra=extra, expected=2)
    print('PASS startup: -F rejects simultaneous -u, -U, -H, -P, and guest options')

    for malformed in ('', valid.rstrip('\n'), valid + '\n',
                      'dave:' + unixuser + ':invalid\n',
                      valid + 'DAVE:' + unixuser + ':' + hashed + '\n',
                      'dave:unix-account-that-does-not-exist:' + hashed + '\n',
                      ''.join('user' + str(i) + ':' + unixuser + ':' + hashed + '\n' for i in range(9))):
        rejection(malformed)
    for mode in (0o644, 0o660, 0o400, 0o700):
        rejection(mode=mode)
    print('PASS startup: empty, unterminated, blank, malformed, duplicate, unknown mapping, ninth account, and unsafe-mode files rejected')

    accounts.chmod(0o600)
    alias = work / 'accounts-alias'
    alias.symlink_to(accounts)
    rejection(source=alias)
    alias.unlink()
    os.link(accounts, alias)
    rejection()
    alias.unlink()
    rejection(source=Path('relative-accounts'))
    print('PASS startup: symlink, hardlink, and relative credential paths rejected')

    if os.geteuid() != 0:
        rejection('root:root:' + hashed + '\n')
        print('PASS startup: nonroot process cannot map an SMB identity to root')

    accounts.write_text(valid.replace('\n', '\r\n'))
    accounts.chmod(0o600)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    server = subprocess.Popen([str(binary), '-r', str(share), '-F', str(accounts),
                               '-T', str(work), '-a', '127.0.0.1', '-p', str(port)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            assert server.poll() is None, 'valid CRLF account file rejected'
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=.1):
                    break
            except OSError:
                time.sleep(.05)
        else:
            raise AssertionError('valid CRLF server did not become ready')
    finally:
        server.terminate()
        server.wait(timeout=5)
    print('PASS startup: private valid CRLF account database accepted')
print('PASS independent startup acceptance complete; fixtures removed')
