/* Host regressions for the actual small-memory buffer and mode-key units.
 * Build with TMUX_SMALL_MEMORY=1 and TMUX_IO_SIZE=256. Compile only the buffer
 * test object with -Drealloc=memory_profile_realloc to inject failed shrinking;
 * this test supplies allocator/fatal stubs, so do not also link xmalloc.o.
 */
#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux-full.h"

#ifndef TMUX_SMALL_MEMORY
#error Run this test with the actual small-memory code enabled.
#endif
#ifdef NDEBUG
#error These regression checks require assertions.
#endif
#if TMUX_IO_SIZE != 256
#error This regression expects the native 256-byte I/O quantum.
#endif

static unsigned allocations;
static unsigned shrink_calls;
static int fail_next_shrink;
static size_t expected_growth_probe;

void *
xmalloc(size_t size)
{
	void *p;

	assert(size != 0);
	p = malloc(size);
	assert(p != NULL);
	allocations++;
	return (p);
}

void *
xcalloc(size_t count, size_t size)
{
	void *p;

	assert(count != 0 && size != 0);
	p = calloc(count, size);
	assert(p != NULL);
	allocations++;
	return (p);
}

void *
xrealloc(void *old, size_t count, size_t size)
{
	void *p;
	int fresh;

	assert(count != 0 && size != 0);
	if (expected_growth_probe != 0) {
		/* Inspect an overflow-boundary request without allocating SIZE_MAX. */
		assert(old != NULL && count == 1 && size == expected_growth_probe);
		expected_growth_probe = 0;
		return (old);
	}
	fresh = old == NULL;
	p = realloc(old, count * size);
	assert(p != NULL);
	if (fresh)
		allocations++;
	return (p);
}

void
xfree(void *p)
{
	assert(p != NULL && allocations != 0);
	free(p);
	allocations--;
}

void
log_fatalx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	abort();
}

/*
 * The generated copies under test fetch error text through this; log.o, which
 * reads it from the message file, is not linked here.
 */
char *
tmux_msg(unsigned offset)
{
	static char text[32];

	snprintf(text, sizeof text, "message %u", offset);
	return (text);
}

void *
memory_profile_realloc(void *p, size_t size)
{
	assert(size == TMUX_IO_SIZE);
	shrink_calls++;
	if (fail_next_shrink) {
		fail_next_shrink = 0;
		errno = ENOMEM;
		return (NULL);
	}
	return (realloc(p, size));
}

static void
check_buffers(void)
{
	struct buffer *b;
	u_char input[900], output[900], *before;
	size_t i, capacity;
	unsigned calls;

	for (i = 0; i < sizeof input; i++)
		input[i] = (u_char)(i % 251);
	b = buffer_create(TMUX_IO_SIZE);
	assert(b->space == TMUX_IO_SIZE);
	buffer_write(b, input, sizeof input);
	assert(b->space >= sizeof input);
	buffer_remove(b, 300);
	assert(BUFFER_USED(b) == 600 && b->off == 300);
	assert(memcmp(BUFFER_OUT(b), input + 300, 600) == 0);
	errno = EDOM;
	buffer_read(b, output, 600);
	assert(memcmp(output, input + 300, 600) == 0);
	assert(BUFFER_USED(b) == 0 && b->off == 0);
	assert(b->space == TMUX_IO_SIZE && errno == EDOM);
	buffer_write(b, input, 100);
	buffer_read(b, output, 100);
	assert(memcmp(output, input, 100) == 0);
	assert(b->off == 0 && b->space == TMUX_IO_SIZE);

	buffer_write(b, input, sizeof input);
	before = b->base;
	capacity = b->space;
	calls = shrink_calls;
	fail_next_shrink = 1;
	errno = EDOM;
	buffer_remove(b, sizeof input);
	assert(shrink_calls == calls + 1 && fail_next_shrink == 0);
	assert(b->base == before && b->space == capacity);
	assert(BUFFER_USED(b) == 0 && b->off == 0 && errno == EDOM);
	buffer_write(b, input, 600);
	buffer_read(b, output, 600);
	assert(memcmp(output, input, 600) == 0);
	assert(b->space == TMUX_IO_SIZE && b->off == 0);
	buffer_destroy(b);
}

static void
check_growth_rounding(void)
{
	struct buffer *b;
	u_char input[2210], output[2087];
	size_t i, needed;

	for (i = 0; i < sizeof input; i++)
		input[i] = (u_char)(i % 251);
	b = buffer_create(TMUX_IO_SIZE);
	buffer_write(b, input, 900);
	buffer_remove(b, 123);
	assert(b->off == 123 && BUFFER_USED(b) == 777);
	buffer_write(b, input + 900, 1310);
	needed = 2087;
	assert(b->off == 0 && BUFFER_USED(b) == needed);
	assert(b->space >= needed && b->space - needed < TMUX_IO_SIZE);
	assert(b->space == 2304);
	assert(memcmp(BUFFER_OUT(b), input + 123, needed) == 0);
	buffer_read(b, output, needed);
	assert(memcmp(output, input + 123, needed) == 0);
	assert(b->off == 0 && b->space == TMUX_IO_SIZE);

	needed = 4097;
	buffer_ensure(b, needed);
	assert(b->space >= needed && b->space - needed < TMUX_IO_SIZE);
	buffer_write(b, input, 100);
	buffer_read(b, output, 100);
	assert(memcmp(output, input, 100) == 0);
	assert(b->off == 0 && b->space == TMUX_IO_SIZE);

	/* Rounding near SIZE_MAX must use exact need, never wrap below it. */
	b->size = b->space = SIZE_MAX - 2;
	expected_growth_probe = SIZE_MAX - 1;
	buffer_ensure(b, 1);
	assert(expected_growth_probe == 0 && b->space == SIZE_MAX - 1);
	b->size = b->off = 0;
	b->space = TMUX_IO_SIZE;
	buffer_destroy(b);
}

