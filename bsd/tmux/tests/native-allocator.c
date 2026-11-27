/* Link actual xrealloc/buffer code with an allocator that models native faults. */
#define ALLOCATOR_TEST_DRIVER 1
#include "tmux.h"

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>

struct allocation {
	void *pointer;
	size_t size;
};
static struct allocation allocations[32];
static unsigned malloc_calls, realloc_calls, free_calls;
static int fail_malloc, fail_realloc, expect_fatal, fatal_calls;
static unsigned warning_calls, break_calls;
static unsigned last_nmemb, last_size, last_newsize, last_break, last_stack;
static jmp_buf fatal_target;

char *
allocator_test_sbrk(intptr_t increment)
{
	assert(increment == 0);
	break_calls++;
	return ((char *)(uintptr_t)0x6000);
}

void
allocator_test_warnx(const char *format, ...)
{
	va_list arguments;

	assert(strstr(format, "nmemb=%u") != NULL);
	assert(strstr(format, "brk=%u stack=%u") != NULL);
	va_start(arguments, format);
	last_nmemb = va_arg(arguments, unsigned);
	last_size = va_arg(arguments, unsigned);
	last_newsize = va_arg(arguments, unsigned);
	last_break = va_arg(arguments, unsigned);
	last_stack = va_arg(arguments, unsigned);
	va_end(arguments);
	warning_calls++;
	errno = EINTR; /* Logging must not replace the original allocation error. */
}

static int
find_allocation(void *pointer)
{
	unsigned i;

	for (i = 0; i < sizeof allocations / sizeof allocations[0]; i++) {
		if (allocations[i].pointer == pointer)
			return (i);
	}
	return (-1);
}

static void
record_allocation(void *pointer, size_t size)
{
	int slot = find_allocation(NULL);

	assert(pointer != NULL && slot >= 0);
	allocations[slot].pointer = pointer;
	allocations[slot].size = size;
}

void *
allocator_test_malloc(size_t size)
{
	void *pointer;

	malloc_calls++;
	if (fail_malloc) {
		fail_malloc = 0;
		errno = ENOMEM;
		return (NULL);
	}
	pointer = malloc(size);
	assert(pointer != NULL);
	record_allocation(pointer, size);
	errno = ERANGE;
	return (pointer);
}

void
allocator_test_free(void *pointer)
{
	int slot;

	assert(pointer != NULL);
	slot = find_allocation(pointer);
	assert(slot >= 0); /* Also detects use of a destructively failed realloc. */
	allocations[slot].pointer = NULL;
	allocations[slot].size = 0;
	free_calls++;
	free(pointer);
	errno = EINTR;
}

void *
allocator_test_realloc(void *old, size_t size)
{
	void *pointer;
	int slot;

	realloc_calls++;
	if (fail_realloc) {
		fail_realloc = 0;
#ifdef TEST_NATIVE_ALLOCATOR
		if (old != NULL)
			allocator_test_free(old);
#endif
		errno = ENOMEM;
		return (NULL);
	}
	if (old == NULL) {
#ifdef TEST_NATIVE_ALLOCATOR
		assert(!"native realloc must never receive NULL");
#endif
		return (allocator_test_malloc(size));
	}
	slot = find_allocation(old);
	assert(slot >= 0);
	pointer = realloc(old, size);
	assert(pointer != NULL);
	allocations[slot].pointer = pointer;
	allocations[slot].size = size;
	return (pointer);
}

void
allocator_test_fatal(const char *message)
{
	if (!expect_fatal) {
		fprintf(stderr, "unexpected fatal: %s\n", message);
		abort();
	}
	fatal_calls++;
	longjmp(fatal_target, 1);
}

void *
xmalloc(size_t size)
{
	return (allocator_test_malloc(size));
}

void *
xcalloc(size_t count, size_t size)
{
	void *pointer = calloc(count, size);

	assert(pointer != NULL);
	record_allocation(pointer, count * size);
	return (pointer);
}

void
xfree(void *pointer)
{
	allocator_test_free(pointer);
}

