#!/usr/bin/env python3
"""Signed write acceptance tests against a disposable writable export.

Creates only a uniquely named directory and removes only its own files.
No client security policy is modified. --read-only tests the default policy.
"""
import argparse
import contextlib
import io
import time
import uuid

import smbclient
from impacket.smbconnection import SMBConnection
from impacket.smb3structs import SMB2_DIALECT_002
from smbprotocol import Dialects
from smbprotocol.connection import Connection
from smbprotocol.exceptions import SMBResponseException
from smbprotocol.open import Open, SMB2SetInfoRequest
from smbprotocol.file_info import FileRenameInformation
from smbprotocol.session import Session
from smbprotocol.tree import TreeConnect


p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, default=1445)
p.add_argument('--user', default='pdp')
p.add_argument('--password', default='testpass')
p.add_argument('--share', default='pdp')
p.add_argument('--read-only', action='store_true')
p.add_argument('--disconnect-timeout', type=float,
               help='seconds to allow worker cleanup (default: 5 locally, 30 remotely)')
a = p.parse_args()
directory = 'smbd-write-' + uuid.uuid4().hex[:12]
root = '\\\\' + a.host + '\\' + a.share
base = root + '\\' + directory
payload = bytes(range(256)) * 1025
options = {'port': a.port}
disconnect_timeout = a.disconnect_timeout
if disconnect_timeout is None:
    disconnect_timeout = 5 if a.host in ('127.0.0.1', 'localhost', '::1') else 30
assert disconnect_timeout > 0


@contextlib.contextmanager
def connection():
    c = Connection(uuid.uuid4(), a.host, port=a.port, require_signing=True)
    c.connect(dialect=Dialects.SMB_2_0_2, timeout=120)
    try:
        s = Session(c, a.user, a.password, require_encryption=False)
        s.connect()
        t = TreeConnect(s, root)
        t.connect()
        yield c, s, t
    finally:
        c.disconnect(close=False)


def expect_status(code, action):
    try:
        action()
    except SMBResponseException as e:
        assert e.status == code, (hex(e.status), hex(code), str(e))
    else:
        raise AssertionError('expected SMB status ' + hex(code))


def rename_handle(c, s, t, f, name, replace=False):
    value = FileRenameInformation()
    value['replace_if_exists'] = replace
    value['file_name'] = name
    request = SMB2SetInfoRequest()
    request['info_type'] = value.INFO_TYPE
    request['file_info_class'] = value.INFO_CLASS
    request['file_id'] = f.file_id
    request['buffer'] = value.pack()
    reply = c.receive(c.send(request, s.session_id, t.tree_connect_id), timeout=120)
    c.verify_signature(reply, s.session_id, force=True)


def await_released(opened):
    """A new transport must acquire a handle orphaned by a TCP disconnect."""
    deadline = time.monotonic() + disconnect_timeout
    while True:
        try:
            opened.create(2, 0x80000000, 128, 7, 1, 64)
            return
        except SMBResponseException as error:
            assert error.status == 0xc0000043, error
            if time.monotonic() >= deadline:
                raise AssertionError('exclusive share lock survived abrupt TCP disconnect') from error
            time.sleep(.1)


def await_deleted(opened):
    """Deletion may wait for the disconnected worker to finish cleanup."""
    deadline = time.monotonic() + disconnect_timeout
    while True:
        try:
            opened.create(2, 0x80000000, 128, 7, 1, 64)
        except SMBResponseException as error:
            if error.status == 0xc0000034:
                return
            assert error.status == 0xc0000056, error
            if time.monotonic() >= deadline:
                raise AssertionError('delete-on-close remained pending after the last reader closed') from error
            time.sleep(.1)
        else:
            opened.close()
            raise AssertionError('delete-on-close file became visible after its last handle closed')


if a.read_only:
    with connection() as (c, s, t):
        f = Open(t, directory)
        expect_status(0xc0000022, lambda: f.create(2, 0x40000000, 128, 7, 2, 64))
    print('PASS read-only default: file creation denied')
    raise SystemExit(0)

smbclient.register_session(a.host, username=a.user, password=a.password,
                           port=a.port, require_signing=True, connection_timeout=120)
