/* Link only compat/asprintf.c with its formatter/allocator calls redirected. */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NATIVE_FORMAT_LIMIT 32767
#define TRACE_LIMIT 64

int format_test_asprintf(char **, const char *, ...);
int format_test_vasprintf(char **, const char *, va_list);

enum formatter_mode { OLD_TRUNCATION, C99_LENGTH, ALWAYS_NEGATIVE, OVERSIZE_LENGTH };
struct allocation { void *pointer; size_t size; };
static enum formatter_mode mode;
static struct allocation allocations[TRACE_LIMIT];
static size_t capacities[TRACE_LIMIT], requests[TRACE_LIMIT];
static unsigned int calls, zero_calls, malloc_calls, fail_at, live;

static void
reset(enum formatter_mode selected, unsigned int fail)
{
	assert(live == 0);
	memset(allocations, 0, sizeof allocations);
	memset(capacities, 0, sizeof capacities);
	memset(requests, 0, sizeof requests);
	calls = zero_calls = malloc_calls = 0;
	fail_at = fail;
	mode = selected;
	errno = 0;
}

void *
format_test_malloc(size_t size)
{
	unsigned int i;
	void *pointer;

	assert(malloc_calls < TRACE_LIMIT);
	requests[malloc_calls++] = size;
	assert(size > 0 && size <= NATIVE_FORMAT_LIMIT);
	if (malloc_calls == fail_at) {
		errno = ENOMEM;
		return NULL;
	}
	pointer = malloc(size);
	assert(pointer != NULL);
	for (i = 0; i < TRACE_LIMIT; i++) {
		if (allocations[i].pointer == NULL) {
			allocations[i].pointer = pointer;
			allocations[i].size = size;
			live++;
			return pointer;
		}
	}
	abort();
}

void
format_test_free(void *pointer)
{
	unsigned int i;

	assert(pointer != NULL);
	for (i = 0; i < TRACE_LIMIT; i++) {
		if (allocations[i].pointer == pointer) {
			allocations[i].pointer = NULL;
			allocations[i].size = 0;
			live--;
			free(pointer);
			return;
		}
	}
	abort();
}

static size_t
allocation_size(const void *pointer)
{
	unsigned int i;

	for (i = 0; i < TRACE_LIMIT; i++) {
		if (allocations[i].pointer == pointer)
			return allocations[i].size;
	}
	abort();
}

int
format_test_vsnprintf(char *buffer, size_t capacity, const char *format, va_list ap)
{
	int length;

	assert(calls < TRACE_LIMIT);
	capacities[calls++] = capacity;
	if (capacity == 0) {
		zero_calls++;
		return -1;
	}
	assert(buffer != NULL && capacity <= NATIVE_FORMAT_LIMIT);
	/* The real formatter consumes ap, exposing missing va_list copies on retry. */
	length = vsnprintf(buffer, capacity, format, ap);
	if (mode == ALWAYS_NEGATIVE)
		return -1;
	if (mode == OVERSIZE_LENGTH)
		return NATIVE_FORMAT_LIMIT;
	if (mode == OLD_TRUNCATION && length >= 0 && (size_t)length >= capacity)
		return -1;
	return length;
}

static void
check_capacities(size_t length)
{
	size_t capacity;
	unsigned int i;

	assert(calls > 0 && zero_calls == 0 && capacities[0] == 64);
	if (mode == C99_LENGTH) {
		assert(calls == (length < 64 ? 1U : 2U));
		if (calls == 2)
			assert(capacities[1] == length + 1);
	} else {
		capacity = 64;
		for (i = 0; i < calls; i++) {
			assert(capacities[i] == capacity);
			if (capacity < NATIVE_FORMAT_LIMIT)
				capacity = capacity > NATIVE_FORMAT_LIMIT / 2 ?
				    NATIVE_FORMAT_LIMIT : capacity * 2;
		}
		assert(capacities[calls - 1] > length);
	}
}

static void
check_format(const char *format, ...)
{
	char expected[NATIVE_FORMAT_LIMIT + 1], *result;
	va_list ap, reference;
	int length, actual;

	va_start(ap, format);
	va_copy(reference, ap);
	length = vsnprintf(expected, sizeof expected, format, reference);
	va_end(reference);
	assert(length >= 0 && length < NATIVE_FORMAT_LIMIT);
	result = (char *)1;
	actual = format_test_vasprintf(&result, format, ap);
	va_end(ap);
	assert(actual == length && result != NULL);
	assert(strcmp(result, expected) == 0);
	assert(allocation_size(result) == (size_t)length + 1);
	assert(live == 1);
	check_capacities((size_t)length);
	format_test_free(result);
	assert(live == 0);
}