static void
check_xrealloc(void)
{
	unsigned before_malloc, before_realloc;
	unsigned char *pointer;
	unsigned i;

	before_malloc = malloc_calls;
	before_realloc = realloc_calls;
	pointer = xrealloc(NULL, 10, 2);
	assert(malloc_calls == before_malloc + 1);
#ifdef TEST_NATIVE_ALLOCATOR
	assert(realloc_calls == before_realloc);
#else
	assert(realloc_calls == before_realloc + 1);
#endif
	memset(pointer, 0x5a, 20);
	pointer = xrealloc(pointer, 20, 2);
	for (i = 0; i < 20; i++)
		assert(pointer[i] == 0x5a);
	xfree(pointer);

	expect_fatal = 1;
#ifdef TEST_NATIVE_ALLOCATOR
	fail_malloc = 1;
#else
	fail_realloc = 1;
#endif
	if (setjmp(fatal_target) == 0) {
		xrealloc(NULL, 1, 20);
		assert(!"allocation failure must be fatal");
	}
	assert(errno == ENOMEM);
#ifdef TEST_NATIVE_ALLOCATOR
	assert(warning_calls == 1 && break_calls == 1);
	assert(last_nmemb == 1 && last_size == 20 && last_newsize == 20);
	assert(last_break == 0x6000 && last_stack != 0);
#else
	assert(warning_calls == 0 && break_calls == 0);
#endif
	pointer = xmalloc(20);
	fail_realloc = 1;
	if (setjmp(fatal_target) == 0) {
		xrealloc(pointer, 2, 20);
		assert(!"reallocation failure must be fatal");
	}
	assert(errno == ENOMEM);
#ifdef TEST_NATIVE_ALLOCATOR
	assert(warning_calls == 2 && break_calls == 2);
	assert(last_nmemb == 2 && last_size == 20 && last_newsize == 40);
	assert(last_break == 0x6000 && last_stack != 0);
	assert(find_allocation(pointer) == -1);
#else
	assert(find_allocation(pointer) >= 0);
	xfree(pointer);
#endif
	before_malloc = malloc_calls;
	before_realloc = realloc_calls;
	if (setjmp(fatal_target) == 0) {
		xrealloc(NULL, 0, 20);
		assert(!"zero allocation must be rejected");
	}
	if (setjmp(fatal_target) == 0) {
		xrealloc(NULL, SIZE_MAX, 2);
		assert(!"16-bit multiplication overflow must be rejected");
	}
	assert(malloc_calls == before_malloc && realloc_calls == before_realloc);
	assert(fatal_calls == 4);
#ifdef TEST_NATIVE_ALLOCATOR
	assert(warning_calls == 2 && break_calls == 2);
#else
	assert(warning_calls == 0 && break_calls == 0);
#endif
	expect_fatal = 0;
}

static void
check_optional_shrink(void)
{
	struct buffer *buffer;
	unsigned char input[900], output[900], *before;
	size_t capacity;
	unsigned before_malloc, before_realloc, before_free;
	unsigned i;

	for (i = 0; i < sizeof input; i++)
		input[i] = i % 251;
	buffer = buffer_create(TMUX_IO_SIZE);
	buffer_write(buffer, input, sizeof input);
	buffer_remove(buffer, 300);
	assert(buffer->off == 300 && BUFFER_USED(buffer) == 600);
	assert(memcmp(BUFFER_OUT(buffer), input + 300, 600) == 0);
	before = buffer->base;
	capacity = buffer->space;
	before_malloc = malloc_calls;
	before_realloc = realloc_calls;
	before_free = free_calls;
#ifdef TEST_NATIVE_ALLOCATOR
	fail_malloc = 1;
#else
	fail_realloc = 1;
#endif
	errno = EDOM;
	buffer_read(buffer, output, 600);
	assert(memcmp(output, input + 300, 600) == 0);
	assert(BUFFER_USED(buffer) == 0 && buffer->off == 0 && errno == EDOM);
	assert(buffer->base == before && buffer->space == capacity);
	assert(find_allocation(before) >= 0 && free_calls == before_free);
#ifdef TEST_NATIVE_ALLOCATOR
	assert(malloc_calls == before_malloc + 1 && realloc_calls == before_realloc);
#else
	assert(malloc_calls == before_malloc && realloc_calls == before_realloc + 1);
#endif
	/* Reuse after the failed optional shrink must read/write live storage. */
	buffer_write(buffer, input, 600);
	errno = EDOM;
	buffer_read(buffer, output, 600);
	assert(memcmp(output, input, 600) == 0);
	assert(buffer->space == TMUX_IO_SIZE && buffer->off == 0 && errno == EDOM);
#ifdef TEST_NATIVE_ALLOCATOR
	assert(buffer->base != before && find_allocation(before) == -1);
#endif
	buffer_write(buffer, input, 100);
	buffer_read(buffer, output, 100);
	assert(memcmp(output, input, 100) == 0 && buffer->space == TMUX_IO_SIZE);
	buffer_destroy(buffer);
}

int
main(void)
{
	unsigned i;

	check_xrealloc();
	check_optional_shrink();
	for (i = 0; i < sizeof allocations / sizeof allocations[0]; i++)
		assert(allocations[i].pointer == NULL);
	puts("allocator NULL/reallocation contracts and optional-shrink recovery passed");
	return (0);
}
