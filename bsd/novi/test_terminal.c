#include <sys/types.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdlib.h>
#include "pdp11_compat.h"
#include "terminal.h"

struct key_test {
	char *input;
	int length;
	int expected;
};

static void
fail(char *name, int got, int expected)
{
	(void)fprintf(stderr, "test_terminal: %s returned %d, expected %d\n",
	    name, got, expected);
	exit(1);
}

int
main(void)
{
	struct key_test tests[] = {
		{ "\033[H", 3, KEY_HOME },
		{ "\033OH", 3, KEY_HOME },
		{ "\033[1~", 4, KEY_HOME },
		{ "\033[7~", 4, KEY_HOME },
		{ "\033[F", 3, KEY_END },
		{ "\033OF", 3, KEY_END },
		{ "\033[4~", 4, KEY_END },
		{ "\033[8~", 4, KEY_END },
		{ "\033[2~", 4, KEY_INSERT },
		{ "\033[5~", 4, KEY_PGUP },
		{ "\033[6~", 4, KEY_PGDN },
		{ "\033[5;2~", 6, KEY_PGUP },
		{ "\033[6;5~", 6, KEY_PGDN },
		{ "\2335~", 3, KEY_PGUP },
		{ "\2336~", 3, KEY_PGDN },
		{ "\033[I", 3, KEY_PGUP },
		{ "\033[G", 3, KEY_PGDN }
	};
	char *names[] = {
		"CSI Home", "SS3 Home", "Home 1", "Home 7",
		"CSI End", "SS3 End", "End 4", "End 8",
		"Insert", "Page Up", "Page Down",
		"Shift Page Up", "Control Page Down",
		"8-bit Page Up", "8-bit Page Down",
		"Legacy Page Up", "Legacy Page Down"
	};
	int fds[2];
	int count;
	int i;
	int got;

	count = sizeof tests / sizeof tests[0];
	for (i = 0; i < count; ++i) {
		if (pipe(fds) < 0)
			fail("pipe", -1, 0);
		if (write(fds[1], tests[i].input, tests[i].length) !=
		    tests[i].length)
			fail("write", -1, tests[i].length);
		(void)close(fds[1]);
		if (dup2(fds[0], 0) < 0)
			fail("dup2", -1, 0);
		(void)close(fds[0]);
		got = term_key();
		if (got != tests[i].expected)
			fail(names[i], got, tests[i].expected);
	}
	(void)printf("terminal key tests passed\n");
	return 0;
}
