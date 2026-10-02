#!/usr/bin/env python3
"""Read-only live check of eight simultaneous directory handles/cursors.

Run on the native server with -M to include the persistent metadata descriptor
in its 30-descriptor process limit. The ordinary acceptance fixture is required.
"""
import argparse
import uuid

from smbprotocol import Dialects
from smbprotocol.connection import Connection
from smbprotocol.exceptions import SMBResponseException
from smbprotocol.open import Open
from smbprotocol.session import Session
from smbprotocol.tree import TreeConnect

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', required=True)
parser.add_argument('--port', type=int, default=445)
parser.add_argument('--share', default='pdp')
parser.add_argument('--user', default='pdp')
parser.add_argument('--password', default='testpass')
args = parser.parse_args()
connection = Connection(uuid.uuid4(), args.host, port=args.port,
                        require_signing=True)
opened = []
try:
    connection.connect(dialect=Dialects.SMB_2_0_2, timeout=120)
    session = Session(connection, args.user, args.password, require_encryption=False)
    session.connect()
    tree = TreeConnect(session, '\\\\' + args.host + '\\' + args.share)
    tree.connect()
    for _ in range(8):
        handle = Open(tree, '')
        handle.create(2, 1, 16, 7, 1, 1)
        opened.append(handle)
        assert handle.query_directory('*', 37, flags=1, max_output=1024)
    # All eight DIR cursors stay open while new path operations use temporary
    # descriptors. This is stricter than opening eight regular files.
    for handle in opened:
        assert handle.query_directory('*', 37, flags=1, max_output=1024)
    excess = Open(tree, '')
    try:
        excess.create(2, 1, 16, 7, 1, 1)
    except SMBResponseException as error:
        assert error.status == 0xc000011f, error
    else:
        excess.close()
        raise AssertionError('ninth directory handle was accepted')
    for handle in opened:
        handle.close()
    opened.clear()
    recovered = Open(tree, 'hello.txt')
    recovered.create(2, 0x80000000, 128, 7, 1, 64)
    opened.append(recovered)
    assert recovered.read(0, 1024) == b'Hello from PDP-11 SMB2!\n'
    print('PASS eight simultaneous directory handles/cursors; ninth rejected; '
          'all descriptors released and subsequent file read succeeds')
finally:
    for handle in opened:
        try:
            handle.close()
        except Exception:
            pass
    connection.disconnect(close=False)
