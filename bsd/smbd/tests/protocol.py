#!/usr/bin/env python3
"""Independent signed compounds, wire rejection, and directory continuation.

Requires the same disposable authenticated server/fixture as clients.py and
the test-only Impacket and smbprotocol packages. --fixture-root optionally
creates and removes a separate directory of 200 files on the local host.
No client security setting is changed.
"""
import argparse
import contextlib
import hashlib
import hmac
import pathlib
import socket
import struct
import time
import uuid

from impacket.smbconnection import SMBConnection, SessionError
from impacket.smb3structs import SMB2_DIALECT_002
from smbprotocol import Dialects
from smbprotocol.connection import Connection, SMB2Echo
from smbprotocol.exceptions import NoMoreFiles, SMBResponseException
from smbprotocol.open import (Open, QueryDirectoryFlags, SMB2ReadResponse,
                              SMB2QueryInfoRequest, SMB2QueryInfoResponse)
from smbprotocol.session import Session
from smbprotocol.tree import TreeConnect


parser = argparse.ArgumentParser()
parser.add_argument('--host', default='127.0.0.1')
parser.add_argument('--port', type=int, default=1445)
parser.add_argument('--share', default='pdp')
parser.add_argument('--user', default='pdp')
parser.add_argument('--password', default='testpass')
parser.add_argument('--timeout', type=int, default=60)
parser.add_argument('--fixture-root', type=pathlib.Path)
parser.add_argument('--auth-recovery-only', action='store_true',
                    help='only verify failed-login EOF and fresh signed login; use a -c 1 server to check slot reuse')
args = parser.parse_args()


def initial_session_id():
    client = SMBConnection(args.host, args.host, sess_port=args.port,
                           preferredDialect=SMB2_DIALECT_002, timeout=args.timeout)
    server = client.getSMBServer()
    send = server.sendSMB
    try:
        server._Session['SessionID'] = 0x123456789abcdef0
        try:
            client.login(args.user, args.password)
        except SessionError as error:
            assert error.getErrorCode() == 0xc0000203, error
        else:
            raise AssertionError('unknown initial header SessionId accepted')
        server._Session['SessionID'] = 0

        def previous_session(packet):
            if packet['Command'] == 1:
                packet['Data']['PreviousSessionId'] = 0x123456789abcdef0
            return send(packet)
        server.sendSMB = previous_session
        # An unknown previous ID (including one from before a daemon restart)
        # needs no cleanup and must not prevent normal authentication.
        client.login(args.user, args.password)
        assert server._Session['SessionID'] != 0
        client.connectTree(args.share)
    finally:
        server.sendSMB = send
        client.close()


def authentication_recovery():
    # Keep the rejected client's socket open: client cleanup must not be what
    # releases the server's worker. Impacket consumes the full error response.
    rejected = SMBConnection(args.host, args.host, sess_port=args.port,
                             preferredDialect=SMB2_DIALECT_002, timeout=args.timeout)
    sock = rejected.getSMBServer().get_socket()
    try:
        try:
            rejected.login(args.user, args.password + '-deliberately-wrong')
        except SessionError as error:
            assert error.getErrorCode() == 0xc000006d, error
        else:
            raise AssertionError('wrong password accepted')
        sock.settimeout(2)
        assert sock.recv(1) == b'', 'failed authentication did not close the transport'

        # A child can exit just after the listener's reap pass. Allow that
        # bounded scheduling race. The EOF check above, rather than handshake
        # speed on the PDP-11, proves the failed worker was released promptly.
        deadline = time.monotonic() + max(5, args.timeout)
        while True:
            connection = Connection(uuid.uuid4(), args.host, port=args.port,
                                    require_signing=True)
            try:
                connection.connect(dialect=Dialects.SMB_2_0_2, timeout=args.timeout)
                session = Session(connection, args.user, args.password, require_encryption=False)
                session.connect()
                request = connection.send(SMB2Echo(), sid=session.session_id)
                reply = connection.receive(request, timeout=args.timeout)
                assert reply['flags'].get_value() & 8, 'fresh session response is unsigned'
                connection.verify_signature(reply, session.session_id, force=True)
                break
            except Exception:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.05)
            finally:
                connection.disconnect(close=False)
    finally:
        sock.close()


