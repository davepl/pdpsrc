#!/usr/bin/env python3
"""Host auth regression tests. Requires impacket and pyspnego in this Python.
Run: python tests/auth_test.py [--sanitize]
All credentials/nonces are disposable test fixtures. No credential values,
challenge responses, password hashes or session keys are printed.
"""
import ctypes
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from impacket import ntlm
from impacket.spnego import SPNEGO_NegTokenResp
import spnego

root = Path(__file__).resolve().parents[1]
cc = shlex.split(os.environ.get("CC", "cc"))
flags = ["-std=c89", "-Wno-deprecated-non-prototype"]
sources = [str(root / p) for p in
           ("tests/auth_harness.c", "auth.c", "crypto.c", "wire.c")]

with tempfile.TemporaryDirectory(prefix="smbd-auth-tests-") as work:
    work = Path(work)
    exe, shared = work / "auth_harness", work / "auth_shared.so"
    sanitizers = ["-fsanitize=address,undefined", "-g"] if "--sanitize" in sys.argv else []
    subprocess.run(cc + flags + sanitizers + ["-o", str(exe)] + sources, check=True)
    subprocess.run([str(exe)], check=True)
    shared_flags = ["-dynamiclib"] if sys.platform == "darwin" else ["-shared", "-fPIC"]
    subprocess.run(cc + flags + shared_flags + ["-DAUTH_HELPERS_ONLY", "-o", str(shared)]
                   + sources, check=True)
    lib = ctypes.CDLL(str(shared))
    lib.auth_session.restype = ctypes.c_ulong
    lib.auth_session.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_void_p,
                                 ctypes.POINTER(ctypes.c_uint)]

    def exchange(blob):
        out, count = ctypes.create_string_buffer(4096), ctypes.c_uint()
        status = lib.auth_session(blob, len(blob), out, ctypes.byref(count))
        return status, out.raw[:count.value]

    def check_key(expected):
        out = ctypes.create_string_buffer(16)
        lib.test_key(out)
        assert out.raw == expected, "exported session key mismatch"

    for sign in (False, True):
        lib.test_init()
        one = ntlm.getNTLMSSPType1(signingRequired=sign)
        status, blob = exchange(one.getData())
        assert status == 0xc0000016
        two = SPNEGO_NegTokenResp(blob)["ResponseToken"]
        three, key = ntlm.getNTLMSSPType3(one, two, "User", "Password", "Domain")
        status, _ = exchange(three.getData())
        assert status == 0
        check_key(key)
    loaded = work / "credential.hash"
    loaded.write_bytes(b"A4F49C406510BDCAB6824EE7C30FD852\n")
    loaded.chmod(0o600)
    lib.test_hash_file.argtypes = [ctypes.c_char_p]
    assert lib.test_hash_file(os.fsencode(loaded)) == 1
    one = ntlm.getNTLMSSPType1(signingRequired=True)
    status, blob = exchange(one.getData())
    assert status == 0xc0000016
    two = SPNEGO_NegTokenResp(blob)["ResponseToken"]
    three, key = ntlm.getNTLMSSPType3(one, two, "User", "Password", "Domain")
    status, _ = exchange(three.getData())
    assert status == 0
    check_key(key)
    print("Independent Impacket NTLMv2 auth and exported session keys PASS, including -H")

    lib.test_init()
    one = ntlm.getNTLMSSPType1(signingRequired=True)
    _, blob = exchange(one.getData())
    two = SPNEGO_NegTokenResp(blob)["ResponseToken"]
    three, _ = ntlm.getNTLMSSPType3(one, two, "User", "wrong", "Domain")
    status, _ = exchange(three.getData())
    assert status == 0xc000006d
    # A failed authentication consumes the challenge; no second attempt.
    three, _ = ntlm.getNTLMSSPType3(one, two, "User", "Password", "Domain")
    status, _ = exchange(three.getData())
    assert status != 0
    print("Wrong-password and consumed-challenge reuse rejection PASS")

    for tamper in (False, True):
        lib.test_init()
        client = spnego.client("Domain\\User", "Password", protocol="ntlm")
        status, blob = exchange(client.step())
        assert status == 0xc0000016
        three = client.step(SPNEGO_NegTokenResp(blob)["ResponseToken"])
        if tamper:
            three = bytearray(three)
            flags = int.from_bytes(three[60:64], "little")
            mic_at = 72 if flags & 0x02000000 else 64
            three[mic_at] ^= 1
            three = bytes(three)
        status, _ = exchange(three)
        if tamper:
            assert status == 0xc000006d
        else:
            assert status == 0
            check_key(client.session_key)
    print("Independent pyspnego MIC verification and tamper rejection PASS")

    path = work / "protected-file"

    def run_file(mode):
        return subprocess.run([str(exe), mode, str(path)]).returncode

    path.write_bytes(bytes(range(1, 17)))
    path.chmod(0o600)
    assert run_file("pool") == 0
    assert path.read_bytes() == b"\0" * 8 + bytes(range(9, 17))
    assert run_file("pool") == 0
    assert path.read_bytes() == b"\0" * 16
    assert run_file("pool") != 0
    path.write_bytes(bytes(range(1, 17)))
    path.chmod(0o644)
    assert run_file("pool") != 0
    path.chmod(0o600)
    for value, valid in [(b"Password\n", True), (b"A" * 256 + b"\r\n", True),
                         (b"A" * 256 + b"\r\nTRAILER", False), (b"a\nb\n", False),
                         (b"\n", False), (b"A" * 257, False), (b"A\x00", False)]:
        path.write_bytes(value)
        assert (run_file("password") == 0) == valid
    for value, valid in [(b"a4f49c406510bdcab6824ee7c30fd852", True),
                         (b"A4F49C406510BDCAB6824EE7C30FD852\n", True),
                         (b"a4f49c406510bdcab6824ee7c30fd852\r\n", True),
                         (b"0" * 31, False), (b"0" * 33, False),
                         (b"g" * 32, False), (b"0" * 32 + b"\ntrailing", False),
                         (b"0" * 32 + b"\n\n", False), (b"0" * 32 + b"\r", False)]:
        path.write_bytes(value)
        assert (run_file("hash") == 0) == valid
    path.write_bytes(b"0" * 32)
    path.chmod(0o644)
    assert run_file("hash") != 0
    print("Protected-file modes, password/hash bounds, pool erase/restart/exhaustion PASS")
