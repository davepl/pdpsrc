#!/usr/bin/env python3
"""Optional signing still authenticates users and honors client requirements."""
import argparse
import hashlib
import hmac
import io
import struct
import time
import uuid
from impacket.smbconnection import SMBConnection, SessionError
from impacket.smb3structs import SMB2_DIALECT_002
from smbprotocol import Dialects
from smbprotocol.connection import Connection, SMB2Echo
from smbprotocol.session import Session

p = argparse.ArgumentParser()
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, default=1445)
p.add_argument('--share', default='pdp')
p.add_argument('--directory', default='')
a = p.parse_args()

def client(password='testpass', required=False):
    # Allow the native listener's one-second child reap cycle between probes.
    if a.host not in ('127.0.0.1', 'localhost', '::1'):
        time.sleep(1.1)
    c = SMBConnection(a.host, a.host, sess_port=a.port,
                      preferredDialect=SMB2_DIALECT_002, timeout=120)
    assert not c.isSigningRequired()
    s = c.getSMBServer()
    if required:
        s._Connection['RequireSigning'] = True
        send = s.sendSMB
        def setup(packet):
            if packet['Command'] == 1:
                packet['Data']['SecurityMode'] |= 2
            return send(packet)
        s.sendSMB = setup
    try:
        c.login('pdp', password)
    except BaseException:
        c.close()
        raise
    return c

try:
    bad = client('wrong-password')
except SessionError as e:
    assert e.getErrorCode() == 0xc000006d
else:
    bad.close()
    raise AssertionError('wrong password accepted')
print('PASS optional signing still rejects wrong passwords')

c = client()
try:
    s = c.getSMBServer()
    assert not s._Session['SigningActivated']
    receive = s.recvSMB
    replies = []
    def check(packet_id=None):
        reply = receive(packet_id)
        assert not reply['Flags'] & 8
        replies.append(reply['Command'])
        return reply
    s.recvSMB = check
    path = a.directory + 'smbd-unsigned-' + uuid.uuid4().hex[:12]
    payload = bytes(range(256)) * 1025
    c.putFile(a.share, path, io.BytesIO(payload).read)
    output = io.BytesIO()
    c.getFile(a.share, path, output.write)
    assert output.getvalue() == payload
    c.deleteFile(a.share, path)
    assert 8 in replies and 9 in replies
finally:
    c.close()
print('PASS password-authenticated unsigned write/read/delete; response flags checked')

def exact(sock, n):
    data = b''
    while len(data) < n:
        piece = sock.recv(n - len(data))
        if not piece:
            raise EOFError()
        data += piece
    return data

for required, signed, corrupt in ((False, True, False), (False, True, True),
                                  (True, False, False)):
    c = client(required=required)
    try:
        s = c.getSMBServer()
        sock = s.get_socket()
        packet = bytearray(68)
        packet[:4] = b'\xfeSMB'
        struct.pack_into('<H', packet, 4, 64)
        struct.pack_into('<HHI', packet, 12, 13, 1, 8 if signed else 0)
        struct.pack_into('<Q', packet, 24, s._Connection['SequenceWindow'])
        struct.pack_into('<Q', packet, 40, s._Session['SessionID'])
        struct.pack_into('<H', packet, 64, 4)
        if signed:
            packet[48:64] = hmac.new(c.getSessionKey(), packet, hashlib.sha256).digest()[:16]
        if corrupt:
            packet[48] ^= 1
        sock.sendall(struct.pack('>I', len(packet)) + packet)
        if corrupt:
            try:
                assert sock.recv(1) == b''
            except ConnectionResetError:
                pass
        else:
            reply = exact(sock, struct.unpack('>I', exact(sock, 4))[0])
            assert struct.unpack_from('<I', reply, 8)[0] == (0xc0000022 if required else 0)
            if signed:
                assert struct.unpack_from('<I', reply, 16)[0] & 8
                digest = hmac.new(c.getSessionKey(), reply[:48] + bytes(16) + reply[64:], hashlib.sha256).digest()[:16]
                assert hmac.compare_digest(digest, reply[48:64])
    finally:
        c.close()
print('PASS optional signed requests verified, corrupt signatures rejected, client-required unsigned requests denied')

if a.host not in ('127.0.0.1', 'localhost', '::1'):
    time.sleep(1.1)
c = Connection(uuid.uuid4(), a.host, port=a.port, require_signing=True)
try:
    c.connect(dialect=Dialects.SMB_2_0_2, timeout=120)
    s = Session(c, 'pdp', 'testpass', require_encryption=False)
    s.connect()
    reply = c.receive(c.send(SMB2Echo(), sid=s.session_id), timeout=120)
    assert reply['flags'].get_value() & 8
    c.verify_signature(reply, s.session_id, force=True)
finally:
    c.disconnect(close=False)
print('PASS independent signing-required client negotiates and verifies signed session')
