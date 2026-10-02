#!/usr/bin/env python3
"""Independent AAPL negotiation, bulk attributes, and empty stream policy."""
import argparse
import struct
import uuid
from smbprotocol import Dialects
from smbprotocol.connection import Connection
from smbprotocol.create_contexts import SMB2CreateContextRequest
from smbprotocol.exceptions import SMBResponseException
from smbprotocol.open import Open, SMB2QueryDirectoryResponse, SMB2QueryInfoRequest, SMB2QueryInfoResponse
from smbprotocol.session import Session
from smbprotocol.tree import TreeConnect

p = argparse.ArgumentParser()
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, default=1445)
p.add_argument('--share', default='pdp')
p.add_argument('--name', default='hello.txt')
a = p.parse_args()
c = Connection(uuid.uuid4(), a.host, port=a.port, require_signing=False)
c.connect(dialect=Dialects.SMB_2_0_2)
try:
    s = Session(c, 'pdp', 'testpass', require_encryption=False)
    s.connect()
    t = TreeConnect(s, '\\\\' + a.host + '\\' + a.share)
    t.connect()

    def exchange(req):
        return c.receive(c.send(req, s.session_id, t.tree_connect_id))['data'].get_value()

    def context(bitmap=7, caps=0x3f, payload=None):
        q = SMB2CreateContextRequest()
        q['buffer_name'] = b'AAPL'
        q['buffer_data'] = struct.pack('<IIQQ', 1, 0, bitmap, caps) if payload is None else payload
        return q

    d = Open(t, '')
    replies = d.create(2, 1, 16, 7, 1, 1, create_contexts=[context()])
    assert len(replies) == 1
    r = replies[0]
    assert struct.unpack_from('<IIQQQ', r) == (1, 0, 7, 0x14, 0)
    assert struct.unpack_from('<II', r, 32) == (0, 14)
    assert r[40:54].decode('utf-16le') == '2.11BSD' and r[54:] == b'\0\0'
    request = SMB2QueryInfoRequest()
    request['info_type'] = 2
    request['file_info_class'] = 5
    request['output_buffer_length'] = 512
    request['file_id'] = d.file_id
    response = SMB2QueryInfoResponse()
    response.unpack(exchange(request))
    assert struct.unpack_from('<I', response['buffer'].get_value())[0] & 0x40000
    request, unused = d.query_directory(a.name, 37, flags=1, send=False)
    response = SMB2QueryDirectoryResponse()
    response.unpack(exchange(request))
    raw = response['buffer'].get_value()
    assert struct.unpack_from('<I', raw, 64)[0] != 0  # max access
    assert struct.unpack_from('<H', raw, 68)[0] == 1  # no xattrs
    assert raw[70:94] == b'\0' * 24  # resource fork length and Finder info
    assert struct.unpack_from('<H', raw, 94)[0] & 0o170000 == 0o100000
    d.close()

    # AAPL capability selection and response lengths are request dependent.
    d = Open(t, '')
    r = d.create(2, 1, 16, 7, 1, 1, create_contexts=[context(1, 5)])[0]
    assert len(r) == 24 and struct.unpack('<IIQQ', r) == (1, 0, 1, 5)
    d.close()
    for payload in [b'\1' * 23, struct.pack('<IIQQ', 1, 1, 7, 0x3f)]:
        try:
            Open(t, '').create(2, 1, 16, 7, 1, 1, create_contexts=[context(payload=payload)])
        except SMBResponseException as e:
            assert e.status == 0xc000000d
        else:
            raise AssertionError('malformed AAPL request accepted')

    f = Open(t, a.name + '::$DATA')
    f.create(2, 0x80000000, 128, 7, 1, 64)
    assert f.read(0, 100) == b'Hello from PDP-11 SMB2!\n'
    f.close()
    for access, disposition, expected in [(0x80000000, 1, 0xc0000034),
                                          (0x40000000, 3, 0xc0000022)]:
        try:
            Open(t, a.name + ':AFP_Resource:$DATA').create(2, access, 128, 7, disposition, 64)
        except SMBResponseException as e:
            assert e.status == expected, hex(e.status)
        else:
            raise AssertionError('named metadata stream unexpectedly opened')
    print('PASS Mac fast mode: AAPL v1/v2, filesystem capability, bulk Unix mode/no-xattrs, malformed contexts, data fork, metadata write refusal')
finally:
    c.disconnect(close=False)