static void
check_poll_buffers(void)
{
	struct buffer *in, *out;
	u_char input[300], output[300];
	int pair[2];
	size_t i;

	for (i = 0; i < sizeof input; i++)
		input[i] = (u_char)(i % 239);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
	in = buffer_create(TMUX_IO_SIZE);
	out = buffer_create(TMUX_IO_SIZE);
	assert(write(pair[1], input, sizeof input) == sizeof input);
	assert(buffer_poll(pair[0], POLLIN, in, NULL) == 0);
	assert(BUFFER_USED(in) == TMUX_IO_SIZE && in->space == TMUX_IO_SIZE);
	buffer_read(in, output, TMUX_IO_SIZE);
	assert(memcmp(output, input, TMUX_IO_SIZE) == 0);
	assert(buffer_poll(pair[0], POLLIN, in, NULL) == 0);
	assert(BUFFER_USED(in) == sizeof input - TMUX_IO_SIZE);
	buffer_read(in, output, sizeof input - TMUX_IO_SIZE);
	assert(memcmp(output, input + TMUX_IO_SIZE, sizeof input - TMUX_IO_SIZE) == 0);

	buffer_write(out, input, sizeof input);
	assert(out->space > TMUX_IO_SIZE);
	assert(buffer_poll(pair[0], POLLOUT, NULL, out) == 0);
	assert(BUFFER_USED(out) == 0 && out->off == 0 && out->space == TMUX_IO_SIZE);
	assert(read(pair[1], output, sizeof output) == sizeof output);
	assert(memcmp(output, input, sizeof input) == 0);
	buffer_destroy(in);
	buffer_destroy(out);
	close(pair[0]);
	close(pair[1]);
}

static void
clear_bindings(struct mode_key_tree *tree)
{
	struct mode_key_binding *binding;

	while (!SPLAY_EMPTY(tree)) {
		binding = SPLAY_ROOT(tree);
		SPLAY_REMOVE(mode_key_tree, tree, binding);
		xfree(binding);
	}
}

static void
check_mode_keys(void)
{
	struct mode_key_data data;
	struct mode_key_binding *binding;
	const struct mode_key_table *table;

	mode_key_init_trees();
	assert(allocations == 0);
	assert(SPLAY_EMPTY(&mode_key_tree_emacs_edit));
	assert(SPLAY_EMPTY(&mode_key_tree_vi_choice));
	mode_key_init(&data, &mode_key_tree_emacs_edit);
	assert(mode_key_lookup(&data, '\r') == MODEKEYEDIT_ENTER);
	assert(!SPLAY_EMPTY(&mode_key_tree_emacs_edit));
	assert(SPLAY_EMPTY(&mode_key_tree_vi_choice));
	assert(mode_key_findtable("unknown-table") == NULL);
	assert(SPLAY_EMPTY(&mode_key_tree_vi_choice));
	table = mode_key_findtable("VI-choice");
	assert(table != NULL && !SPLAY_EMPTY(table->tree));
	/* A copy table is built on first use; the milestone has the emacs one. */
	assert(SPLAY_EMPTY(&mode_key_tree_emacs_copy));
	table = mode_key_findtable("emacs-copy");
	assert(table != NULL && !SPLAY_EMPTY(table->tree));
#ifdef TMUX_MILESTONE
	assert(mode_key_findtable("vi-copy") == NULL);
#else
	assert(SPLAY_EMPTY(&mode_key_tree_vi_copy));
#endif

	binding = xmalloc(sizeof *binding);
	binding->key = 'X';
	binding->mode = 0;
	binding->cmd = MODEKEYEDIT_CANCEL;
	assert(SPLAY_INSERT(mode_key_tree, data.tree, binding) == NULL);
	mode_key_init(&data, data.tree);
	assert(mode_key_lookup(&data, 'X') == MODEKEYEDIT_CANCEL);

	clear_bindings(data.tree);
	mode_key_init(&data, data.tree);
	assert(SPLAY_EMPTY(data.tree));
	assert(mode_key_lookup(&data, '\r') == MODEKEY_OTHER);
	table = mode_key_findtable("emacs-edit");
	assert(table != NULL && SPLAY_EMPTY(table->tree));

	mode_key_free_trees();
	assert(allocations == 0);
	mode_key_init_trees();
	mode_key_init(&data, &mode_key_tree_emacs_edit);
	assert(mode_key_lookup(&data, '\r') == MODEKEYEDIT_ENTER);
	assert(mode_key_lookup(&data, 'X') == MODEKEY_OTHER);
	mode_key_free_trees();
	assert(allocations == 0);
}

int
main(void)
{
	check_buffers();
	check_growth_rounding();
	check_poll_buffers();
	assert(allocations == 0);
	check_mode_keys();
	puts("small-memory buffer/poll/mode-key regressions passed");
	return (0);
}
