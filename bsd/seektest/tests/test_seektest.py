#!/usr/bin/env python3
"""Run the actual utility with simulated disk labels and read failures."""
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='seektest-check-') as scratch:
    scratch = Path(scratch)
    (scratch / 'sys').mkdir()
    (scratch / 'sys/disklabel.h').write_bytes((here / 'mock_disklabel.h').read_bytes())
    exe = scratch / 'seektest'
    subprocess.run(['cc', '-std=gnu89', '-Wno-deprecated-non-prototype',
                    '-I', str(scratch), str(here / 'mock_device.c'), '-o', str(exe)], check=True)

    def run(fixture, *args, status=0):
        result = subprocess.run([str(exe), *args], env=dict(os.environ, SEEKTEST_FIXTURE=fixture),
                                text=True, capture_output=True)
        assert result.returncode == status, result.stdout + result.stderr
        return result.stdout + result.stderr

    output = run('ra82', '-n', 'ra8')
    assert 'limiting sweep to 1216665 sectors' in output and '/dev/rra8c: 1216665' in output
    assert 'READ_SECTOR' not in output
    output = run('ra82', 'ra8', status=1)
    assert [line for line in output.splitlines() if line.startswith('READ_SECTOR')] == [
        'READ_SECTOR 0', 'READ_SECTOR 1216260', 'READ_SECTOR 870']
    output = run('ra60', 'ra9', status=1)
    assert [line for line in output.splitlines() if line.startswith('READ_SECTOR')] == [
        'READ_SECTOR 0', 'READ_SECTOR 400000']
    assert 'at cylinder 625, byte offset 204800000' in output
    assert '/dev/rra8c: 1000000' in run('label-smaller', '-n', 'ra8')
    for fixture in ('overflow', 'no-zero-offset', 'bad-label'):
        assert 'READ_SECTOR' not in run(fixture, 'ra8', status=1)
    assert 'PASS per-read bounds' in run('guard')
print('PASS: padded RA82 label, smaller label bound, geometry-only mode, synthetic RA60,')
print('      read-error termination, invalid labels, and 32-bit/per-read bounds')
