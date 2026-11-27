#!/usr/bin/env python3
"""Compile actual xmalloc.c's native formatter branch with a temporary host shim."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='tmux-xsnprintf-regression-') as temporary:
        work = Path(temporary)
        shutil.copy2(ROOT / 'upstream/xmalloc.c', work / 'xmalloc.c')
        (work / 'tmux.h').write_text('''#ifndef FORMAT_TEST_TMUX_H
#define FORMAT_TEST_TMUX_H
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>
#undef INT_MAX
#define INT_MAX 32767
#define printflike2
#define printflike3
#undef strlcpy
#undef vasprintf
#define strlcpy test_strlcpy
#define vasprintf test_unused_vasprintf
void fatal(const char *, ...);
void fatalx(const char *, ...);
void log_warnx(const char *, ...);
void *xmalloc(size_t);
int xvasprintf(char **, const char *, va_list);
int xsnprintf(char *, size_t, const char *, ...);
int xvsnprintf(char *, size_t, const char *, va_list);
size_t test_strlcpy(char *, const char *, size_t);
int test_unused_vasprintf(char **, const char *, va_list);
#endif
''')
        # Load system aliases/fortify macros before redirecting only the tested
        # object's vsnprintf calls, leaving the harness's real formatter alone.
        (work / 'redirect.h').write_text('''#include <sys/param.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#undef vsnprintf
#define vsnprintf test_vsnprintf
int test_vsnprintf(char *, size_t, const char *, va_list);
''')
        flags = ['-O0', '-g', '-Wall', '-Wextra', '-Werror', '-iquote.']
        subprocess.run([args.cc, *flags, '-DTMUX_NATIVE_211BSD=1',
                        '-include', 'redirect.h', '-c', 'xmalloc.c', '-o', 'xmalloc.o'],
                       cwd=work, check=True)
        # The optional diagnostic is not enabled in normal builds, but keep
        # its complete wrapper and native-only declarations compilable too.
        subprocess.run([args.cc, *flags, '-std=c89', '-DTMUX_NATIVE_211BSD=1',
                        '-DTMUX_ALLOC_STATS=1', '-include', 'redirect.h',
                        '-c', 'xmalloc.c', '-o', 'xmalloc-stats.o'],
                       cwd=work, check=True)
        subprocess.run([args.cc, *flags, str(ROOT / 'tests/xsnprintf-native.c'),
                        'xmalloc.o', '-o', 'xsnprintf-regression'], cwd=work, check=True)
        subprocess.run([str(work / 'xsnprintf-regression')], cwd=work, check=True)


if __name__ == '__main__':
    main()
