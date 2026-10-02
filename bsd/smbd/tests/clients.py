#!/usr/bin/env python3
"""Independent authenticated client acceptance tests (test-only dependencies).
Run with a disposable server using the documented fixture and test account.
"""
import argparse
import hashlib
import io
import pathlib

p = argparse.ArgumentParser()
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, default=1445)
p.add_argument('--password', default='testpass')
p.add_argument('--share', default='pdp')
p.add_argument('--user', default='pdp')
a = p.parse_args()
expected = bytes(range(256)) * 1025

from impacket.smbconnection import SMBConnection
from impacket.smb3structs import SMB2_DIALECT_002
c = SMBConnection(a.host, a.host, sess_port=a.port, preferredDialect=SMB2_DIALECT_002, timeout=120)
c.login(a.user, a.password)
assert c.getDialect() == SMB2_DIALECT_002
assert c.isSigningRequired()
entries = [x.get_longname() for x in c.listPath(a.share, '*')]
assert {'hello.txt', 'nested', 'empty', 'zero'} <= set(entries), entries
for name, data in [('hello.txt', b'Hello from PDP-11 SMB2!\n'), ('nested/binary file.bin', expected), ('zero', b'')]:
    output = io.BytesIO()
    c.getFile(a.share, name, output.write)
    assert output.getvalue() == data, (name, len(output.getvalue()))
try:
    assert c.listPath(a.share, 'empty/*') == []
except Exception as e:
    assert getattr(e, 'getErrorCode', lambda: None)() == 0xc000000f, e
c.logoff()
c.close()
print('PASS Impacket: signed NTLMv2, nested listing, empty directory, zero/small/262400-byte file copies')

import smbclient
smbclient.register_session(a.host, username=a.user, password=a.password, port=a.port, require_signing=True, connection_timeout=120)
root = '\\\\' + a.host + '\\' + a.share
assert {'hello.txt', 'nested', 'empty', 'zero'} <= set(smbclient.listdir(root, port=a.port))
for name, data in [('hello.txt', b'Hello from PDP-11 SMB2!\n'), ('nested\\binary file.bin', expected), ('zero', b'')]:
    with smbclient.open_file(root + '\\' + name, mode='rb', port=a.port) as f:
        actual = f.read()
    assert actual == data, (name, len(actual))
assert smbclient.listdir(root + '\\empty', port=a.port) == []
assert smbclient.stat(root + '\\nested\\binary file.bin', port=a.port).st_size == len(expected)
smbclient.reset_connection_cache()
print('PASS smbprotocol: signed NTLMv2, nested listing/stat, empty directory, zero/small/262400-byte file copies')
print('SHA256 binary file:', hashlib.sha256(expected).hexdigest())
