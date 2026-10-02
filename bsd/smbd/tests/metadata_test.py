#!/usr/bin/env python3
"""Signed persistent timestamp checks against a disposable export using -M.

The default round trip creates only unique names and removes them. For a
restart check, run --phase prepare --name NAME, restart the server with the
same export and metadata file, then run --phase verify --name NAME.
"""
import argparse
import contextlib
import struct
import uuid

from impacket.smbconnection import SMBConnection, SessionError
from impacket.smb3structs import SMB2_DIALECT_002
from smbprotocol import Dialects
from smbprotocol.connection import Connection
from smbprotocol.file_info import (FileBasicInformation, FileEndOfFileInformation,
                                   FileRenameInformation)
from smbprotocol.open import (Open, SMB2QueryDirectoryResponse, SMB2QueryInfoRequest,
                              SMB2QueryInfoResponse, SMB2SetInfoRequest)
from smbprotocol.session import Session
from smbprotocol.tree import TreeConnect


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', default='127.0.0.1')
parser.add_argument('--port', type=int, default=1445)
parser.add_argument('--user', default='pdp')
parser.add_argument('--password', default='testpass')
parser.add_argument('--share', default='pdp')
parser.add_argument('--timeout', type=int, default=60)
parser.add_argument('--phase', choices=('roundtrip', 'prepare', 'verify'), default='roundtrip')
parser.add_argument('--name')
a = parser.parse_args()
if a.phase != 'roundtrip' and not a.name:
    parser.error('phased restart checks require --name')
name = a.name or 'smbd-meta-' + uuid.uuid4().hex[:12]
if not name or len(name) > 40 or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in name):
    parser.error('--name must be 1..40 ASCII letters, digits, hyphens or underscores')
renamed = name + '-renamed'
epoch = 116444736000000000
birth = epoch + 1600000000 * 10000000 + 1234567
access = epoch + 1600000200 * 10000000
modified = epoch + 1600000300 * 10000000
changed = epoch + 1600000100 * 10000000 + 7654321
final_change = epoch + 1600000400 * 10000000 + 9876543
expected = (birth, access, modified, changed)
final_expected = (birth, access, modified, final_change)


@contextlib.contextmanager
def connection():
    c = Connection(uuid.uuid4(), a.host, port=a.port, require_signing=True)
    c.connect(dialect=Dialects.SMB_2_0_2, timeout=a.timeout)
    try:
        s = Session(c, a.user, a.password, require_encryption=False)
        s.connect()
        t = TreeConnect(s, '\\\\' + a.host + '\\' + a.share)
        t.connect()
        yield c, s, t
    finally:
        c.disconnect(close=False)


def exchange(c, s, t, request):
    reply = c.receive(c.send(request, s.session_id, t.tree_connect_id), timeout=a.timeout)
    assert reply['flags'].get_value() & 8
    c.verify_signature(reply, s.session_id, force=True)
    return reply['data'].get_value()


def set_info(c, s, t, f, value):
    request = SMB2SetInfoRequest()
    request['info_type'] = value.INFO_TYPE
    request['file_info_class'] = value.INFO_CLASS
    request['file_id'] = f.file_id
    request['buffer'] = value.pack()
    exchange(c, s, t, request)


def query(c, s, t, f):
    request = SMB2QueryInfoRequest()
    request['info_type'] = 1
    request['file_info_class'] = 4
    request['output_buffer_length'] = 40
    request['file_id'] = f.file_id
    body = SMB2QueryInfoResponse()
    body.unpack(exchange(c, s, t, request))
    return struct.unpack_from('<QQQQ', body['buffer'].get_value())


def directory_times(c, s, t, target):
    directory = Open(t, '')
    directory.create(2, 1, 16, 7, 1, 1)
    try:
        request, unused = directory.query_directory(target, 37, flags=1, send=False)
        body = SMB2QueryDirectoryResponse()
        body.unpack(exchange(c, s, t, request))
        raw = body['buffer'].get_value()
        assert struct.unpack_from('<I', raw)[0] == 0, 'expected one directory entry'
        length = struct.unpack_from('<I', raw, 60)[0]
        assert raw[104:104 + length].decode('utf-16-le') == target
        # Decode raw integers: datetime conversion could discard 100ns digits.
        return struct.unpack_from('<QQQQ', raw, 8)
    finally:
        directory.close()


