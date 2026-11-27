#!/usr/bin/env python3
"""Test the optional native arena walk without using a real allocator's headers."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    source = (ROOT / 'upstream/xmalloc.c').read_text()
    start = source.index('/* Begin allocator-ring walker')
    end = source.index('/* End allocator-ring walker. */', start)
    with tempfile.TemporaryDirectory(prefix='tmux-allocator-stats-') as temporary:
        work = Path(temporary)
        (work / 'allocator-ring.c').write_text(source[start:end])
        flags = ['-std=c89', '-O0', '-g', '-Wall', '-Wextra', '-Werror',
                 '-fsanitize=address,undefined', '-I' + str(work)]
        subprocess.run([args.cc, *flags, str(ROOT / 'tests/allocator-stats.c'),
                        '-o', str(work / 'allocator-stats')], check=True)
        environment = os.environ.copy()
        environment['ASAN_OPTIONS'] = 'symbolize=0'
        subprocess.run([str(work / 'allocator-stats')], check=True, env=environment)


if __name__ == '__main__':
    main()
