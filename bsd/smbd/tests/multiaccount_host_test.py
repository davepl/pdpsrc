#!/usr/bin/env python3
"""Run private independent multiaccount checks against a local disposable export."""
import json
import os
from pathlib import Path
import pwd
import secrets
import socket
import subprocess
import sys
import tempfile
import time

from impacket.ntlm import compute_nthash


binary = Path(__file__).resolve().parents[1] / 'smbd'
test = Path(__file__).with_name('multiaccount_test.py')
with tempfile.TemporaryDirectory(prefix='smb-multiaccount-') as temporary:
    work = Path(temporary).resolve()
    work.chmod(0o700)
    share = work / 'share'
    share.mkdir()
    (share / 'hello.txt').write_bytes(b'Independent SMB2 account and reconnect test\n')
    unixuser = pwd.getpwuid(os.getuid()).pw_name
    passwords = {user: secrets.token_urlsafe(24) for user in ('dave', 'root')}
    accounts = work / 'accounts'
    accounts.write_text(''.join(user + ':' + unixuser + ':' + compute_nthash(passwords[user]).hex() + '\n'
                                for user in ('dave', 'root')))
    accounts.chmod(0o600)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    credentials = work / 'credentials.json'
    credentials.write_text(json.dumps({'accounts': passwords, 'port': port}))
    credentials.chmod(0o600)
    with (work / 'server.log').open('w+') as log:
        server = subprocess.Popen([str(binary), '-r', str(share), '-s', 'pdp',
                                   '-F', str(accounts), '-T', str(work),
                                   '-a', '127.0.0.1', '-p', str(port), '-w', '-c', '8', '-v'],
                                  stderr=log)
        try:
            for _ in range(100):
                if server.poll() is not None:
                    raise RuntimeError('host test server exited before ready')
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=.1):
                        break
                except OSError:
                    time.sleep(.05)
            else:
                raise RuntimeError('host test server did not become ready')
            subprocess.run([sys.executable, str(test), '--credentials', str(credentials),
                            '--writable', '.', '--reconnect-path', 'hello.txt', '--timeout', '10'], check=True)
            subprocess.run([sys.executable, str(test), '--credentials', str(credentials),
                            '--smoke-only', '--reconnect-path', 'hello.txt', '--timeout', '10'], check=True)
            assert sorted(p.name for p in share.iterdir()) == ['hello.txt'], 'test files leaked'
        except BaseException:
            log.flush()
            log.seek(0)
            sys.stderr.write(log.read())
            raise
        finally:
            server.terminate()
            server.wait(timeout=10)
print('PASS private local multiaccount host test; disposable export and credentials removed')
