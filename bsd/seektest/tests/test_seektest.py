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
    output = run('ra82', '-n', '-u', '200', '-l', '100', 'ra8')
    assert 'selected cylinders 100..200 (inclusive)' in output
    assert 'READ_SECTOR' not in output
    assert 'selected cylinders 100..1398' in run('ra82', '-l', '100', '-n', 'ra8')
    assert 'selected cylinders 0..200' in run('ra82', '-n', '-u', '200', 'ra8')
    assert 'selected cylinders 625..625' in run('ra60', '-n', '-l', '625', 'ra9')
    def sectors(*args):
        return [line for line in run('ra82', *args, status=1).splitlines()
                if line.startswith('READ_SECTOR')]
    assert sectors('-l', '100', '-u', '200', 'ra8', '2') == [
        'READ_SECTOR 87000', 'READ_SECTOR 174000', 'READ_SECTOR 88740']
    assert sectors('-l', '100', '-u', '200', 'ra8', '0') == [
        'READ_SECTOR 87000', 'READ_SECTOR 174000', 'READ_SECTOR 87000']
    assert sectors('-l', '100', '-u', '100', 'ra8') == ['READ_SECTOR 87000'] * 3
    assert sectors('-l', '100', '-u', '101', 'ra8', '65535') == [
        'READ_SECTOR 87000', 'READ_SECTOR 87870', 'READ_SECTOR 87000']
    for args in [('-l',), ('-u',), ('-l', '-1', 'ra8'),
                 ('-u', '1.5', 'ra8'), ('-l', 'abc', 'ra8'),
                 ('-u', '2147483648', 'ra8'), ('-l', '', 'ra8'),
                 ('-l', '200', '-u', '100', 'ra8'),
                 ('-l', '1', '-l', '2', 'ra8'), ('-x', 'ra8')]:
        output = run('ra82', *args, status=1)
        assert 'OPEN ' not in output and 'READ_SECTOR' not in output
    for args in [('-n', '-u', '1399', 'ra8'), ('-l', '1399', 'ra8'),
                 ('-n', '-u', '626', 'ra9')]:
        fixture = 'ra60' if args[-1] == 'ra9' else 'ra82'
        assert 'READ_SECTOR' not in run(fixture, *args, status=1)
print('PASS: padded RA82 label, smaller label bound, geometry-only mode, synthetic RA60,')
print('      read-error termination, invalid labels, and 32-bit/per-read bounds')
print('PASS: default/explicit cylinder limits, bounded sweeps, single-cylinder and')
print('      full-stroke modes, malformed/overflow/reversed/out-of-disk limits')
