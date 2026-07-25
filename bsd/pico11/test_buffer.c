#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdp11_compat.h"
#include "buffer.h"

#define TEST_SIZE 200000L

static int
value_at(long i)
{
	return (int)(i % 251) + 1;
}

static void
fail(char *s)
{
	(void)fprintf(stderr, "test_buffer: %s\n", s);
	exit(1);
}

int
main(void)
{
	char source[32];
	char output[32];
	unsigned char block[256];
	struct buffer b;
	long made;
	long i;
	int fd;
	int n;
	int ch;

	(void)strcpy(source, "/tmp/p11srcXXXXXX");
	(void)strcpy(output, "/tmp/p11outXXXXXX");
	fd = mkstemp(source);
	if (fd < 0)
		fail("mkstemp source");
	made = 0;
	while (made < TEST_SIZE) {
		n = TEST_SIZE - made > (long)sizeof block ?
		    sizeof block : TEST_SIZE - made;
		for (i = 0; i < n; ++i)
			block[i] = value_at(made + i);
		if (write(fd, (char *)block, n) != n)
			fail("write source");
		made += n;
	}
	(void)close(fd);
	fd = mkstemp(output);
	if (fd < 0)
		fail("mkstemp output");
	(void)close(fd);
	(void)unlink(output);
	b.left = b.right = -1;
	if (buf_open(&b, source) < 0)
		fail("buf_open");
	if (buf_size(&b) != TEST_SIZE || buf_pos(&b) != 0) {
		(void)fprintf(stderr, "test_buffer: size %ld, position %ld\n",
		    (long)buf_size(&b), (long)buf_pos(&b));
		fail("initial size or position");
	}
	for (i = 0; i < TEST_SIZE; ++i) {
		if (buf_get(&b, i) != value_at(i))
			fail("loaded byte order");
	}
	if (buf_seek(&b, (off_t)1000) < 0 || b.changed)
		fail("seek changed document");
	if (buf_insert(&b, 'Z') < 0)
		fail("insert");
	if (buf_delete(&b) != value_at(1000))
		fail("delete");
	if (buf_seek(&b, buf_size(&b)) < 0 || buf_insert(&b, 'Q') < 0)
		fail("append");
	if (buf_save(&b, output) < 0 || b.changed)
		fail("save");
	buf_close(&b);
	fd = open(output, O_RDONLY, 0);
	if (fd < 0)
		fail("open output");
	for (i = 0; i < TEST_SIZE + 1; ++i) {
		if (read(fd, (char *)&block[0], 1) != 1)
			fail("short output");
		ch = i == 1000 ? 'Z' : i == TEST_SIZE ? 'Q' : value_at(i);
		if (block[0] != ch)
			fail("saved contents");
	}
	if (read(fd, (char *)&block[0], 1) != 0)
		fail("long output");
	(void)close(fd);
	(void)unlink(source);
	(void)unlink(output);
	(void)printf("buffer tests passed\n");
	return 0;
}
