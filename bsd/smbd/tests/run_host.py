#!/usr/bin/env python3
"""Isolated host regression runner. Does not mount shares or alter settings."""
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from impacket.smbconnection import SMBConnection
from impacket.smb3structs import SMB2_DIALECT_002

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='smbd-host-tests-') as work:
    work = Path(work).resolve()
    share = work / 'share'
    share.mkdir()
    (share / 'nested').mkdir()
    (share / 'empty').mkdir()
    (share / 'hello.txt').write_bytes(b'Hello from PDP-11 SMB2!\n')
    (share / 'zero').write_bytes(b'')
    (share / 'nested' / 'binary file.bin').write_bytes(bytes(range(256))*1025)
    password = work / 'password'
    password.write_text('testpass\n')
    password.chmod(0o600)
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
    cases = ((False, 4, 'read'), (True, 4, 'write'),
             (True, 4, 'metadata-restart'), (False, 1, 'one-worker'), (False, 4, 'guest'))
    restart_client = None
    for writable, workers, case in cases:
        with (work / (case + '.log')).open('w+') as log:
            server = subprocess.Popen([str(root / 'smbd'), '-r', str(share),
                                       '-a', '127.0.0.1', '-p', str(port), '-T', str(work),
                                       '-c', str(workers), '-v'] +
                                      (['-g'] if case == 'guest' else ['-P', str(password)]) +
                                      (['-w', '-M', str(work / 'metadata')] if writable else []), stderr=log)
            try:
                for _ in range(100):
                    if server.poll() is not None:
                        raise RuntimeError('server exited before accepting connections')
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=.1):
                            break
                    except OSError:
                        time.sleep(.05)
                else:
                    raise RuntimeError('server did not begin listening')
                if workers == 1:
                    # The readiness socket briefly occupied the only child;
                    # allow the listener's one-second reap cycle to finish.
                    time.sleep(1.1)
                if case == 'guest':
                    tests = ['guest_test.py']
                elif case == 'metadata-restart':
                    tests = ['metadata_test.py', 'reconnect_test.py']
                elif workers == 1:
                    tests = ['protocol.py']
                elif writable:
                    tests = ['write_test.py', 'archive_test.py', 'metadata_test.py', 'reconnect_test.py']
                else:
                    tests = ['clients.py', 'protocol.py', 'rpc_test.py', 'write_test.py', 'reconnect_test.py']
                for test in tests:
                    args = [sys.executable, str(root / 'tests' / test), '--port', str(port)]
                    if test == 'protocol.py':
                        args += ['--auth-recovery-only', '--timeout', '5'] if workers == 1 else ['--fixture-root', str(share)]
                    if test == 'write_test.py' and not writable:
                        args += ['--read-only']
                    if test == 'reconnect_test.py' and writable:
                        args += ['--writable']
                    if test == 'reconnect_test.py' and case == 'metadata-restart':
                        args += ['--restart-id', hex(restart_id)]
                    if test == 'metadata_test.py':
                        args += ['--name', 'smbd-metadata-restart', '--phase',
                                 'verify' if case == 'metadata-restart' else 'prepare']
                    subprocess.run(args, check=True)
                if case == 'write':
                    restart_client = SMBConnection('127.0.0.1', '127.0.0.1', sess_port=port,
                                                   preferredDialect=SMB2_DIALECT_002)
                    restart_client.login('pdp', 'testpass')
                    restart_id = restart_client.getSMBServer()._Session['SessionID']
                elif case == 'metadata-restart':
                    restart_client.close()
                    restart_client = None
            except BaseException:
                log.flush(); log.seek(0)
                sys.stderr.write(log.read())
                raise
            finally:
                server.terminate()
                server.wait(timeout=10)
    flags = ['-std=c89', '-Wno-deprecated-non-prototype', '-fsanitize=address,undefined', '-g']
    subprocess.run(['cc', *flags, str(root / 'tests/fs_test.c'), str(root / 'fs.c'),
                    str(root / 'metadata.c'), str(root / 'wire.c'), '-o', str(work / 'fs-test')], check=True)
    subprocess.run([str(work / 'fs-test')], check=True)
    subprocess.run(['cc', *flags, str(root / 'tests/metadata_backend_test.c'),
                    str(root / 'wire.c'), '-o', str(work / 'metadata-test')], check=True)
    subprocess.run([str(work / 'metadata-test')], check=True)
    subprocess.run(['cc', *flags, str(root / 'tests/session_test.c'),
                    str(root / 'wire.c'), '-o', str(work / 'session-test')], check=True)
    subprocess.run([str(work / 'session-test')], check=True, timeout=15)
    subprocess.run(['cc', *flags, str(root / 'tests/rpc_fuzz.c'),
                    str(root / 'wire.c'), '-o', str(work / 'rpc-test')], check=True)
    subprocess.run([str(work / 'rpc-test')], check=True)
    subprocess.run([sys.executable, str(root / 'tests/auth_test.py'), '--sanitize'], check=True)
    subprocess.run([sys.executable, str(root / 'tests/password_test.py')], check=True)
    subprocess.run([sys.executable, str(root / 'tests/metadata_startup_test.py')], check=True)
print('PASS complete isolated host regression suite')