initial_session_id()
print('PASS session IDs: unknown initial header ID rejected; unknown PreviousSessionId '
      'authenticates and connects normally')
if args.auth_recovery_only:
    # The focused mode also runs against a one-worker listener. Retire the
    # preceding valid test session before testing failed-login slot release.
    time.sleep(1.1)
authentication_recovery()
print('PASS authentication recovery: complete LOGON_FAILURE followed by EOF; '
      'fresh signed login succeeds while rejected client remains open')
if args.auth_recovery_only:
    raise SystemExit(0)


@contextlib.contextmanager
def independent_connection():
    connection = Connection(uuid.uuid4(), args.host, port=args.port, require_signing=True)
    connection.connect(dialect=Dialects.SMB_2_0_2, timeout=args.timeout)
    try:
        session = Session(connection, args.user, args.password, require_encryption=False)
        session.connect()
        tree = TreeConnect(session, '\\\\' + args.host + '\\' + args.share)
        tree.connect()
        yield connection, session, tree
    finally:
        connection.disconnect(close=False)


def create_request(opened):
    return opened.create(2, 0x80000000, 128, 7, 1, 64, send=False)[0]


with independent_connection() as (connection, session, tree):
    connection.echo(session.session_id, timeout=args.timeout, credit_request=8)
    for name, size, expected in [
        ('hello.txt', 1024, b'Hello from PDP-11 SMB2!\n'),
        ('nested\\binary file.bin', 65536, bytes(range(256)) * 256),
    ]:
        opened = Open(tree, name)
        messages = [create_request(opened), opened.read(0, size, send=False)[0],
                    opened.close(send=False)[0]]
        requests = connection.send_compound(messages, session.session_id,
                                            tree.tree_connect_id, related=True)
        replies = [connection.receive(r, timeout=args.timeout) for r in requests]
        for reply in replies:
            assert reply['flags'].get_value() & 8, 'missing response signature'
            connection.verify_signature(reply, session.session_id, force=True)
        read = SMB2ReadResponse()
        read.unpack(replies[1]['data'].get_value())
        assert read['buffer'].get_value() == expected

    opened = Open(tree, 'hello.txt')
    query = SMB2QueryInfoRequest()
    query['info_type'] = 1
    query['file_info_class'] = 5
    query['output_buffer_length'] = 24
    query['file_id'] = b'\xff' * 16
    requests = connection.send_compound(
        [create_request(opened), query, opened.close(send=False)[0]],
        session.session_id, tree.tree_connect_id, related=True)
    replies = [connection.receive(r, timeout=args.timeout) for r in requests]
    for reply in replies:
        assert reply['flags'].get_value() & 8
        connection.verify_signature(reply, session.session_id, force=True)
    information = SMB2QueryInfoResponse()
    information.unpack(replies[1]['data'].get_value())
    assert struct.unpack_from('<Q', information['buffer'].get_value(), 8)[0] == len(b'Hello from PDP-11 SMB2!\n')
print('PASS smbprotocol: related CREATE/READ/CLOSE (small and 65536 bytes), '
      'CREATE/QUERY_INFO/CLOSE; every response signature verified')

with independent_connection() as (connection, session, tree):
    opened = Open(tree, 'nested\\binary file.bin')
    opened.create(2, 0x80000000, 128, 7, 1, 64)
    connection.echo(session.session_id, timeout=args.timeout, credit_request=8)
    requests = connection.send_compound(
        [opened.read(0, 65536, send=False)[0] for _ in range(8)],
        session.session_id, tree.tree_connect_id)
    replies = [connection.receive(request, timeout=args.timeout) for request in requests]
    assert len(replies) == 8
    for reply in replies:
        assert reply['flags'].get_value() & 8
        connection.verify_signature(reply, session.session_id, force=True)
        read = SMB2ReadResponse()
        read.unpack(reply['data'].get_value())
        assert read['buffer'].get_value() == bytes(range(256)) * 256
    opened.close()
