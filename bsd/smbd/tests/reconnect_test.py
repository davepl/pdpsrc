#!/usr/bin/env python3
"""Authenticated PreviousSessionId replacement using independent client sessions."""
import argparse
import hashlib
import hmac
import uuid

from impacket.smbconnection import SMBConnection, SessionError
from impacket.smb3structs import SMB2_DIALECT_002


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', default='127.0.0.1')
parser.add_argument('--port', type=int, default=1445)
parser.add_argument('--share', default='pdp')
parser.add_argument('--user', default='pdp')
parser.add_argument('--password', default='testpass')
parser.add_argument('--timeout', type=int, default=60)
parser.add_argument('--writable', action='store_true')
parser.add_argument('--restart-id', type=lambda value: int(value, 0),
                    help='only verify a genuine PreviousSessionId saved across daemon restart')
a = parser.parse_args()
clients = []


def client():
    c = SMBConnection(a.host, a.host, sess_port=a.port,
                      preferredDialect=SMB2_DIALECT_002, timeout=a.timeout)
    clients.append(c)
    return c


def login(c, previous=0, password=None, before_final=None):
    server = c.getSMBServer()
    send, receive = server.sendSMB, server.recvSMB
    replies = []

    def sending(packet):
        if packet['Command'] == 1:
            packet['Data']['PreviousSessionId'] = (server._Session['SessionID']
                                                   if previous == 'self' else previous)
            if server._Session['SessionID'] and before_final:
                before_final()
        return send(packet)

    def receiving(*args, **kwargs):
        packet = receive(*args, **kwargs)
        if packet['Command'] == 1 and packet['Status'] == 0:
            replies.append(packet.getData())
        return packet

    server.sendSMB, server.recvSMB = sending, receiving
    try:
        c.login(a.user, password or a.password)
    finally:
        server.sendSMB, server.recvSMB = send, receive
    assert len(replies) == 1
    final = bytearray(replies[0])
    assert final[16] & 8, 'final authentication response is unsigned'
    signature = final[48:64]
    final[48:64] = bytes(16)
    assert hmac.compare_digest(signature, hmac.new(c.getSessionKey(), final, hashlib.sha256).digest()[:16])
    return server._Session['SessionID']


def eof(c):
    sock = c.getSMBServer().get_socket()
    sock.settimeout(2)
    assert sock.recv(1) == b'', 'replaced session transport remained open'


fixture = 'smbd-reconnect-' + uuid.uuid4().hex[:12]
cleanup = False
try:
    if a.restart_id is not None:
        c = client()
        login(c, a.restart_id)
        tree = c.connectTree(a.share)
        f = c.openFile(tree, 'hello.txt')
        assert c.readFile(tree, f, bytesToRead=1024) == b'Hello from PDP-11 SMB2!\n'
        c.closeFile(tree, f)
        print('PASS actual daemon restart: saved PreviousSessionId authenticates a signed new session and reads the share')
        raise SystemExit(0)
    old = client()
    old_id = login(old)
    old_tree = old.connectTree(a.share)
    old_file = old.openFile(old_tree, 'hello.txt', desiredAccess=0x80000000, shareMode=0)

    def old_read():
        assert old.readFile(old_tree, old_file, bytesToRead=1024) == b'Hello from PDP-11 SMB2!\n'

    failed = client()
    try:
        login(failed, old_id, a.password + '-wrong', before_final=old_read)
    except SessionError as error:
        assert error.getErrorCode() == 0xc000006d, error
    else:
        raise AssertionError('wrong password accepted during reconnect')
    eof(failed)
    old_read()
    print('PASS reconnect authorization: Type1 and failed Type3 preserve old session and handle')

    replacement = client()
    new_id = login(replacement, old_id, before_final=old_read)
    assert new_id != old_id
    eof(old)
    tree = replacement.connectTree(a.share)
    opened = replacement.openFile(tree, 'hello.txt', desiredAccess=0x80000000, shareMode=0)
    assert replacement.readFile(tree, opened, bytesToRead=1024) == b'Hello from PDP-11 SMB2!\n'
    replacement.closeFile(tree, opened)
    print('PASS active reconnect: signed new authentication closes old transport and releases exclusive handle')

    # The specification permits ignoring PreviousSessionId equal to the new
    # SessionId. This must not revoke the just-authenticated connection.
    same = client()
    login(same, 'self')
    same.connectTree(a.share)
    same.close()
    print('PASS reconnect to own new SessionId is harmless')

    if a.writable:
        doomed = replacement.createFile(tree, fixture, desiredAccess=0xc0010000,
                                         creationOption=0x1040, creationDisposition=2)
        cleanup = True
        replacement.writeFile(tree, doomed, b'delete on authenticated reconnect\n')
        final = client()
        login(final, new_id)
        eof(replacement)
        final_tree = final.connectTree(a.share)
        try:
            final.openFile(final_tree, fixture)
        except SessionError as error:
            assert error.getErrorCode() == 0xc0000034, error
        else:
            raise AssertionError('replaced session retained delete-on-close file')
        cleanup = False
        print('PASS reconnect cleanup: delete-on-close completes before new authentication is acknowledged')
finally:
    for c in clients:
        c.close()
    if cleanup:
        c = client()
        try:
            login(c)
            try:
                c.deleteFile(a.share, fixture)
            except SessionError as error:
                if error.getErrorCode() != 0xc0000034:
                    raise
        finally:
            c.close()
