/* Check actual capability stripping, including oversized and malformed tails. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "tmux-full.h"

char *tty_term_strip(const char *);

static void
expect(const char *input, const char *wanted)
{
	char *actual = tty_term_strip(input);
	assert(actual != input);
	assert(strcmp(actual, wanted) == 0);
	xfree(actual);
}

int
main(void)
{
	char *input, *wanted;
	size_t i;

	expect("", "");
	expect("plain", "plain");
	expect("$", "$");
	expect("a$", "a$");
	expect("a$b", "a$b");
	expect("$<5>", "");
	expect("a$<5>b$<2.5*>c", "abc");
	expect("$<5/>tail", "tail");
	expect("tail$<1>", "tail");
	expect("$<>x", "x");
	expect("x$<oops>y", "xy");
	expect("a$<", "a");
	expect("a$<5", "a");
	expect("$<oops", "");
	expect("$<<5>>", ">");
	expect("$<a$<b>c", "c");
	input = xmalloc(20001);
	wanted = xmalloc(20001);
	memset(input, 'x', 20000);
	input[20000] = '\0';
	expect(input, input);
	for (i = 0; i < 2000; i++) {
		memcpy(input + i * 10, "a$<2.5*>bc", 10);
		memcpy(wanted + i * 3, "abc", 3);
	}
	input[20000] = '\0';
	wanted[6000] = '\0';
	expect(input, wanted);
	xfree(input);
	xfree(wanted);
	return (0);
}