print('PASS maximum compound: eight signed 65536-byte reads; 524928-byte response verified')

with independent_connection() as (connection, session, tree):
    opened = Open(tree, 'hello.txt')
    opened.create(2, 0x80000000, 128, 7, 1, 64)
    second = TreeConnect(session, '\\\\' + args.host + '\\' + args.share)
    second.connect()
    assert second.tree_connect_id != tree.tree_connect_id
    request = connection.send(opened.read(0, 1024, send=False)[0],
                              session.session_id, second.tree_connect_id)
    try:
        connection.receive(request, timeout=args.timeout)
    except SMBResponseException as error:
        assert error.status == 0xc0000008, error
    else:
        raise AssertionError('file ID accepted on the wrong tree')
    second.disconnect()
    assert opened.read(0, 1024) == b'Hello from PDP-11 SMB2!\n'
    opened.close()
print('PASS tree isolation: wrong-tree handle denied; temporary tree disconnect preserves open file')


def impacket_login(password):
    client = SMBConnection(args.host, args.host, sess_port=args.port,
                           preferredDialect=SMB2_DIALECT_002, timeout=args.timeout)
    try:
        client.login(args.user, password)
    except Exception:
        client.close()
        raise
    return client


class RawSession:
    """Use Impacket only to authenticate, then test framing independently."""
    def __enter__(self):
        self.client = impacket_login(args.password)
        self.tree = self.client.connectTree(args.share)
        server = self.client.getSMBServer()
        self.sock = server.get_socket()
        self.sock.settimeout(args.timeout)
        self.key = self.client.getSessionKey()
        self.sid = server._Session['SessionID']
        self.mid = server._Connection['SequenceWindow']
        return self

    def __exit__(self, *unused):
        self.sock.close()

    def packet(self, command, body, *, signed=True, next_command=0, mid=None):
        header = bytearray(64)
        header[:4] = b'\xfeSMB'
        struct.pack_into('<H', header, 4, 64)
        struct.pack_into('<HH', header, 12, command, 1)
        struct.pack_into('<II', header, 16, 8 if signed else 0, next_command)
        struct.pack_into('<Q', header, 24, self.mid if mid is None else mid)
        struct.pack_into('<I', header, 36, self.tree)
        struct.pack_into('<Q', header, 40, self.sid)
        data = header + body
        if signed:
            data[48:64] = hmac.new(self.key, data, hashlib.sha256).digest()[:16]
        return bytes(data)

    def send(self, data, fragmented=False):
        framed = struct.pack('>I', len(data)) + data
        if fragmented:
            # Fragment the length prefix and SMB header/body at distinct writes.
            for i in range(4):
                self.sock.sendall(framed[i:i+1])
                time.sleep(0.005)
            for i in range(4, len(framed), 7):
                self.sock.sendall(framed[i:i+7])
                time.sleep(0.001)
        else:
            self.sock.sendall(framed)
        self.mid += 1

    def exact(self, length):
        data = b''
        while len(data) < length:
            chunk = self.sock.recv(length - len(data))
            if not chunk:
                raise EOFError('server closed connection')
            data += chunk
        return data

    def receive(self, status=0, verify=True):
        length = struct.unpack('>I', self.exact(4))[0]
        assert 64 <= length <= 524288, length
        reply = self.exact(length)
        assert reply[:4] == b'\xfeSMB'
        assert struct.unpack_from('<I', reply, 8)[0] == status, reply[:24].hex()
        if verify:
            assert struct.unpack_from('<I', reply, 16)[0] & 8, 'unsigned response'
            calculated = hmac.new(self.key, reply[:48] + b'\0' * 16 + reply[64:],
                                  hashlib.sha256).digest()[:16]
            assert hmac.compare_digest(reply[48:64], calculated), 'invalid response signature'
        return reply

    def closed(self):
        try:
            data = self.sock.recv(1)
        except (ConnectionResetError, ConnectionAbortedError):
            data = b''
        assert data == b'', 'malformed request was not disconnected'


