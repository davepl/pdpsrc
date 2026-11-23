#!/usr/bin/env python3
"""Independent SMB2 multiaccount acceptance check. Never prints credentials.

JSON input: {"accounts": {"dave": {"password": "..."},
                          "root": {"password": "..."}}}
The accounts values may also be password strings. Optional JSON keys host,
port, share supply defaults. For per-UID tests, --fixture is a share-relative
directory containing dave-only (0600 dave), root-only (0600 root), dave-writes
(0700 dave), and root-writes (0700 root). Test files are unique and cleaned.
"""
import argparse
import contextlib
import hashlib
import hmac
import io
import json
from pathlib import Path
import stat
import uuid

from impacket.smbconnection import SMBConnection, SessionError
from impacket.smb3structs import SMB2_DIALECT_002


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--credentials', type=Path, required=True)
parser.add_argument('--host')
parser.add_argument('--port', type=int)
parser.add_argument('--share')
parser.add_argument('--timeout', type=int, default=120)
parser.add_argument('--fixture', help='share-relative precreated permission fixtures')
parser.add_argument('--writable', help='share-relative shared writable directory for host tests')
parser.add_argument('--reconnect-path', default='', help='readable share-relative file used for reconnect tests')
parser.add_argument('--smoke-only', action='store_true', help='only signed logins and permission-fixture or shared-file reads')
parser.add_argument('--cross-account-status', type=lambda s: int(s, 0), default=0xc0000022)
args = parser.parse_args()
if args.smoke_only and not (args.fixture or args.reconnect_path):
    parser.error('--smoke-only requires --fixture or --reconnect-path')
if stat.S_IMODE(args.credentials.stat().st_mode) & 0o077:
    raise SystemExit('Credential JSON must be private (mode 0600).')
settings = json.loads(args.credentials.read_text())
credentials = settings.get('accounts', settings.get('users', settings))
passwords = {
    name: (value['password'] if isinstance(value, dict) else value)
    for name, value in credentials.items() if name in ('dave', 'root')
}
assert set(passwords) == {'dave', 'root'}, 'dave and root credentials required'
assert passwords['dave'] != passwords['root'], 'use distinct account passwords'
host = args.host or settings.get('host', settings.get('server', '127.0.0.1'))
port = args.port or settings.get('port', 1445)
share = args.share or settings.get('share', 'pdp')
clients = []
created = []


def new_client():
    client = SMBConnection(host, host, sess_port=port,
                           preferredDialect=SMB2_DIALECT_002, timeout=args.timeout)
    clients.append(client)
    return client


def login(client, user, password=None, previous=0, before_final=None):
    server = client.getSMBServer()
    send, receive = server.sendSMB, server.recvSMB
    final_replies = []

    def sending(packet):
        if packet['Command'] == 1:
            packet['Data']['PreviousSessionId'] = previous
            if server._Session['SessionID'] and before_final:
                before_final()
        return send(packet)

    def receiving(*positional, **keywords):
        packet = receive(*positional, **keywords)
        if packet['Command'] == 1 and packet['Status'] == 0:
            final_replies.append(packet.getData())
        return packet

    server.sendSMB, server.recvSMB = sending, receiving
    try:
        client.login(user, passwords[user] if password is None else password)
    finally:
        server.sendSMB, server.recvSMB = send, receive
    assert client.getDialect() == SMB2_DIALECT_002
    assert client.isSigningRequired(), 'server did not require message signing'
    assert len(final_replies) == 1
    final = bytearray(final_replies[0])
    assert final[16] & 8, 'final authentication response is unsigned'
    signature = final[48:64]
    final[48:64] = bytes(16)
    expected = hmac.new(client.getSessionKey(), final, hashlib.sha256).digest()[:16]
    assert hmac.compare_digest(signature, expected), 'invalid authentication signature'
    return server._Session['SessionID']


def expect_error(statuses, action):
    if isinstance(statuses, int):
        statuses = (statuses,)
    try:
        action()
    except SessionError as error:
        actual = error.getErrorCode()
        assert actual in statuses, 'unexpected SMB status ' + hex(actual)
    else:
        raise AssertionError('operation unexpectedly succeeded')


def eof(client):
    sock = client.getSMBServer().get_socket()
    sock.settimeout(5)
    assert sock.recv(1) == b'', 'retired or rejected session transport remained open'


def read(client, name):
    result = io.BytesIO()
    client.getFile(share, name, result.write)
    return result.getvalue()


def joined(directory, name):
    return directory.rstrip('/\\') + '/' + name


