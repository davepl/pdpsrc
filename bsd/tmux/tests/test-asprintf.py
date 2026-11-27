#!/usr/bin/env python3
"""Test the actual fallback against emulated old formatting and 16-bit bounds."""
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
    with tempfile.TemporaryDirectory(prefix='tmux-asprintf-regression-') as temporary:
        work = Path(temporary)
        shutil.copy2(ROOT / 'upstream/compat/asprintf.c', work / 'asprintf.c')
        (work / 'tmux.h').write_text('''#include <stdarg.h>
#include <stddef.h>
#include <limits.h>
#undef INT_MAX
#define INT_MAX 32767
int asprintf(char **, const char *, ...);
int vasprintf(char **, const char *, va_list);
void *xmalloc(size_t);
void xfree(void *);
''')
        flags = ['-O0', '-g', '-Wall', '-Wextra', '-Werror']
        # Include system declarations before renaming calls: Darwin's stdio
        # declarations attach assembler aliases that a -Dname flag preserves.
        (work / 'redirect.h').write_text('''#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#undef asprintf
#undef vasprintf
#undef vsnprintf
#undef malloc
#undef free
#define asprintf format_test_asprintf
#define vasprintf format_test_vasprintf
#define vsnprintf format_test_vsnprintf
#define malloc format_test_malloc
#define free format_test_free
int format_test_asprintf(char **, const char *, ...);
int format_test_vasprintf(char **, const char *, va_list);
int format_test_vsnprintf(char *, size_t, const char *, va_list);
void *format_test_malloc(size_t);
void format_test_free(void *);
''')
        subprocess.run([args.cc, *flags, '-DBROKEN_VSNPRINTF=1',
                        '-include', 'redirect.h', '-c', 'asprintf.c',
                        '-o', 'asprintf.o'], cwd=work, check=True)
        subprocess.run([args.cc, *flags, str(ROOT / 'tests/asprintf-old.c'),
                        'asprintf.o', '-o', 'asprintf-regression'], cwd=work, check=True)
        subprocess.run([str(work / 'asprintf-regression')], cwd=work, check=True)


if __name__ == '__main__':
    main()
