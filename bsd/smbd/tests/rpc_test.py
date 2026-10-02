#!/usr/bin/env python3
"""Independent Impacket encoders/decoders exercise the bounded IPC$/srvsvc API."""
import argparse
import struct
from impacket.smbconnection import SMBConnection, SessionError
from impacket.smb3structs import SMB2_DIALECT_002
from impacket.dcerpc.v5 import srvs, transport, rpcrt
from impacket.dcerpc.v5.ndr import NULL
from impacket.uuid import uuidtup_to_bin

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, default=1445)
p.add_argument('--user', default='pdp')
p.add_argument('--password', default='testpass')
p.add_argument('--share', default='pdp')
a = p.parse_args()
c = SMBConnection(a.host, a.host, sess_port=a.port,
                  preferredDialect=SMB2_DIALECT_002, timeout=120)
c.login(a.user, a.password)
assert c.isSigningRequired()
shares = c.listShares()
assert {s['shi1_netname'].rstrip('\0') for s in shares} == {a.share, 'IPC$'}
d = transport.SMBTransport(a.host, a.port, r'\srvsvc', smb_connection=c).get_dce_rpc()
d.connect()
d.bind(srvs.MSRPC_UUID_SRVS)
for level in (0, 1, 2):
    result = srvs.hNetrShareEnum(d, level)
    arm = result['InfoStruct']['ShareInfo']['Level%d' % level]
    assert arm['EntriesRead'] == 2
    for share in (a.share, 'IPC$'):
        result = srvs.hNetrShareGetInfo(d, share + '\0', level)
        assert result['ErrorCode'] == 0
result = srvs.hNetrShareGetInfo(d, a.share + '\0', 1005)
assert result['ErrorCode'] == 0
try:
    srvs.hNetrShareGetInfo(d, 'nonexistent\0', 1)
    raise AssertionError('Missing share unexpectedly succeeded')
except srvs.DCERPCSessionError as error:
    assert error.get_error_code() == 2310
try:
    srvs.hNetrShareEnum(d, 502)
    raise AssertionError('Unsupported level unexpectedly succeeded')
except srvs.DCERPCSessionError as error:
    assert error.get_error_code() == 124
try:
    srvs.hNetrShareEnum(d, 1, preferedMaximumLength=0)
    raise AssertionError('Zero preferred size unexpectedly succeeded')
except srvs.DCERPCSessionError as error:
    assert error.get_error_code() == 234
    assert error.get_packet()['InfoStruct']['ShareInfo']['Level1']['EntriesRead'] == 0
result = srvs.hNetrShareEnum(d, 1, resumeHandle=1)
assert result['InfoStruct']['ShareInfo']['Level1']['EntriesRead'] == 1
assert result['InfoStruct']['ShareInfo']['Level1']['Buffer'][0]['shi1_netname'] == 'IPC$\0'
result = srvs.hNetrShareEnum(d, 1, resumeHandle=2)
assert result['InfoStruct']['ShareInfo']['Level1']['EntriesRead'] == 0
# A long server-name argument forces several connection-oriented DCE fragments.
d.set_max_fragment_size(64)
result = srvs.hNetrShareEnum(d, 1, serverName='\\\\' + 'server' * 30 + '\0')
assert result['InfoStruct']['ShareInfo']['Level1']['EntriesRead'] == 2
d.disconnect()
c.logoff()
c.close()
c = SMBConnection(a.host, a.host, sess_port=a.port,
                  preferredDialect=SMB2_DIALECT_002, timeout=120)
c.login(a.user, a.password)

