#ifndef BUFFER_H
#define BUFFER_H

#include <sys/types.h>

#define BUF_CACHE 256

struct buffer {
	int left;
	int right;
	off_t nleft;
	off_t nright;
	int changed;
	int cachefd;
	off_t cachebase;
	int cachelen;
	unsigned char cache[BUF_CACHE];
};

int buf_open(struct buffer *b, char *name);
void buf_close(struct buffer *b);
off_t buf_size(struct buffer *b);
off_t buf_pos(struct buffer *b);
int buf_get(struct buffer *b, off_t pos);
int buf_insert(struct buffer *b, int ch);
int buf_backspace(struct buffer *b);
int buf_delete(struct buffer *b);
int buf_left(struct buffer *b);
int buf_right(struct buffer *b);
int buf_seek(struct buffer *b, off_t pos);
int buf_save(struct buffer *b, char *name);

#endif
