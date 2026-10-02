#!/usr/bin/env python3
"""Signed credit-window saturation and replenishment against a -C 8 server."""
import argparse
import uuid
from smbprotocol import Dialects
from smbprotocol.connection import Connection, SMB2Echo
from smbprotocol.session import Session

p = argparse.ArgumentParser()
p.add_argument('--port', type=int, default=1445)
a = p.parse_args()
c = Connection(uuid.uuid4(), '127.0.0.1', port=a.port, require_signing=True)
try:
    c.connect(dialect=Dialects.SMB_2_0_2)
    s = Session(c, 'pdp', 'testpass', require_encryption=False)
    s.connect()
    c.echo(s.session_id, credit_request=32)
    for _ in range(3):
        assert c.sequence_window['high'] - c.sequence_window['low'] == 8
        requests = [c.send(SMB2Echo(), sid=s.session_id, credit_request=32)
                    for _ in range(8)]
        for request in requests:
            reply = c.receive(request)
            assert reply['credit_response'].get_value() == 1
            assert reply['flags'].get_value() & 8
            c.verify_signature(reply, s.session_id, force=True)
    assert c.sequence_window['high'] - c.sequence_window['low'] == 8
finally:
    c.disconnect(close=False)
print('PASS eight-credit window: saturation, signed replies, replenishment, cap')