echo = struct.pack('<HH', 4, 0)
with RawSession() as session:
    session.send(session.packet(13, echo), fragmented=True)
    session.receive()
print('PASS transport: fragmented TCP length, SMB header, and body with verified signature')

with RawSession() as session:
    request = bytearray(session.packet(13, echo))
    request[48] ^= 1
    session.send(request)
    session.closed()
print('PASS signing: invalid request signature disconnected')

with RawSession() as session:
    session.send(session.packet(13, echo, signed=False))
    # ACCESS_DENIED is required. An unsigned rejection carries no trusted data.
    session.receive(status=0xc0000022, verify=False)
print('PASS signing: unsigned authenticated request denied')

with RawSession() as session:
    request = session.packet(13, echo)
    session.send(request)
    session.receive()
    session.send(request)
    session.closed()
print('PASS replay: previously accepted signed message ID disconnected')

for invalid_next in [1, 64, 65, 0xfffffff8]:
    with RawSession() as session:
        session.send(session.packet(13, echo, next_command=invalid_next))
        session.closed()
print('PASS compound validation: short, missing, misaligned, and oversized NextCommand rejected')

with RawSession() as session:
    body = bytearray(56)
    struct.pack_into('<H', body, 0, 57)
    struct.pack_into('<I', body, 24, 0x80000000)
    struct.pack_into('<I', body, 36, 1)
    struct.pack_into('<HH', body, 44, 65535, 2)
    session.send(session.packet(5, body))
    session.receive(status=0xc000000d)
    # Invalid request must leave the session usable at the next valid ID.
    session.send(session.packet(13, echo))
    session.receive()
print('PASS request bounds: signed CREATE with invalid name offset denied; session remains usable')

for length in [63, 524289]:
    with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
        sock.sendall(struct.pack('>I', length))
        try:
            result = sock.recv(1)
        except ConnectionResetError:
            result = b''
        assert result == b'', length
print('PASS frame bounds: too-small and oversized direct TCP frame lengths disconnected')


if args.fixture_root:
    fixture = args.fixture_root / ('protocol-many-' + uuid.uuid4().hex[:8])
    expected = {'entry-%04d.txt' % i for i in range(200)}
    fixture.mkdir(mode=0o755)
    try:
        for name in expected:
            (fixture / name).write_bytes(b'')
        with independent_connection() as (connection, session, tree):
            opened = Open(tree, fixture.name)
            opened.create(2, 1, 16, 7, 1, 1)
            entries = []
            pages = 0
            first = []
            while True:
                try:
                    batch = opened.query_directory('*' if not pages else '', 37,
                                                    flags=1 if not pages else 0,
                                                    max_output=1024)
                except NoMoreFiles:
                    break
                names = [item['file_name'].get_value().decode('utf-16-le') for item in batch]
                if not pages:
                    first = names
                entries.extend(names)
                pages += 1
            assert pages > 1 and len(entries) == 200 and set(entries) == expected
            batch = opened.query_directory('*', 37, flags=1, max_output=1024)
            assert [item['file_name'].get_value().decode('utf-16-le') for item in batch] == first
            batch = opened.query_directory(
                'entry-0000.txt', 37,
                flags=QueryDirectoryFlags.SMB2_REOPEN | QueryDirectoryFlags.SMB2_RESTART_SCANS,
                max_output=1024)
            assert [item['file_name'].get_value().decode('utf-16-le') for item in batch] == ['entry-0000.txt']
            opened.close()
        print('PASS directory continuation: 200 distinct entries over %d signed pages; restart and REOPEN verified' % pages)
    finally:
        for name in expected:
            (fixture / name).unlink(missing_ok=True)
        fixture.rmdir()
else:
    print('SKIP 200-file continuation fixture: use --fixture-root for a local disposable export')

print('All protocol tests passed')
