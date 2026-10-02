#!/usr/bin/env python3
"""Two concurrent lab-guest sessions must have independent live IDs."""
import argparse
from impacket.smbconnection import SMBConnection
from impacket.smb3structs import SMB2_DIALECT_002

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--port', type=int, required=True)
a = p.parse_args()
clients = []
try:
    for unused in range(2):
        c = SMBConnection('127.0.0.1', '127.0.0.1', sess_port=a.port,
                          preferredDialect=SMB2_DIALECT_002)
        clients.append(c)
        c.login('', '')
        assert c.isGuestSession()
    assert clients[0].getSMBServer()._Session['SessionID'] != clients[1].getSMBServer()._Session['SessionID']
    for c in clients:
        tree = c.connectTree('pdp')
        f = c.openFile(tree, 'hello.txt', desiredAccess=0x80000000)
        assert c.readFile(tree, f, bytesToRead=1024) == b'Hello from PDP-11 SMB2!\n'
        c.closeFile(tree, f)
finally:
    for c in clients:
        c.close()
print('PASS two simultaneous lab-guest sessions have distinct IDs and readable shares')