def write_read_delete(client, directory, user):
    path = joined(directory, 'smbcheck-' + user + '-' + uuid.uuid4().hex[:10])
    payload = b'SMB2 account ' + user.encode() + b'\n' + bytes(range(256)) * 16
    client.putFile(share, path, io.BytesIO(payload).read)
    created.append((user, path))
    assert read(client, path) == payload, 'write/read byte comparison failed'
    client.deleteFile(share, path)
    created.remove((user, path))


try:
    active = {}
    for user in ('dave', 'root'):
        client = new_client()
        login(client, user)
        client.connectTree(share)
        active[user] = client
        print('PASS ' + user + ': distinct NTLMv2 account, signed authentication, share connection')

    if not args.smoke_only:
        for user, wrong in (
            ('dave', passwords['dave'] + '-wrong'),
            ('root', passwords['root'] + '-wrong'),
            ('dave', passwords['root']),
            ('root', passwords['dave']),
            ('unconfigured-account', passwords['root']),
        ):
            rejected = new_client()
            expect_error(0xc000006d, lambda: login(rejected, user, wrong))
            eof(rejected)
            rejected.close()
        print('PASS authentication isolation: wrong, swapped, and unknown-account credentials rejected with EOF')

    if args.fixture:
        dave_path = joined(args.fixture, 'dave-only')
        root_path = joined(args.fixture, 'root-only')
        assert read(active['dave'], dave_path), 'dave permission fixture is empty'
        assert read(active['root'], root_path), 'root permission fixture is empty'
        assert read(active['root'], dave_path), 'root cannot read dave fixture'
        expect_error(0xc0000022, lambda: read(active['dave'], root_path))
        if args.smoke_only:
            print('PASS permission reads: dave reads own 0600 file, root reads both, dave denied root file')
            print('PASS signed multiaccount post-restart smoke checks complete')
            raise SystemExit(0)
        denied_name = joined(args.fixture, 'root-writes/denied-' + uuid.uuid4().hex[:10])
        expect_error(0xc0000022, lambda: active['dave'].putFile(share, denied_name, io.BytesIO(b'denied').read))
        print('PASS Unix permissions: dave reads own 0600 file, root reads both, dave denied root file and 0700 directory')
        for user in ('dave', 'root'):
            write_read_delete(active[user], joined(args.fixture, user + '-writes'), user)
            print('PASS ' + user + ': create, signed write/read, byte comparison, deletion in own 0700 directory')
        reconnect_path = args.reconnect_path or dave_path
    else:
        reconnect_path = args.reconnect_path
        if args.smoke_only:
            for user in ('dave', 'root'):
                assert read(active[user], reconnect_path), 'shared read fixture is empty'
            print('PASS signed multiaccount shared-file smoke checks complete')
            raise SystemExit(0)
        if args.writable:
            for user in ('dave', 'root'):
                write_read_delete(active[user], args.writable, user)
            print('PASS both accounts: unique create, signed write/read byte comparison, deletion')

    if reconnect_path:
        # Free idle sessions so the reconnect sequence also works with c=4.
        for client in active.values():
            client.logoff()
            client.close()
        old = new_client()
        old_id = login(old, 'dave')
        old_tree = old.connectTree(share)
        old_file = old.openFile(old_tree, reconnect_path, desiredAccess=0x80000000, shareMode=0)
        before = old.readFile(old_tree, old_file, bytesToRead=1024)

        def old_read():
            assert old.readFile(old_tree, old_file, bytesToRead=1024) == before

        failed = new_client()
        expect_error(0xc000006d, lambda: login(failed, 'dave', passwords['dave'] + '-wrong',
                                              previous=old_id, before_final=old_read))
        eof(failed)
        failed.close()
        old_read()
        other = new_client()
        expect_error(args.cross_account_status, lambda: login(other, 'root', previous=old_id,
                                                              before_final=old_read))
        eof(other)
        other.close()
        old_read()
        print('PASS reconnect isolation: wrong password and authenticated other account preserve original session and exclusive handle')

        replacement = new_client()
        replacement_id = login(replacement, 'dave', previous=old_id, before_final=old_read)
        assert replacement_id != old_id
        eof(old)
        tree = replacement.connectTree(share)
        opened = replacement.openFile(tree, reconnect_path, desiredAccess=0x80000000, shareMode=0)
        assert replacement.readFile(tree, opened, bytesToRead=1024) == before
        replacement.closeFile(tree, opened)
        print('PASS same-account reconnect: signed new session closes original transport and releases exclusive handle')
finally:
    for client in clients:
        with contextlib.suppress(Exception):
            client.close()
    for user, path in created:
        cleaner = new_client()
        try:
            login(cleaner, user)
            cleaner.deleteFile(share, path)
        finally:
            cleaner.close()
print('PASS independent multiaccount acceptance checks complete')
