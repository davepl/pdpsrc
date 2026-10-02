#!/usr/bin/env python3
"""Exercise smbpwd on a private pseudo-terminal; passwords are test fixtures.
No entered password or resulting hash is printed. Standard Python only.
"""
import errno
import os
from pathlib import Path
import pty
import select
import shlex
import signal
import stat
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[1]


def interact(executable, destination, first="Password", second="Password"):
    pid, terminal = pty.fork()
    if not pid:
        os.execv(str(executable), [str(executable), str(destination)])
    output = bytearray()
    sent_first = sent_second = False
    limit = time.monotonic() + 10
    try:
        while True:
            if time.monotonic() >= limit:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
                raise AssertionError("password tool timed out")
            readable, _, _ = select.select([terminal], [], [], 0.1)
            if not readable:
                continue
            try:
                data = os.read(terminal, 4096)
            except OSError as error:
                if error.errno == errno.EIO:
                    break
                raise
            if not data:
                break
            output.extend(data)
            if not sent_first and b"New SMB password: " in output:
                os.write(terminal, first.encode("utf-8") + b"\n")
                sent_first = True
            if not sent_second and b"Retype SMB password: " in output:
                os.write(terminal, second.encode("utf-8") + b"\n")
                sent_second = True
        _, status = os.waitpid(pid, 0)
        # Check no-echo with long fixtures, avoiding coincidences in prose.
        for value in (first, second):
            if len(value) > 16:
                assert value.encode("utf-8") not in output, "terminal echoed password"
        return os.waitstatus_to_exitcode(status)
    finally:
        os.close(terminal)


with tempfile.TemporaryDirectory(prefix="smbpwd-tests-") as temporary:
    work = Path(temporary)
    executable = work / "smbpwd"
    command = shlex.split(os.environ.get("CC", "cc"))
    command += ["-std=c89", "-Wno-deprecated-non-prototype", "-o", str(executable)]
    command += [str(root / name) for name in ("smbpwd.c", "crypto.c", "wire.c")]
    subprocess.run(command, check=True)
    destination = work / "account.hash"
    assert interact(executable, destination) == 0
    assert destination.read_bytes() == b"a4f49c406510bdcab6824ee7c30fd852\n"
    assert stat.S_IMODE(destination.stat().st_mode) == 0o600
    assert interact(executable, destination, "password", "password") == 0
    assert destination.read_bytes() == b"8846f7eaee8fb117ad06bdd830b7586c\n"
    before = destination.read_bytes()
    assert interact(executable, destination, "Password-one", "Password-two") != 0
    assert destination.read_bytes() == before
    assert interact(executable, destination, "A" * 256, "A" * 256) == 0
    before = destination.read_bytes()
    assert len(before) == 33 and before[-1:] == b"\n"
    for rejected in ("", "A" * 257, "non-ASCII-\u00e9"):
        assert interact(executable, destination, rejected, rejected) != 0
        assert destination.read_bytes() == before
    alias = work / "alias.hash"
    alias.symlink_to(destination)
    assert interact(executable, alias) != 0
    alias.unlink()
    os.link(destination, alias)
    assert interact(executable, destination) != 0
    alias.unlink()
    destination.chmod(0o644)
    assert interact(executable, destination) != 0
    destination.chmod(0o600)
    unsafe = work / "public"
    unsafe.mkdir(mode=0o777)
    unsafe.chmod(0o777)
    assert interact(executable, unsafe / "account.hash") != 0
    assert not list(work.glob(".smbpwd-*.tmp")), "temporary password file leaked"
print("smbpwd: create/update, known hashes, 0600 mode, no echo, bounds/mismatch, unsafe paths PASS")
