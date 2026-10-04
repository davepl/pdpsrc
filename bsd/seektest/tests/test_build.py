#!/usr/bin/env python3
"""Exercise the real Makefile compiler selection without native compilation."""
from pathlib import Path
import shutil
import subprocess
import tempfile

source = Path(__file__).resolve().parent.parent

def executable(path, body='#!/bin/sh\nexit 0\n'):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(body)
    path.chmod(0o755)

for mode, expected in [('stock', ['-t1p', '-B../tools/']),
                       ('prepared', ['-t01p', '-B./compiler/']),
                       ('sibling', ['-t01p', '-B./compiler/']),
                       ('override', ['-custom'])]:
    with tempfile.TemporaryDirectory(prefix='seektest-build-') as tmp:
        root = Path(tmp)
        project = root / 'seektest'
        project.mkdir()
        for name in ['Makefile', 'seektest.c', 'build-mentec.sh', 'compile.sh']:
            shutil.copy2(source / name, project / name)
        executable(root / 'cc', '#!/bin/sh\nprintf "%s\\n" "$@" > selected-flags\ntouch seektest\n')
        if mode in ('prepared', 'override'):
            executable(project / 'compiler/c0')
            executable(project / 'compiler/c1')
        if mode == 'sibling':
            executable(root / 'ksh/native-tools/c0')
            executable(root / 'ksh/native-tools/c1')
            executable(root / 'tools/cpp')
        args = ['make', 'CC=' + str(root / 'cc')]
        if mode == 'override':
            args += ['PDPFLAGS=-custom']
        result = subprocess.run(args, cwd=project, text=True, capture_output=True)
        assert result.returncode == 0, result.stdout + result.stderr
        flags = (project / 'selected-flags').read_text().splitlines()
        assert flags[:len(expected)] == expected, (mode, flags)
        assert (project / 'seektest').exists()
        print('PASS compiler selection:', mode)
