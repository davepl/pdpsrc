/* Actual CSI allocation/append functions with PDP-11 sizes and moving storage. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tmux.h"

static void *allocation;
static size_t allocated;
static unsigned int allocation_calls;
static int expecting_fatal;
static const char *fatal_message;
static jmp_buf fatal_return;

void *
xrealloc(void *old, size_t nmemb, size_t size)
{
	void *next;

	assert(old == allocation);
	assert(nmemb == 1 && size > 0 && size <= SIZE_MAX);
	next = malloc(size);
	assert(next != NULL);
	memset(next, 0xa5, size);
	if (old != NULL) {
		assert(size >= allocated);
		memcpy(next, old, allocated);
		free(old);
	}
	allocation = next;
	allocated = size;
	allocation_calls++;
	return (next);
}

void
fatalx(const char *message)
{
	assert(expecting_fatal);
	fatal_message = message;
	longjmp(fatal_return, 1);
}

static void
reset(struct input_ctx *ictx)
{
	free(allocation);
	allocation = NULL;
	allocated = 0;
	allocation_calls = 0;
	expecting_fatal = 0;
	fatal_message = NULL;
	memset(ictx, 0, sizeof *ictx);
}

static unsigned char
pattern(unsigned int argument, unsigned int offset)
{
	return ((unsigned char)('0' + ((argument + offset) % 10)));
}

static void
fill_last(struct input_ctx *ictx)
{
	unsigned int j, i = ictx->args.num - 1;

	assert(ictx->args.list[i].used == 0);
	for (j = 0; j < sizeof ictx->args.list[i].data; j++)
		assert(input_add_argument(ictx, pattern(i, j)) == 0);
	assert(ictx->args.list[i].used == 64);
	assert(input_add_argument(ictx, 'X') == -1);
	assert(ictx->args.list[i].used == 64);
}

static void
check_full(const struct input_ctx *ictx, unsigned int count)
{
	unsigned int i, j;

	assert(ictx->args.num >= count);
	for (i = 0; i < count; i++) {
		assert(ictx->args.list[i].used == 64);
		for (j = 0; j < sizeof ictx->args.list[i].data; j++)
			assert(ictx->args.list[i].data[j] == pattern(i, j));
	}
}

static void
seed(struct input_ctx *ictx, unsigned int count, unsigned int space)
{
	unsigned int i, j;

	reset(ictx);
	assert(space >= count * sizeof(struct input_arg));
	ictx->args.list = xrealloc(NULL, 1, space);
	ictx->args.num = count;
	ictx->args.space = space;
	for (i = 0; i < count; i++) {
		ictx->args.list[i].used = 64;
		for (j = 0; j < sizeof ictx->args.list[i].data; j++)
			ictx->args.list[i].data[j] = pattern(i, j);
	}
	allocation_calls = 0;
}

static void
expect_fatal(struct input_ctx *ictx, const char *message)
{
	struct input_arg *before = ictx->args.list;
	unsigned int num = ictx->args.num, space = ictx->args.space;
	unsigned int calls = allocation_calls;

	expecting_fatal = 1;
	if (setjmp(fatal_return) == 0) {
		input_new_argument(ictx);
		abort();
	}
	expecting_fatal = 0;
	assert(strcmp(fatal_message, message) == 0);
	assert(ictx->args.list == before && ictx->args.num == num);
	assert(ictx->args.space == space && allocation_calls == calls);
}

int
main(void)
{
	struct input_ctx ictx;
	struct input_arg *before;
	unsigned int i, calls, space;

	reset(&ictx);
	assert(sizeof(struct input_arg) == 66);
	assert(input_add_argument(&ictx, 'X') == 0);
	assert(ictx.args.num == 0 && allocation_calls == 0);
	for (i = 1; i <= 65; i++) {
		input_new_argument(&ictx);
		assert(ictx.args.num == i);
		check_full(&ictx, i - 1);
		if (i <= 4) {
			assert(ictx.args.space == 264 && allocation_calls == 1);
		} else if (i <= 8) {
			assert(ictx.args.space == 528 && allocation_calls == 2);
		} else if (i == 65) {
			assert(ictx.args.space == 8448 && allocation_calls == 6);
		}
		fill_last(&ictx);
	}
	check_full(&ictx, 65);

	/* Clearing the real array retains storage; new arguments reset used. */
	before = ictx.args.list;
	calls = allocation_calls;
	space = ictx.args.space;
	ARRAY_CLEAR(&ictx.args);
	assert(input_add_argument(&ictx, 'X') == 0);
	for (i = 0; i < 4; i++) {
		input_new_argument(&ictx);
		assert(ictx.args.list[i].used == 0);
		assert(input_add_argument(&ictx, 'R') == 0);
		assert(ictx.args.list[i].used == 1);
		assert(ictx.args.list[i].data[0] == 'R');
	}
	assert(ictx.args.list == before && ictx.args.space == space);
	assert(allocation_calls == calls && ictx.args.num == 4);

	/* Above the signed 16-bit range, use the exact required byte count. */
	seed(&ictx, 606, 40000);
	input_new_argument(&ictx);
	assert(ictx.args.num == 607 && ictx.args.space == 40062);
	assert(allocation_calls == 1 && ictx.args.list[606].used == 0);
	check_full(&ictx, 606);

	/* Both sides of SIZE_MAX/2 retain unsigned arithmetic and no wrap. */
	seed(&ictx, 496, 32767);
	input_new_argument(&ictx);
	assert(ictx.args.num == 497 && ictx.args.space == 65534);
	assert(allocation_calls == 1 && ictx.args.list[496].used == 0);
	check_full(&ictx, 496);
	seed(&ictx, 496, 32768);
	input_new_argument(&ictx);
	assert(ictx.args.num == 497 && ictx.args.space == 32802);
	assert(allocation_calls == 1 && ictx.args.list[496].used == 0);
	check_full(&ictx, 496);

	/* 992 native arguments fit; the 993rd exceeds a 16-bit byte size. */
	seed(&ictx, 991, 65406);
	input_new_argument(&ictx);
	assert(ictx.args.num == 992 && ictx.args.space == 65472);
	assert(allocation_calls == 1 && ictx.args.list[991].used == 0);
	check_full(&ictx, 991);
	expect_fatal(&ictx, "size too big");
	ictx.args.num = 65535U;
	expect_fatal(&ictx, "number too big");

	reset(&ictx);
	puts("native CSI four-slot growth, 64-byte fields, reuse and 16-bit overflow guards passed");
	return (0);
}
