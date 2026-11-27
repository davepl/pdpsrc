#!/usr/bin/env python3
"""Check actual milestone CSI argument growth with native 16-bit limits."""
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
    source = (ROOT / 'upstream/input.c').read_text()
    start = source.index('void\ninput_new_argument(')
    end = source.index('\nint\ninput_get_argument(', start)
    functions = source[start:end].replace('size_t', 'uint16_t')
    header = (ROOT / 'upstream/tmux-full.h').read_text()
    structure = re.search(r'^struct input_arg\s*\{.*?^\};',
                          header, re.M | re.S).group()
    structure = structure.replace('size_t', 'uint16_t')
    with tempfile.TemporaryDirectory(prefix='tmux-native-input-args-') as temporary:
        work = Path(temporary)
        shutil.copy2(ROOT / 'upstream/array.h', work / 'array.h')
        (work / 'input-args.c').write_text('#include "tmux.h"\n' + functions)
        (work / 'tmux.h').write_text('''#ifndef INPUT_ARGS_TEST_TMUX_H
#define INPUT_ARGS_TEST_TMUX_H
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#undef UINT_MAX
#undef SIZE_MAX
#define UINT_MAX 65535U
#define SIZE_MAX 65535U
#define TMUX_MILESTONE 1
#define TMUX_NATIVE_HEAP_TEST 1
#define u_char unsigned char
#include "array.h"
''' + structure + '''
struct input_ctx {
    struct {
        struct input_arg *list;
        uint16_t num;
        uint16_t space;
    } args;
};
typedef char native_input_arg_must_be_66_bytes[sizeof(struct input_arg) == 66 ? 1 : -1];
void *xrealloc(void *, size_t, size_t);
void fatalx(const char *);
void input_new_argument(struct input_ctx *);
int input_add_argument(struct input_ctx *, u_char);
#endif
''')
        flags = ['-std=c89', '-O0', '-g', '-Wall', '-Wextra', '-Werror',
                 '-fsanitize=address,undefined', '-I' + str(work)]
        subprocess.run([args.cc, *flags, str(work / 'input-args.c'),
                        str(ROOT / 'tests/native-input-args.c'),
                        '-o', str(work / 'native-input-args')], check=True)
        environment = os.environ.copy()
        environment['ASAN_OPTIONS'] = 'symbolize=0'
        subprocess.run([str(work / 'native-input-args')], check=True,
                       env=environment)


if __name__ == '__main__':
    main()