static void
check_preserved_va_list(const char *format, ...)
{
	va_list ap;
	char *first, *second;
	int a, b;

	va_start(ap, format);
	a = format_test_vasprintf(&first, format, ap);
	b = format_test_vasprintf(&second, format, ap);
	va_end(ap);
	assert(a >= 0 && b == a && strcmp(first, second) == 0);
	format_test_free(first);
	format_test_free(second);
	assert(live == 0 && zero_calls == 0);
}

static void
check_successes(void)
{
	const size_t lengths[] = { 0, 1, 62, 63, 64, 65, 126, 127, 128, 129,
	    255, 256, 511, 512, 1023, 1024, 16383, 16384, 32765, 32766 };
	char long_text[NATIVE_FORMAT_LIMIT + 1], *result;
	unsigned int selected;
	size_t i;

	memset(long_text, 'x', sizeof long_text);
	long_text[sizeof long_text - 1] = '\0';
	for (selected = OLD_TRUNCATION; selected <= C99_LENGTH; selected++) {
		for (i = 0; i < sizeof lengths / sizeof lengths[0]; i++) {
			reset((enum formatter_mode)selected, 0);
			check_format("%.*s", (int)lengths[i], long_text);
		}
		reset((enum formatter_mode)selected, 0);
		check_format("[%s] %d %lu %08x %.*s %c %%", "prefix", -12345,
		    123456789UL, 0xabcdU, 180, long_text, 'Z');
		reset((enum formatter_mode)selected, 0);
		check_preserved_va_list("%s:%d:%.*s", "again", 42, 400, long_text);
		reset((enum formatter_mode)selected, 0);
		assert(format_test_asprintf(&result, "%s/%d", "wrapper", -17) == 11);
		assert(strcmp(result, "wrapper/-17") == 0);
		assert(allocation_size(result) == 12 && capacities[0] == 64);
		format_test_free(result);
	}
}

static void
check_allocation_failures(void)
{
	char text[501], *result;
	unsigned int selected, count, fail;

	memset(text, 'q', sizeof text - 1);
	text[sizeof text - 1] = '\0';
	for (selected = OLD_TRUNCATION; selected <= C99_LENGTH; selected++) {
		reset((enum formatter_mode)selected, 0);
		assert(format_test_asprintf(&result, "%s:%d", text, 37) == 503);
		count = malloc_calls;
		format_test_free(result);
		for (fail = 1; fail <= count; fail++) {
			reset((enum formatter_mode)selected, fail);
			result = (char *)1;
			assert(format_test_asprintf(&result, "%s:%d", text, 37) == -1);
			assert(result == NULL && live == 0 && zero_calls == 0);
			assert(errno == ENOMEM && malloc_calls == fail);
		}
	}
	reset(OLD_TRUNCATION, 1);
	result = (char *)1;
	assert(format_test_asprintf(&result, "%s", "short") == -1);
	assert(result == NULL && live == 0 && calls == 1 && errno == ENOMEM);
}

static void
check_overflow_guards(void)
{
	char text[NATIVE_FORMAT_LIMIT + 1], *result;
	unsigned int selected;

	memset(text, 'r', sizeof text - 1);
	text[sizeof text - 1] = '\0';
	for (selected = OLD_TRUNCATION; selected <= OVERSIZE_LENGTH; selected++) {
		reset((enum formatter_mode)selected, 0);
		result = (char *)1;
		assert(format_test_asprintf(&result, "%s", text) == -1);
		assert(result == NULL && live == 0 && zero_calls == 0);
		assert(calls > 0 && calls <= 11 && errno == ENOMEM);
		if (selected == OLD_TRUNCATION || selected == ALWAYS_NEGATIVE)
			assert(capacities[calls - 1] == NATIVE_FORMAT_LIMIT);
		else
			assert(calls == 1);
	}
}

int
main(void)
{
	check_successes();
	check_allocation_failures();
	check_overflow_guards();
	puts("old/C99 formatting retries, exact allocations, va_list reuse, faults and 16-bit bounds passed");
	return 0;
}
