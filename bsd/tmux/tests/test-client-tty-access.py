#!/usr/bin/env python3
"""Check the native client's terminal-access decision with the canonical function.

The native server opens the client's terminal by pathname. The client refuses
early, with an explanation, only when that open is certain to fail.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

HARNESS = """
int
main(void)
{
	/* The reported case: root's 0620 terminal and a server run by uid 100. */
	assert(client_tty_refused(100, 0, 020620) == 1);
	assert(client_tty_refused(100, 0, 020600) == 1);
	/* Write alone, or read alone, is not enough to reopen a terminal. */
	assert(client_tty_refused(100, 0, 020622) == 1);
	assert(client_tty_refused(100, 0, 020644) == 1);
	/* The same user, or a server run by root, can always open it. */
	assert(client_tty_refused(100, 100, 020600) == 0);
	assert(client_tty_refused(100, 100, 020000) == 0);
	assert(client_tty_refused(0, 100, 020600) == 0);
	assert(client_tty_refused(0, 0, 020600) == 0);
	/* Read and write for others is enough; for the group it may be. */
	assert(client_tty_refused(100, 0, 020606) == 0);
	assert(client_tty_refused(100, 0, 020666) == 0);
	assert(client_tty_refused(100, 0, 020660) == 0);
	assert(client_tty_refused(101, 100, 020620) == 1);
	puts("native client terminal-access decision passed");
	return (0);
}
"""


def function(text, name):
    match = re.search(r'(?m)^static int\n' + re.escape(name) + r'\(', text)
    if match is None:
        raise ValueError('Missing canonical function: ' + name)
    end = text.index('\n}\n', match.start()) + 3
    return text[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    source = function((ROOT / 'upstream/client-tty.c').read_text(), 'client_tty_refused')
    program = ('#include <sys/types.h>\n#include <assert.h>\n#include <stdio.h>\n\n' +
               source + HARNESS)
    with tempfile.TemporaryDirectory(prefix='tmux-client-tty-') as temporary:
        work = Path(temporary)
        (work / 'check.c').write_text(program)
        subprocess.run([args.cc, '-O0', '-g', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', str(work / 'check.c'),
                        '-o', str(work / 'check')], check=True)
        subprocess.run([str(work / 'check')], check=True)


if __name__ == '__main__':
    main()