def cleanup():
    client = SMBConnection(a.host, a.host, sess_port=a.port,
                           preferredDialect=SMB2_DIALECT_002, timeout=a.timeout)
    try:
        client.login(a.user, a.password)
        for target in (name, renamed):
            try:
                client.deleteFile(a.share, target)
            except SessionError as error:
                if error.getErrorCode() != 0xc0000034:
                    raise
    finally:
        client.close()


created = a.phase == 'verify'
success = False
try:
    if a.phase != 'verify':
        with connection() as (c, s, t):
            f = Open(t, name)
            f.create(2, 0xc0010100, 128, 7, 2, 64)
            created = True
            f.write(b'persistent timestamp fixture\n')
            f.flush()
            value = FileBasicInformation()
            for key, stamp in zip(('creation_time', 'last_access_time', 'last_write_time', 'change_time'), expected):
                value[key] = stamp
            value['file_attributes'] = 0x80
            set_info(c, s, t, f, value)
            assert query(c, s, t, f) == expected
            f.write(b'P', 0)
            f.flush()
            assert query(c, s, t, f) == expected
            assert f.read(0, 1) == b'P'
            assert query(c, s, t, f) == expected
            eof = FileEndOfFileInformation()
            eof['end_of_file'] = 17
            set_info(c, s, t, f, eof)
            assert query(c, s, t, f) == expected
            # Suppression belongs to this open. Values changed through
            # another worker must survive subsequent I/O on this handle.
            with connection() as (c2, s2, t2):
                other = Open(t2, name)
                other.create(2, 0xc0010100, 128, 7, 1, 64)
                replacement_times = FileBasicInformation()
                replacement_times['last_access_time'] = access + 10000000
                replacement_times['last_write_time'] = modified + 20000000
                set_info(c2, s2, t2, other, replacement_times)
                current = query(c2, s2, t2, other)
                assert current[:3] == (birth, access + 10000000, modified + 20000000)
                other.close()
            f.write(b'Q', 0)
            assert f.read(0, 1) == b'Q'
            assert query(c, s, t, f) == current
            # Restore the fixed vector used by reconnect/restart checks.
            set_info(c, s, t, f, value)
            assert query(c, s, t, f) == expected
            assert directory_times(c, s, t, name) == expected
            f.close()
        with connection() as (c, s, t):
            f = Open(t, name)
            f.create(2, 0xc0010100, 128, 7, 1, 64)
            assert query(c, s, t, f) == expected
            value = FileRenameInformation()
            value['file_name'] = renamed
            set_info(c, s, t, f, value)
            after = query(c, s, t, f)
            assert after[:3] == expected[:3], after
            assert after[3] > changed, 'rename did not advance change time'
            value = FileBasicInformation()
            value['change_time'] = final_change
            value['file_attributes'] = 0x80
            set_info(c, s, t, f, value)
            assert query(c, s, t, f) == final_expected
            assert directory_times(c, s, t, renamed) == final_expected
            f.close()
        print('PASS metadata prepare: exact creation/change FILETIMEs, native access/write times, '
              'same-handle I/O suppression, other-worker updates, fresh session persistence, '
              'rename preserves birth and advances change')

    if a.phase != 'prepare':
        with connection() as (c, s, t):
            f = Open(t, renamed)
            f.create(2, 0x80000000, 128, 7, 1, 64)
            assert query(c, s, t, f) == final_expected
            assert directory_times(c, s, t, renamed) == final_expected
            f.close()
        print('PASS metadata verify: exact persisted creation/change timestamps through '
              'fresh session and directory enumeration')
    success = True
finally:
    if created and not (success and a.phase == 'prepare'):
        cleanup()
