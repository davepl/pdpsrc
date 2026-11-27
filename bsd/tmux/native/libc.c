/* Small libc interfaces missing from the installed 2.11BSD library. */
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

extern void bcopy(const void *, void *, size_t);

void *
memmove(dst, src, len)
void *dst;
const void *src;
size_t len;
{
	if (len != 0)
		bcopy(src, dst, len);
	return (dst);
}

void *
bsearch(key, base, count, size, compare)
const void *key, *base;
size_t count, size;
int (*compare)(const void *, const void *);
{
	const char *first, *middle;
	size_t half;
	int order;

	if (size == 0)
		return (NULL);
	first = base;
	while (count != 0) {
		half = count / 2;
		middle = first + half * size;
		order = (*compare)(key, middle);
		if (order == 0)
			return ((void *)middle);
		if (order < 0)
			count = half;
		else {
			first = middle + size;
			count -= half + 1;
		}
	}
	return (NULL);
}

#ifndef TMUX_MILESTONE
struct tm *
gmtime_r(clock, result)
const time_t *clock;
struct tm *result;
{
	struct tm *shared;

	if ((shared = gmtime(clock)) == NULL)
		return (NULL);
	*result = *shared;
	return (result);
}
#endif
