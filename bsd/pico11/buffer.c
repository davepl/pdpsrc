#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "pdp11_compat.h"
#include "buffer.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int
scratch(void)
{
	char path[32];
	int fd;

	(void)strcpy(path, "/tmp/pico11XXXXXX");
	fd = mkstemp(path);
	if (fd >= 0)
		(void)unlink(path);
	return fd;
}

static void
uncache(struct buffer *b)
{
	b->cachefd = -1;
	b->cachelen = 0;
}

static int
putone(int fd, int ch)
{
	unsigned char c;

	c = (unsigned char)ch;
	return write(fd, (char *)&c, 1) == 1 ? 0 : -1;
}

static int
popone(int fd, off_t *len)
{
	unsigned char c;

	if (*len == 0)
		return -1;
	if (lseek(fd, *len - 1, L_SET) < 0)
		return -1;
	if (read(fd, (char *)&c, 1) != 1)
		return -1;
	--*len;
	if (ftruncate(fd, *len) < 0)
		return -1;
	return (int)c;
}

int
buf_open(struct buffer *b, char *name)
{
	int in;
	char block[BUF_CACHE];
	char swap;
	int n;
	int i;
	off_t left;
	off_t end;

	b->left = scratch();
	b->right = scratch();
	b->nleft = 0;
	b->nright = 0;
	b->changed = 0;
	uncache(b);
	if (b->left < 0 || b->right < 0)
		return -1;
	if (name == (char *)0 || *name == '\0')
		return 0;
	in = open(name, O_RDONLY | O_BINARY, 0);
	if (in < 0) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	end = lseek(in, (off_t)0, L_XTND);
	if (end < 0) {
		(void)close(in);
		return -1;
	}
	while (end > 0) {
		n = end > BUF_CACHE ? BUF_CACHE : (int)end;
		left = end - n;
		if (lseek(in, left, L_SET) < 0 || read(in, block, n) != n) {
			(void)close(in);
			return -1;
		}
		for (i = 0; i < n / 2; ++i) {
			swap = block[i];
			block[i] = block[n - 1 - i];
			block[n - 1 - i] = swap;
		}
		if (write(b->right, block, n) != n) {
			(void)close(in);
			return -1;
		}
		b->nright += n;
		end = left;
	}
	(void)close(in);
	return 0;
}

void
buf_close(struct buffer *b)
{
	if (b->left >= 0)
		(void)close(b->left);
	if (b->right >= 0)
		(void)close(b->right);
	b->left = b->right = -1;
}

off_t
buf_size(struct buffer *b)
{
	return b->nleft + b->nright;
}

off_t
buf_pos(struct buffer *b)
{
	return b->nleft;
}

int
buf_get(struct buffer *b, off_t pos)
{
	int fd;
	off_t physical;
	off_t base;
	int n;

	if (pos < 0 || pos >= buf_size(b))
		return -1;
	if (pos < b->nleft) {
		fd = b->left;
		physical = pos;
	} else {
		fd = b->right;
		physical = b->nright - 1 - (pos - b->nleft);
	}
	base = physical - physical % BUF_CACHE;
	if (b->cachefd != fd || physical < b->cachebase ||
	    physical >= b->cachebase + b->cachelen) {
		if (lseek(fd, base, L_SET) < 0)
			return -1;
		n = read(fd, (char *)b->cache, BUF_CACHE);
		if (n <= 0)
			return -1;
		b->cachefd = fd;
		b->cachebase = base;
		b->cachelen = n;
	}
	return b->cache[(int)(physical - b->cachebase)];
}

int
buf_insert(struct buffer *b, int ch)
{
	if (lseek(b->left, b->nleft, L_SET) < 0 || putone(b->left, ch) < 0)
		return -1;
	++b->nleft;
	b->changed = 1;
	uncache(b);
	return 0;
}

int
buf_backspace(struct buffer *b)
{
	int ch;

	ch = popone(b->left, &b->nleft);
	if (ch < 0)
		return -1;
	b->changed = 1;
	uncache(b);
	return ch;
}

int
buf_delete(struct buffer *b)
{
	int ch;

	ch = popone(b->right, &b->nright);
	if (ch < 0)
		return -1;
	b->changed = 1;
	uncache(b);
	return ch;
}

int
buf_left(struct buffer *b)
{
	int ch;

	ch = popone(b->left, &b->nleft);
	if (ch < 0)
		return -1;
	if (lseek(b->right, b->nright, L_SET) < 0 || putone(b->right, ch) < 0)
		return -1;
	++b->nright;
	uncache(b);
	return 0;
}

int
buf_right(struct buffer *b)
{
	int ch;

	ch = popone(b->right, &b->nright);
	if (ch < 0)
		return -1;
	if (lseek(b->left, b->nleft, L_SET) < 0 || putone(b->left, ch) < 0)
		return -1;
	++b->nleft;
	uncache(b);
	return 0;
}

int
buf_seek(struct buffer *b, off_t pos)
{
	if (pos < 0 || pos > buf_size(b))
		return -1;
	while (b->nleft > pos) {
		if (buf_left(b) < 0)
			return -1;
	}
	while (b->nleft < pos) {
		if (buf_right(b) < 0)
			return -1;
	}
	return 0;
}

static int
copy_forward(int out, int in, off_t len)
{
	char block[BUF_CACHE];
	int want;
	int n;

	if (lseek(in, (off_t)0, L_SET) < 0)
		return -1;
	while (len > 0) {
		want = len > BUF_CACHE ? BUF_CACHE : (int)len;
		n = read(in, block, want);
		if (n != want || write(out, block, n) != n)
			return -1;
		len -= n;
	}
	return 0;
}

static int
copy_reverse(int out, int in, off_t len)
{
	char block[BUF_CACHE];
	char rev[BUF_CACHE];
	off_t start;
	int want;
	int n;
	int i;

	while (len > 0) {
		want = len > BUF_CACHE ? BUF_CACHE : (int)len;
		start = len - want;
		if (lseek(in, start, L_SET) < 0)
			return -1;
		n = read(in, block, want);
		if (n != want)
			return -1;
		for (i = 0; i < n; ++i)
			rev[i] = block[n - 1 - i];
		if (write(out, rev, n) != n)
			return -1;
		len -= n;
	}
	return 0;
}

int
buf_save(struct buffer *b, char *name)
{
	char temp[512];
	char *slash;
	struct stat st;
	int out;
	int ok;
	int have_mode;

	if (strlen(name) > sizeof temp - 16)
		return -1;
	slash = strrchr(name, '/');
	if (slash != (char *)0) {
		(void)memcpy(temp, name, slash - name + 1);
		temp[slash - name + 1] = '\0';
		(void)strcat(temp, ".p11XXXXXX");
	} else
		(void)strcpy(temp, ".p11XXXXXX");
	have_mode = stat(name, &st) == 0;
	out = mkstemp(temp);
	if (out < 0)
		return -1;
	if (have_mode)
		(void)fchmod(out, st.st_mode & 07777);
	ok = copy_forward(out, b->left, b->nleft);
	if (ok == 0)
		ok = copy_reverse(out, b->right, b->nright);
	if (close(out) < 0)
		ok = -1;
	if (ok == 0 && rename(temp, name) < 0)
		ok = -1;
	if (ok < 0)
		(void)unlink(temp);
	else
		b->changed = 0;
	return ok;
}
