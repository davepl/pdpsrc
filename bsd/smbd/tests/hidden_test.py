#!/usr/bin/env python3
"""Verify persistent HIDDEN metadata through independent signed TCP sessions.

Requires a disposable writable export. Creates and removes only unique
test names; does not change client policy or server configuration.
"""
import argparse
import contextlib
import uuid

from impacket.smbconnection import SMBConnection, SessionError
from impacket.smb3structs import SMB2_DIALECT_002
from smbprotocol import Dialects
from smbprotocol.connection import Connection
from smbprotocol.file_info import FileBasicInformation, FileRenameInformation
from smbprotocol.open import Open, SMB2QueryInfoRequest, SMB2QueryInfoResponse, SMB2SetInfoRequest
from smbprotocol.session import Session
from smbprotocol.tree import TreeConnect


p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, default=1445)
p.add_argument('--user', default='pdp')
p.add_argument('--password', default='testpass')
p.add_argument('--share', default='pdp')
p.add_argument('--timeout', type=int, default=60)
a = p.parse_args()
name = '.smbd-hidden-' + uuid.uuid4().hex[:12]
renamed = name[1:] + '-renamed'
directory_name = name + '-dir'
session_ids = set()


@contextlib.contextmanager
def connection():
    c = Connection(uuid.uuid4(), a.host, port=a.port, require_signing=True)
    c.connect(dialect=Dialects.SMB_2_0_2, timeout=a.timeout)
    try:
        s = Session(c, a.user, a.password, require_encryption=False)
        s.connect()
        assert s.session_id not in session_ids, 'transport reused a session ID'
        session_ids.add(s.session_id)
        t = TreeConnect(s, '\\\\' + a.host + '\\' + a.share)
        t.connect()
        yield c, s, t
    finally:
        c.disconnect(close=False)


def exchange(c, s, t, request):
    reply = c.receive(c.send(request, s.session_id, t.tree_connect_id), timeout=a.timeout)
    assert reply['flags'].get_value() & 8
    c.verify_signature(reply, s.session_id, force=True)
    return reply


def set_info(c, s, t, f, value):
    request = SMB2SetInfoRequest()
    request['info_type'] = value.INFO_TYPE
    request['file_info_class'] = value.INFO_CLASS
    request['file_id'] = f.file_id
    request['buffer'] = value.pack()
    exchange(c, s, t, request)


def attributes(c, s, t, f):
    request = SMB2QueryInfoRequest()
    request['info_type'] = 1
    request['file_info_class'] = 4
    request['output_buffer_length'] = 40
    request['file_id'] = f.file_id
    body = SMB2QueryInfoResponse()
    body.unpack(exchange(c, s, t, request)['data'].get_value())
    info = FileBasicInformation()
    info.unpack(body['buffer'].get_value())
    return info['file_attributes'].get_value()


def directory_attributes(t, target):
    directory = Open(t, '')
    directory.create(2, 1, 16, 7, 1, 1)
    try:
        entries = directory.query_directory(target, 37, flags=1)
        assert len(entries) == 1
        assert entries[0]['file_name'].get_value().decode('utf-16-le') == target
        return entries[0]['file_attributes'].get_value()
    finally:
        directory.close()


def set_attributes(c, s, t, f, flags):
    value = FileBasicInformation()
    value['file_attributes'] = flags
    set_info(c, s, t, f, value)


try:
    with connection() as (c, s, t):
        f = Open(t, name)
        f.create(2, 0xc0010100, 2, 7, 2, 64)
        assert attributes(c, s, t, f) == 0x22
        set_attributes(c, s, t, f, 2)
        assert attributes(c, s, t, f) == 2
        f.write(b'hidden persistence\n')
        f.flush()
        assert attributes(c, s, t, f) == 0x22
        value = FileRenameInformation()
        value['file_name'] = renamed
        set_info(c, s, t, f, value)
        assert attributes(c, s, t, f) == 0x22
        f.close()
        d = Open(t, directory_name)
        d.create(2, 0xc0010100, 0x12, 7, 2, 1)
        assert attributes(c, s, t, d) == 0x12
        d.close()

    with connection() as (c, s, t):
        f = Open(t, renamed)
        f.create(2, 0xc0010100, 128, 7, 1, 64)
        assert attributes(c, s, t, f) == 0x22
        assert directory_attributes(t, renamed) == 0x22
        set_attributes(c, s, t, f, 0x23)
        assert attributes(c, s, t, f) == 0x23
        set_attributes(c, s, t, f, 0x80)
        assert attributes(c, s, t, f) == 0x80
        f.close()
        d = Open(t, directory_name)
        d.create(2, 0xc0010100, 16, 7, 1, 1)
        assert attributes(c, s, t, d) == 0x12
        assert directory_attributes(t, directory_name) == 0x12
        set_attributes(c, s, t, d, 16)
        assert attributes(c, s, t, d) == 16
        d.close()

    with connection() as (c, s, t):
        f = Open(t, renamed)
        f.create(2, 0x80000000, 128, 7, 1, 64)
        assert attributes(c, s, t, f) == 0x80
        assert f.read(0, 19) == b'hidden persistence\n'
        f.close()
        assert directory_attributes(t, renamed) == 0x80
        assert directory_attributes(t, directory_name) == 16
    print('PASS persistent HIDDEN: create files/directories, WRITE preserves it, '
          'rename/fresh sessions/enumeration agree, readonly/archive remain independent, '
          'explicit clearing persists even for dot directories')
finally:
    cleanup = SMBConnection(a.host, a.host, sess_port=a.port,
                            preferredDialect=SMB2_DIALECT_002, timeout=a.timeout)
    try:
        cleanup.login(a.user, a.password)
        for target in (name, renamed):
            try:
                cleanup.deleteFile(a.share, target)
            except SessionError as error:
                if error.getErrorCode() != 0xc0000034:
                    raise
        try:
            cleanup.deleteDirectory(a.share, directory_name)
        except SessionError as error:
            if error.getErrorCode() != 0xc0000034:
                raise
    finally:
        cleanup.close()
