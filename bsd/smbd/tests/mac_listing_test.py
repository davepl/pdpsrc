#!/usr/bin/env python3
"""Exercise the unsigned CREATE/QUERY_DIRECTORY/CLOSE used by macOS.

Read-only requests against the existing host fixture; no mount or policy changes.
"""
import argparse
import struct
from impacket.smbconnection import SMBConnection
from impacket.smb3structs import SMB2_DIALECT_002

p = argparse.ArgumentParser()
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, default=1445)
p.add_argument('--share', default='pdp')
a = p.parse_args()
c = SMBConnection(a.host, a.host, sess_port=a.port,
                  preferredDialect=SMB2_DIALECT_002, timeout=60)
try:
    c.login('pdp', 'testpass')
    tree = c.connectTree(a.share)
    server = c.getSMBServer()
    sock = server.get_socket()
    sid = server._Session['SessionID']
    mid = server._Connection['SequenceWindow']

    def packet(cmd, body, related=False, more=False):
        global mid
        h = bytearray(64)
        h[:4] = b'\xfeSMB'
        struct.pack_into('<H', h, 4, 64)
        struct.pack_into('<HH', h, 12, cmd, 3)
        pad = (-(64 + len(body))) % 8 if more else 0
        struct.pack_into('<II', h, 16, 4 if related else 0,
                         64 + len(body) + pad if more else 0)
        struct.pack_into('<Q', h, 24, mid)
        struct.pack_into('<I', h, 36, tree)
        struct.pack_into('<Q', h, 40, sid)
        mid += 1
        return h + body + b'\0' * pad

    def exact(n):
        data = b''
        while len(data) < n:
            chunk = sock.recv(n - len(data))
            assert chunk, 'unexpected disconnect'
            data += chunk
        return data

    def exchange(data):
        sock.sendall(struct.pack('>I', len(data)) + data)
        data = exact(struct.unpack('>I', exact(4))[0])
        replies = []
        while True:
            size = struct.unpack_from('<I', data, 20)[0]
            part = data[:size] if size else data
            assert not struct.unpack_from('<I', part, 16)[0] & 8
            assert part[48:64] == b'\0' * 16
            replies.append(part)
            if not size:
                return replies
            data = data[size:]

    for i in range(20):
        missing = bool(i % 2)
        name = (('._\uf029' if i % 4 == 1 else '._missing-test')
                if missing else 'hello.txt').encode('utf-16le')
        create = bytearray(56)
        struct.pack_into('<H', create, 0, 57)
        struct.pack_into('<I', create, 24, 0x80000000)
        struct.pack_into('<III', create, 32, 7, 1, 1)
        query = bytearray(32)
        struct.pack_into('<HBBI', query, 0, 33, 37, 3, 0)
        query[8:24] = b'\xff' * 16
        struct.pack_into('<HHI', query, 24, 96, len(name), 8192)
        close = struct.pack('<HHI', 24, 0, 0) + b'\xff' * 16
        replies = exchange(packet(5, create, more=True) +
                           packet(14, query + name, True, True) +
                           packet(6, close, True))
        statuses = [struct.unpack_from('<I', r, 8)[0] for r in replies]
        assert statuses == ([0, 0xc000000f, 0] if missing else [0, 0, 0]), statuses
        if not missing:
            off = struct.unpack_from('<H', replies[1], 66)[0]
            n = struct.unpack_from('<I', replies[1], off + 60)[0]
            assert replies[1][off + 104:off + 104 + n].decode('utf-16le') == 'hello.txt'
        # Reserved bytes must not contain the preceding directory record.
        echo = exchange(packet(13, struct.pack('<HH', 4, 0)))[0]
        assert len(echo) == 68 and echo[64:] == b'\x04\0\0\0'
    print('PASS Mac metadata compounds: hits, misses, empty-query cleanup, unsigned framing and clean reserved bytes')
finally:
    c.close()
