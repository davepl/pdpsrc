#!/usr/bin/env python3
"""Reject unsafe persistent-metadata paths without changing their contents."""
import hashlib
from pathlib import Path
import socket
import subprocess
import tempfile
import time


server = Path(__file__).resolve().parents[1] / 'smbd'


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


with tempfile.TemporaryDirectory(prefix='smbd-meta-startup-') as directory:
    work = Path(directory).resolve()
    share = work / 'share'
    share.mkdir()
    other_share = work / 'other-share'
    other_share.mkdir()
    password = work / 'password'
    password.write_text('testpass\n')
    password.chmod(0o600)
    metadata = work / 'metadata'
    with socket.socket() as reserved:
        reserved.bind(('127.0.0.1', 0))
        port = reserved.getsockname()[1]

    def command(path, export=share):
        return [str(server), '-a', '127.0.0.1', '-p', str(port), '-r', str(export),
                '-P', str(password), '-T', str(work), '-w', '-M', str(path)]

    with (work / 'initialize.log').open('w+') as log:
        process = subprocess.Popen(command(metadata), stderr=log)
        try:
            for unused in range(100):
                if process.poll() is not None:
                    log.seek(0)
                    raise AssertionError('valid metadata startup failed: ' + log.read())
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=.1):
                        break
                except OSError:
                    time.sleep(.05)
            else:
                raise AssertionError('valid metadata server did not start')
        finally:
            process.terminate()
            process.wait(timeout=10)

    contents = metadata.read_bytes()
    assert contents, 'metadata initializer did not write a header'

    def copy(path):
        path.write_bytes(contents)
        path.chmod(0o600)
        return path

    def rejected(label, path, watched, export=share):
        before = [(target, digest(target), target.stat().st_mode,
                   target.stat().st_size, target.stat().st_nlink) for target in watched]
        result = subprocess.run(command(path, export), capture_output=True, timeout=10)
        assert result.returncode != 0, label + ' was accepted'
        for target, checksum, mode, size, links in before:
            stat = target.stat()
            assert (digest(target), stat.st_mode, stat.st_size, stat.st_nlink) == (
                checksum, mode, size, links), label + ' changed ' + str(target)
        print('PASS metadata startup refusal:', label)

    symlink = work / 'symlink'
    symlink.symlink_to(metadata)
    rejected('symlink file', symlink, [metadata])
    assert symlink.is_symlink()

    linked = copy(work / 'linked')
    alias = work / 'linked-alias'
    alias.hardlink_to(linked)
    rejected('hardlinked file', linked, [linked, alias])

    for mode in (0o640, 0o644, 0o660, 0o666):
        public = copy(work / ('public-%o' % mode))
        public.chmod(mode)
        rejected('file mode %o' % mode, public, [public])

    for mode in (0o770, 0o777):
        parent = work / ('parent-%o' % mode)
        parent.mkdir()
        path = copy(parent / 'metadata')
        parent.chmod(mode)
        rejected('writable immediate parent %o' % mode, path, [path])

    parent_link = work / 'parent-link'
    parent_link.symlink_to(work, target_is_directory=True)
    rejected('symlink parent', parent_link / 'metadata', [metadata])
    assert parent_link.is_symlink()

    inside = copy(share / 'inside-metadata')
    rejected('path inside export', inside, [inside])
    rejected('table bound to different export', metadata, [metadata], other_share)

print('PASS protected metadata startup checks; all rejected target contents unchanged')
