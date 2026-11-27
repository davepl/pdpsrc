/* Native no-rights branches, using host memory and deterministic I/O shims. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "tmux.h"

static unsigned char stream[100000];
static size_t stream_length, stream_offset, read_chunk, write_chunk;
static size_t seen_bytes;
static int seen_iov, reads, writes, closes, read_error, write_error, write_zero;

static void
reset_io(void)
{
	stream_length = stream_offset = 0;
	read_chunk = write_chunk = sizeof stream;
	seen_bytes = 0;
	seen_iov = reads = writes = closes = read_error = write_error = write_zero = 0;
}

ssize_t
test_read(int fd, void *buffer, size_t capacity)
{
	size_t count;
	(void)fd;
	reads++;
	assert(capacity > 0 && capacity <= READ_BUF_SIZE);
	if (read_error) {
		errno = read_error;
		return -1;
	}
	count = stream_length - stream_offset;
	if (count > capacity)
		count = capacity;
	if (count > read_chunk)
		count = read_chunk;
	memcpy(buffer, stream + stream_offset, count);
	stream_offset += count;
	return (ssize_t)count;
}

ssize_t
test_writev(int fd, const struct iovec *iov, int count)
{
	size_t remaining, size;
	int i;
	(void)fd;
	writes++;
	seen_iov = count;
	seen_bytes = 0;
	assert(count > 0 && count <= 15);
	for (i = 0; i < count; i++)
		seen_bytes += iov[i].iov_len;
	assert(seen_bytes <= 32767);
	if (write_error) {
		errno = write_error;
		return -1;
	}
	if (write_zero)
		return 0;
	remaining = seen_bytes < write_chunk ? seen_bytes : write_chunk;
	size = remaining;
	for (i = 0; i < count && remaining != 0; i++) {
		size_t part = iov[i].iov_len < remaining ? iov[i].iov_len : remaining;
		assert(stream_length + part <= sizeof stream);
		memcpy(stream + stream_length, iov[i].iov_base, part);
		stream_length += part;
		remaining -= part;
	}
	return (ssize_t)size;
}

int
test_close(int fd)
{
	(void)fd;
	closes++;
	return 0;
}

static struct buf *
add_buffer(struct msgbuf *queue, size_t length, int descriptor)
{
	struct buf *buffer = buf_open(length);
	assert(buffer != NULL);
	memset(buffer->buf, 'x', length);
	buffer->wpos = length;
	buffer->fd = descriptor;
	buf_close(queue, buffer);
	return buffer;
}

static void
check_write_limits_and_partial(void)
{
	struct msgbuf queue;
	struct buf *buffer;
	int i;

	reset_io();
	msgbuf_init(&queue);
	for (i = 0; i < 16; i++)
		add_buffer(&queue, 64, -1);
	assert(msgbuf_write(&queue) == 0);
	assert(seen_iov == 15 && seen_bytes == 960 && queue.queued == 1);
	assert(msgbuf_write(&queue) == 0 && queue.queued == 0 && seen_bytes == 64);

	reset_io();
	msgbuf_init(&queue);
	buffer = add_buffer(&queue, 32768, -1);
	assert(msgbuf_write(&queue) == 0 && seen_bytes == 32767);
	assert(queue.queued == 1 && buffer->rpos == 32767);
	assert(msgbuf_write(&queue) == 0 && queue.queued == 0 && seen_bytes == 1);

	reset_io();
	msgbuf_init(&queue);
	for (i = 0; i < 8; i++)
		add_buffer(&queue, 4096, -1);
	assert(msgbuf_write(&queue) == 0 && seen_iov == 8 && seen_bytes == 32767);
	assert(queue.queued == 1 && TAILQ_FIRST(&queue.bufs)->rpos == 4095);
	assert(msgbuf_write(&queue) == 0 && queue.queued == 0 && seen_bytes == 1);

	reset_io();
	msgbuf_init(&queue);
	buffer = add_buffer(&queue, 100, -1);
	write_chunk = 3;
	assert(msgbuf_write(&queue) == 0 && buffer->rpos == 3 && queue.queued == 1);
	write_chunk = sizeof stream;
	assert(msgbuf_write(&queue) == 0 && queue.queued == 0 && stream_length == 100);
	assert(closes == 0);
}

static void
check_write_errors_and_descriptor_rejection(void)
{
	const int errors[] = { EAGAIN, ENOBUFS, EINTR, EIO };
	struct msgbuf queue;
	struct buf *buffer;
	size_t i;

	for (i = 0; i < sizeof errors / sizeof errors[0]; i++) {
		reset_io();
		msgbuf_init(&queue);
		buffer = add_buffer(&queue, 100, -1);
		write_error = errors[i];
		assert(msgbuf_write(&queue) == (errors[i] == EIO ? -1 : 0));
		assert(buffer->rpos == 0 && queue.queued == 1 && stream_length == 0);
		msgbuf_clear(&queue);
	}
	reset_io();
	msgbuf_init(&queue);
	buffer = add_buffer(&queue, 100, -1);
	write_zero = 1;
	errno = EIO;
	assert(msgbuf_write(&queue) == -2 && errno == 0);
	assert(buffer->rpos == 0 && queue.queued == 1);
	msgbuf_clear(&queue);

	reset_io();
	msgbuf_init(&queue);
	buffer = add_buffer(&queue, 100, 71);
	assert(msgbuf_write(&queue) == -1 && errno == EOPNOTSUPP);
	assert(writes == 0 && closes == 0 && buffer->fd == 71 && buffer->rpos == 0);
	assert(buf_write(&queue) == -1 && errno == EOPNOTSUPP);
	assert(writes == 0 && queue.queued == 1);
	msgbuf_clear(&queue);
	assert(closes == 1);
}

static void
check_compose_and_framing(void)
{
	struct imsgbuf outgoing, incoming;
	struct imsg message;
	struct iovec parts[2];
	char payload[] = "hello";
	ssize_t n;
	int messages = 0;

	reset_io();
	imsg_init(&outgoing, 10);
	imsg_init(&incoming, 11);
	parts[0].iov_base = payload;
	parts[0].iov_len = 2;
	parts[1].iov_base = payload + 2;
	parts[1].iov_len = 3;
	assert(imsg_compose(&outgoing, 1, 7, 0, 71, payload, 5) == -1);
	assert(errno == EOPNOTSUPP && outgoing.w.queued == 0 && closes == 0);
	assert(imsg_composev(&outgoing, 1, 7, 0, 72, parts, 2) == -1);
	assert(errno == EOPNOTSUPP && outgoing.w.queued == 0 && closes == 0);
	assert(imsg_compose(&outgoing, 1, 7, 0, -1, payload, 5) == 1);
	assert(imsg_composev(&outgoing, 2, 9, 123, -1, parts, 2) == 1);
	assert(imsg_compose(&outgoing, 3, 11, 0, -1, NULL, 0) == 1);
	write_chunk = 3;
	while (outgoing.w.queued != 0)
		assert(msgbuf_write(&outgoing.w) == 0);
	read_chunk = 5;
	while (stream_offset < stream_length) {
		assert(imsg_read(&incoming) > 0);
		while ((n = imsg_get(&incoming, &message)) > 0) {
			messages++;
			assert(message.hdr.type == (u_int32_t)messages && message.fd == -1);
			assert(!(message.hdr.flags & IMSGF_HASFD));
			if (messages < 3)
				assert(n == (ssize_t)(IMSG_HEADER_SIZE + 5) &&
				    memcmp(message.data, payload, 5) == 0);
			else
				assert(n == IMSG_HEADER_SIZE && message.data == NULL);
			imsg_free(&message);
		}
		assert(n == 0);
	}
	assert(messages == 3 && incoming.r.wpos == 0 && closes == 0);
	assert(imsg_read(&incoming) == 0);
	imsg_clear(&outgoing);
	imsg_clear(&incoming);
}

static void
check_maximum_message(void)
{
	struct imsgbuf outgoing, incoming;
	struct imsg message;
	unsigned char payload[MAX_IMSGSIZE];
	size_t length = MAX_IMSGSIZE - IMSG_HEADER_SIZE;

	reset_io();
	imsg_init(&outgoing, 10);
	imsg_init(&incoming, 11);
	memset(payload, 0xa5, sizeof payload);
	assert(imsg_compose(&outgoing, 4, 0, 0, -1, payload, length + 1) == -1);
	assert(errno == ERANGE && outgoing.w.queued == 0);
	assert(imsg_compose(&outgoing, 4, 0, 0, -1, payload, length) == 1);
	assert(msgbuf_write(&outgoing.w) == 0 && outgoing.w.queued == 0);
	assert(imsg_read(&incoming) == MAX_IMSGSIZE);
	assert(imsg_get(&incoming, &message) == MAX_IMSGSIZE);
	assert(message.fd == -1 && memcmp(message.data, payload, length) == 0);
	imsg_free(&message);
	assert(incoming.r.wpos == 0);
	imsg_clear(&outgoing);
	imsg_clear(&incoming);
}

static void
check_read_errors_and_bad_headers(void)
{
	const int errors[] = { EAGAIN, EINTR, EIO };
	struct imsgbuf incoming;
	struct imsg message;
	struct imsg_hdr header;
	size_t i;

	for (i = 0; i < sizeof errors / sizeof errors[0]; i++) {
		reset_io();
		imsg_init(&incoming, 11);
		read_error = errors[i];
		assert(imsg_read(&incoming) == (errors[i] == EIO ? -1 : -2));
		assert(incoming.r.wpos == 0);
	}
	reset_io();
	imsg_init(&incoming, 11);
	incoming.r.wpos = READ_BUF_SIZE;
	assert(imsg_read(&incoming) == -1 && errno == ENOBUFS && reads == 0);

	imsg_init(&incoming, 11);
	memset(&header, 0, sizeof header);
	header.len = IMSG_HEADER_SIZE;
	header.flags = IMSGF_HASFD;
	memcpy(incoming.r.buf, &header, sizeof header);
	incoming.r.wpos = sizeof header;
	assert(imsg_get(&incoming, &message) == -1 && errno == EOPNOTSUPP);
	assert(incoming.r.wpos == sizeof header && TAILQ_EMPTY(&incoming.fds));
	header.len = IMSG_HEADER_SIZE - 1;
	memcpy(incoming.r.buf, &header, sizeof header);
	assert(imsg_get(&incoming, &message) == -1 && errno == ERANGE);
}

int
main(void)
{
	check_write_limits_and_partial();
	check_write_errors_and_descriptor_rejection();
	check_compose_and_framing();
	check_maximum_message();
	check_read_errors_and_bad_headers();
	puts("native read/writev imsg framing, bounds, retries, partial I/O and FD rejection passed");
	return 0;
}