try:
    smbclient.mkdir(base, **options)
    first = base + '\\copy.bin'
    with smbclient.open_file(first, mode='wb', **options) as f:
        assert f.write(payload) == len(payload)
        f.flush()
    with smbclient.open_file(first, mode='rb', **options) as f:
        assert f.read() == payload
    with smbclient.open_file(first, mode='r+b', **options) as f:
        f.seek(65530)
        f.write(b'boundary update')
        f.truncate(65545)
        f.flush()
    changed = payload[:65530] + b'boundary update'
    with smbclient.open_file(first, mode='rb', **options) as f:
        assert f.read() == changed
    smbclient.rename(first, base + '\\renamed.bin', **options)
    with smbclient.open_file(first, mode='wb', **options) as f:
        f.write(b'replaced')
    smbclient.replace(first, base + '\\renamed.bin', **options)
    with smbclient.open_file(base + '\\renamed.bin', mode='rb', **options) as f:
        assert f.read() == b'replaced'
    smbclient.remove(base + '\\renamed.bin', **options)
    smbclient.mkdir(base + '\\folder', **options)
    smbclient.rename(base + '\\folder', base + '\\renamed folder', **options)
    smbclient.rmdir(base + '\\renamed folder', **options)
    print('PASS smbprotocol: signed 262400-byte copy, flush, update, truncate, '
          'rename, replacement, delete, mkdir/rmdir')

    imp = SMBConnection(a.host, a.host, sess_port=a.port,
                        preferredDialect=SMB2_DIALECT_002, timeout=120)
    try:
        imp.login(a.user, a.password)
        assert imp.isSigningRequired()
        name = directory + '\\impacket.bin'
        imp.putFile(a.share, name, io.BytesIO(payload).read)
        out = io.BytesIO()
        imp.getFile(a.share, name, out.write)
        assert out.getvalue() == payload
        imp.rename(a.share, name, directory + '\\impacket-renamed.bin')
        imp.deleteFile(a.share, directory + '\\impacket-renamed.bin')
        imp.logoff()
    finally:
        imp.close()
    print('PASS Impacket: signed write/read byte comparison, rename and delete')

    with connection() as (c1, s1, t1), connection() as (c2, s2, t2):
        name = directory + '\\sharing.bin'
        writer = Open(t1, name)
        writer.create(2, 0xc0000000, 128, 7, 2, 66)  # FILE_WRITE_THROUGH
        block = bytes(range(256)) * 256
        assert writer.write(block, 0) == 65536
        writer.flush()
        assert writer.read(0, 65536) == block
        writer.close()

        reader = Open(t1, name)
        reader.create(2, 0x80000000, 128, 1, 1, 64)
        conflict = Open(t2, name)
        expect_status(0xc0000043, lambda: conflict.create(2, 0x40000000, 128, 7, 1, 64))
        # Even a read open must permit the access held by existing writers.
        reader.close()
        writer = Open(t1, name)
        writer.create(2, 0x40000000, 128, 7, 1, 64)
        expect_status(0xc0000043, lambda: conflict.create(2, 0x80000000, 128, 1, 1, 64))
        writer.close()

        reader = Open(t1, name)
        reader.create(2, 0x80000000, 128, 7, 1, 64)
        deleting = Open(t2, name)
        deleting.create(2, 0x00010000, 128, 7, 1, 0x1040)
        deleting.close()
        expect_status(0xc0000056, lambda: conflict.create(2, 0x80000000, 128, 7, 1, 64))
        assert reader.read(0, 256) == bytes(range(256))
        reader.close()
        expect_status(0xc0000034, lambda: conflict.create(2, 0x80000000, 128, 7, 1, 64))

        # A handle in another worker survives rename and keeps its identity.
        moving = Open(t1, name)
        moving.create(2, 0xc0010000, 128, 7, 2, 64)
        moving.write(b'identity')
        reader = Open(t2, name)
        reader.create(2, 0x80000000, 128, 7, 1, 64)
        renamed = directory + '\\moved.bin'
        rename_handle(c1, s1, t1, moving, renamed)
        assert reader.read(0, 8) == b'identity'
        reader.close()
        moving.close()
    smbclient.remove(base + '\\moved.bin', **options)
    print('PASS two independent TCP sessions: sharing conflicts in both directions, '
          'delete pending until last close, cross-worker rename identity')

    orphan = directory + '\\abrupt-exclusive.bin'
    with connection() as (owner_c, owner_s, owner_t):
        exclusive = Open(owner_t, orphan)
        exclusive.create(2, 0xc0000000, 128, 0, 2, 64)
        exclusive.write(b'orphaned exclusive handle')
        # close=False shuts down TCP without SMB CLOSE, TREE_DISCONNECT, or LOGOFF.
        owner_c.disconnect(close=False)
        with connection() as (probe_c, probe_s, probe_t):
            reopened = Open(probe_t, orphan)
            await_released(reopened)
            assert reopened.read(0, 64) == b'orphaned exclusive handle'
            reopened.close()
    smbclient.remove(base + '\\abrupt-exclusive.bin', **options)

    orphan = directory + '\\abrupt-delete.bin'
    with connection() as (reader_c, reader_s, reader_t):
        reader = Open(reader_t, orphan)
        reader.create(2, 0xc0000000, 128, 7, 2, 64)
        reader.write(b'reader survives disconnected deleting handle')
        reader.close()
        reader = Open(reader_t, orphan)
        reader.create(2, 0x80000000, 128, 7, 1, 64)
        with connection() as (deleting_c, deleting_s, deleting_t):
            deleting = Open(deleting_t, orphan)
            deleting.create(2, 0x00010000, 128, 7, 1, 0x1040)
            deleting_c.disconnect(close=False)
            assert reader.read(0, 64) == b'reader survives disconnected deleting handle'
            with connection() as (probe_c, probe_s, probe_t):
                probe = Open(probe_t, orphan)
                expect_status(0xc0000056, lambda: probe.create(2, 0x80000000, 128, 7, 1, 64))
                reader.close()
                await_deleted(probe)
    print('PASS abrupt TCP disconnect: exclusive handle released; delete-on-close waits for last reader then deletes')

    assert smbclient.listdir(base, **options) == []
    smbclient.rmdir(base, **options)
finally:
    smbclient.reset_connection_cache()
print('PASS writable desktop operations; test directory removed')