# Exercise message-mode transceive and arbitrary SMB write boundaries separately.
tree = c.connectTree('IPC$')
c.waitNamedPipe(tree, 'srvsvc', timeout=1)
fid = c.openFile(tree, 'srvsvc', desiredAccess=3, shareMode=3)
bind = rpcrt.MSRPCBind()
context = rpcrt.CtxItem()
context['ContextID'] = 3
context['TransItems'] = 1
context['AbstractSyntax'] = srvs.MSRPC_UUID_SRVS
context['TransferSyntax'] = uuidtup_to_bin(('8a885d04-1ceb-11c9-9fe8-08002b104860', '2.0'))
bind.addCtxItem(context)
header = rpcrt.MSRPCHeader()
header['type'] = rpcrt.MSRPC_BIND
header['call_id'] = 101
header['pduData'] = bind.getData()
packet = header.get_packet()
c.writeNamedPipe(tree, fid, packet[:13])
c.writeNamedPipe(tree, fid, packet[13:])
ack = c.readNamedPipe(tree, fid, bytesToRead=4096)
assert rpcrt.MSRPCHeader(ack)['type'] == rpcrt.MSRPC_BINDACK
request = rpcrt.MSRPCRequestHeader()
request['call_id'] = 102
request['ctx_id'] = 3
request['op_num'] = 15
stub = srvs.NetrShareEnum()
stub['ServerName'] = '\0'
stub['InfoStruct']['Level'] = 1
stub['InfoStruct']['ShareInfo']['tag'] = 1
stub['InfoStruct']['ShareInfo']['Level1']['Buffer'] = NULL
stub['PreferedMaximumLength'] = 0xffffffff
stub['ResumeHandle'] = 0
request['pduData'] = stub.getData()
request['alloc_hint'] = len(request['pduData'])
result = c.transactNamedPipe(tree, fid, request.get_packet())
reply = rpcrt.MSRPCRespHeader(result)
assert reply['call_id'] == 102 and reply['ctx_id'] == 3
assert srvs.NetrShareEnumResponse(reply['pduData'])['ErrorCode'] == 0
request['call_id'] = 103
request['op_num'] = 999
request['pduData'] = b''
result = c.transactNamedPipe(tree, fid, request.get_packet())
assert rpcrt.MSRPCHeader(result)['type'] == rpcrt.MSRPC_FAULT
assert struct.unpack_from('<I', result, 24)[0] == 0x1c010002
request['op_num'] = 15
request['ctx_id'] = 99
result = c.transactNamedPipe(tree, fid, request.get_packet())
assert struct.unpack_from('<I', result, 24)[0] == 0x1c00001a
# A valid call after faults verifies that rejected input doesn't poison the pipe.
request['ctx_id'] = 3
request['pduData'] = stub.getData()
result = c.transactNamedPipe(tree, fid, request.get_packet())
assert srvs.NetrShareEnumResponse(rpcrt.MSRPCRespHeader(result)['pduData'])['ErrorCode'] == 0
# Peek reports queued data without consuming it.
c.writeNamedPipe(tree, fid, request.get_packet())
peek = c.getSMBServer().ioctl(tree, fid, ctlCode=0x0011400c, flags=1,
                             inputBlob=b'', maxInputResponse=0, maxOutputResponse=4096)
queued = c.readNamedPipe(tree, fid, bytesToRead=4096)
assert struct.unpack_from('<I', peek, 4)[0] == len(queued)
assert peek[16:] == queued
# Reject an impossible DCE fragment length without an allocation or crash.
malformed = bytearray(request.get_packet())
struct.pack_into('<H', malformed, 8, 65535)
result = c.transactNamedPipe(tree, fid, bytes(malformed))
assert struct.unpack_from('<I', result, 24)[0] == 0x1c01000b
result = c.transactNamedPipe(tree, fid, request.get_packet())
assert srvs.NetrShareEnumResponse(rpcrt.MSRPCRespHeader(result)['pduData'])['ErrorCode'] == 0
second = c.openFile(tree, 'SRVSVC', desiredAccess=3, shareMode=3)
try:
    c.openFile(tree, 'SrvSvc', desiredAccess=3, shareMode=3)
    raise AssertionError('Pipe handle limit was not enforced')
except SessionError as error:
    assert error.getErrorCode() == 0xc00000ae
c.closeFile(tree, second)
c.closeFile(tree, fid)
c.disconnectTree(tree)
c.logoff()
c.close()
print('PASS signed RPC: share levels 0/1/2, get-info, resume, DCE fragmentation, split SMB writes, pipe transceive/peek, handle limits, malformed lengths, faults/recovery')
