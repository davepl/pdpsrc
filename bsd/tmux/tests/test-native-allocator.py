#!/usr/bin/env python3
"""Exercise actual allocator/buffer code with native and ordinary host faults."""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    header = (ROOT / 'upstream/tmux-full.h').read_text()
    structure = re.search(r'^struct buffer\s*\{.*?^\};', header, re.M | re.S).group()
    macros = '\n'.join(re.findall(r'^#define BUFFER_.*$', header, re.M))
    source = (ROOT / 'upstream/xmalloc.c').read_text()
    start = source.index('void *\nxrealloc(')
    end = source.index('\nvoid\nxfree(', start)
    with tempfile.TemporaryDirectory(prefix='tmux-allocator-regression-') as temporary:
        work = Path(temporary)
        shutil.copy2(ROOT / 'upstream/buffer.c', work / 'buffer.c')
        (work / 'xrealloc.c').write_text('#include "tmux.h"\n' + source[start:end])
        (work / 'tmux.h').write_text('''#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#undef SIZE_MAX
#define SIZE_MAX 65535U
#define TMUX_SMALL_MEMORY 1
#define TMUX_IO_SIZE 256
''' + macros + '\n' + structure + '''
void *xmalloc(size_t);
void *xcalloc(size_t, size_t);
void *xrealloc(void *, size_t, size_t);
void xfree(void *);
struct buffer *buffer_create(size_t);
void buffer_destroy(struct buffer *);
void buffer_write(struct buffer *, const void *, size_t);
void buffer_read(struct buffer *, void *, size_t);
void buffer_remove(struct buffer *, size_t);
void *allocator_test_malloc(size_t);
void *allocator_test_realloc(void *, size_t);
void allocator_test_free(void *);
void allocator_test_fatal(const char *);
char *allocator_test_sbrk(intptr_t);
void allocator_test_warnx(const char *, ...);
#define fatal(message) allocator_test_fatal(message)
#define fatalx(message) allocator_test_fatal(message)
#ifndef ALLOCATOR_TEST_DRIVER
#define malloc allocator_test_malloc
#define realloc allocator_test_realloc
#define free allocator_test_free
#define sbrk allocator_test_sbrk
#define log_warnx allocator_test_warnx
#endif
''')
        flags = ['-std=c89', '-D_DEFAULT_SOURCE=1', '-O0', '-g',
                 '-Wall', '-Wextra', '-Werror', '-fsanitize=address', '-I.']
        environment = os.environ.copy()
        environment['ASAN_OPTIONS'] = 'symbolize=0'
        for name, profile in [('native', ['-DTMUX_NATIVE_211BSD=1',
                                        '-DTEST_NATIVE_ALLOCATOR=1']), ('host', [])]:
            for unit in ['buffer', 'xrealloc']:
                subprocess.run([args.cc, *flags, *profile, '-c', unit + '.c',
                                '-o', unit + '.o'], cwd=work, check=True)
            subprocess.run([args.cc, *flags, *profile,
                            str(ROOT / 'tests/native-allocator.c'),
                            'buffer.o', 'xrealloc.o', '-o', name], cwd=work, check=True)
            subprocess.run([str(work / name)], cwd=work, check=True, env=environment)


if __name__ == '__main__':
    main()
