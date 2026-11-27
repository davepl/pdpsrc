/* Actual native xsnprintf/xvsnprintf with old formatter behavior emulated. */
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tmux.h"

static jmp_buf fatal_return;
static int expecting_fatal, was_fatalx, formatter_calls, zero_calls, formatter_error;
static size_t last_capacity;
static char *last_buffer;
static char fatal_message[80];

static void
reset(void)
{
	expecting_fatal = was_fatalx = formatter_calls = zero_calls = formatter_error = 0;
	last_capacity = 0;
	last_buffer = NULL;
	fatal_message[0] = '\0';
}

static void
record_fatal(int extended, const char *format, va_list ap)
{
	assert(expecting_fatal);
	was_fatalx = extended;
	vsnprintf(fatal_message, sizeof fatal_message, format, ap);
	longjmp(fatal_return, 1);
}

void
fatal(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	record_fatal(0, format, ap);
	abort();
}

void
fatalx(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	record_fatal(1, format, ap);
	abort();
}

void
log_warnx(const char *format, ...)
{
	(void)format;
	abort();
}

int
test_vsnprintf(char *buffer, size_t capacity, const char *format, va_list ap)
{
	formatter_calls++;
	last_capacity = capacity;
	last_buffer = buffer;
	if (capacity == 0) {
		zero_calls++;
		errno = EINVAL;
		return -1;
	}
	assert(buffer != NULL);
	if (formatter_error) {
		errno = EINVAL;
		return -1;
	}
	/* Like the probed native formatter, count all output even when truncated. */
	return vsnprintf(buffer, capacity, format, ap);
}

size_t
test_strlcpy(char *destination, const char *source, size_t capacity)
{
	size_t length = strlen(source), copied;
	if (capacity != 0) {
		copied = length < capacity - 1 ? length : capacity - 1;
		memcpy(destination, source, copied);
		destination[copied] = '\0';
	}
	return length;
}

int
test_unused_vasprintf(char **result, const char *format, va_list ap)
{
	(void)result;
	(void)format;
	(void)ap;
	abort();
}

static int
via_va_list(char *buffer, size_t capacity, const char *format, ...)
{
	va_list ap;
	int length;
	va_start(ap, format);
	length = xvsnprintf(buffer, capacity, format, ap);
	va_end(ap);
	return length;
}

static void
check_measurements(void)
{
	char untouched[3] = { 'L', 'M', 'R' };
	char expected[256];
	int length;

	reset();
	assert(xsnprintf(NULL, 0, "%s", "") == 0);
	assert(formatter_calls == 1 && last_capacity == 1 && last_buffer != NULL);
	assert(zero_calls == 0);
	reset();
	length = snprintf(expected, sizeof expected, "%s:%d:%lu:%c:%.*s",
	    "window", -123, 123456789UL, 'Z', 5, "longname");
	assert(xsnprintf(NULL, 0, "%s:%d:%lu:%c:%.*s",
	    "window", -123, 123456789UL, 'Z', 5, "longname") == length);
	assert(formatter_calls == 1 && last_capacity == 1 && zero_calls == 0);
	reset();
	assert(via_va_list(&untouched[1], 0, "%s/%d", "title", 42) == 8);
	assert(memcmp(untouched, "LMR", 3) == 0);
	assert(last_buffer != &untouched[1] && last_capacity == 1 && zero_calls == 0);
}

static void
check_real_buffers(void)
{
	char single[3] = { 'L', 'M', 'R' }, buffer[4], complete[32];

	reset();
	assert(xsnprintf(&single[1], 1, "%s", "window") == 6);
	assert(single[0] == 'L' && single[1] == '\0' && single[2] == 'R');
	assert(last_buffer == &single[1] && last_capacity == 1 && zero_calls == 0);
	reset();
	assert(via_va_list(buffer, sizeof buffer, "%s-%d", "abcdef", 42) == 9);
	assert(strcmp(buffer, "abc") == 0);
	assert(last_buffer == buffer && last_capacity == sizeof buffer && zero_calls == 0);
	reset();
	assert(xsnprintf(complete, sizeof complete, "%s-%d", "abcdef", 42) == 9);
	assert(strcmp(complete, "abcdef-42") == 0 && last_capacity == sizeof complete);
}

static void
check_failure_paths(void)
{
	char buffer[4];

	reset();
	expecting_fatal = 1;
	if (setjmp(fatal_return) == 0) {
		xsnprintf(NULL, 32768, "%s", "x");
		abort();
	}
	assert(was_fatalx && strcmp(fatal_message, "len > INT_MAX") == 0);
	assert(formatter_calls == 0);
	reset();
	expecting_fatal = formatter_error = 1;
	if (setjmp(fatal_return) == 0) {
		xsnprintf(buffer, sizeof buffer, "%s", "x");
		abort();
	}
	assert(!was_fatalx && strcmp(fatal_message, "vsnprintf failed") == 0);
	assert(formatter_calls == 1 && last_capacity == sizeof buffer && zero_calls == 0);
}

int
main(void)
{
	check_measurements();
	check_real_buffers();
	check_failure_paths();
	puts("native xsnprintf zero-size measurement, va_args, truncation, canaries and bounds passed");
	return 0;
}
