/* BSD fgetln interface with temporary, bounded native storage. */
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *
fgetln(fp, len)
FILE *fp;
size_t *len;
{
	static char *buf;
	static size_t space;
	char *grown;
	size_t used, next, maximum;
	int c, saved;

	*len = 0;
	used = 0;
	maximum = (size_t)-1;
	while ((c = getc(fp)) != EOF) {
		/* Keep one byte for the convenient, uncounted trailing NUL. */
		if (used == maximum - 1) {
			errno = ENOMEM;
			goto fail;
		}
		if (used + 1 >= space) {
			next = space > maximum - 256 ? maximum : space + 256;
			if (next <= used + 1 || (grown = malloc(next)) == NULL) {
				errno = ENOMEM;
				goto fail;
			}
			if (used != 0)
				memcpy(grown, buf, used);
			if (buf != NULL)
				free(buf);
			buf = grown;
			space = next;
		}
		buf[used++] = c;
		if (c == '\n')
			break;
	}
	if (c == EOF && (ferror(fp) || used == 0))
		goto fail;
	buf[used] = '\0';
	*len = used;
	return (buf);

fail:
	saved = errno;
	if (buf != NULL)
		free(buf);
	buf = NULL;
	space = 0;
	errno = saved;
	return (NULL);
}
