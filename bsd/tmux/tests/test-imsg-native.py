#!/usr/bin/env python3
"""Compile native imsg branches in a temporary host shim; never contact Minerva."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='tmux-imsg-regression-') as temporary:
        work = Path(temporary)
        for name in ['imsg.c', 'imsg-buffer.c', 'imsg.h', 'queue.h']:
            shutil.copy2(ROOT / 'upstream/compat' / name, work / name)
        (work / 'tmux.h').write_text('''#ifndef IMSG_TEST_TMUX_H
#define IMSG_TEST_TMUX_H
#include <sys/types.h>
#include <sys/uio.h>
#include <stdint.h>
#include <limits.h>
#ifndef IOV_MAX
#define IOV_MAX 64
#endif
#include "queue.h"
#include "imsg.h"
#endif
''')
        (work / 'redirect.h').write_text('''#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define TMUX_NATIVE_211BSD 1
#undef INT_MAX
#define INT_MAX 32767
#define read test_read
#define writev test_writev
#define close test_close
#define sendmsg forbidden_sendmsg
#define recvmsg forbidden_recvmsg
ssize_t test_read(int, void *, size_t);
ssize_t test_writev(int, const struct iovec *, int);
int test_close(int);
''')
        flags = ['-O0', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-pointer-sign', '-iquote.']
        for source in ['imsg.c', 'imsg-buffer.c']:
            subprocess.run([args.cc, *flags, '-include', 'redirect.h', '-c', source,
                            '-o', source[:-2] + '.o'], cwd=work, check=True)
        undefined = subprocess.check_output(['nm', '-u', 'imsg.o', 'imsg-buffer.o'],
                                            cwd=work, text=True)
        if re.search(r'\b_?(?:forbidden_)?(?:sendmsg|recvmsg)\b', undefined):
            raise AssertionError('native objects still reference message/rights syscalls')
        subprocess.run([args.cc, *flags, '-DTMUX_NATIVE_211BSD=1',
                        str(ROOT / 'tests/imsg-native.c'), 'imsg.o', 'imsg-buffer.o',
                        '-o', 'imsg-regression'], cwd=work, check=True)
        subprocess.run([str(work / 'imsg-regression')], cwd=work, check=True)
        # A separate normal-host build keeps the stock SCM_RIGHTS path and
        # proves descriptor ownership/transfer with local host sockets only.
        for source in ['imsg.c', 'imsg-buffer.c']:
            subprocess.run([args.cc, *flags, '-c', source,
                            '-o', 'host-' + source[:-2] + '.o'], cwd=work, check=True)
        subprocess.run([args.cc, *flags, str(ROOT / 'tests/imsg-host-fd.c'),
                        'host-imsg.o', 'host-imsg-buffer.o', '-o', 'imsg-host-fd'],
                       cwd=work, check=True)
        subprocess.run([str(work / 'imsg-host-fd')], cwd=work, check=True)


if __name__ == '__main__':
    main()
