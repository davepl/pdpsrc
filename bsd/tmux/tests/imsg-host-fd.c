/* Ensure the ordinary host branch still transfers real descriptors. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "tmux.h"

int
main(void)
{
	struct imsgbuf sender, receiver;
	struct imsg message;
	int sockets[2], pipefds[2], original;
	char payload[] = "host", byte;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	assert(pipe(pipefds) == 0);
	original = pipefds[0];
	imsg_init(&sender, sockets[0]);
	imsg_init(&receiver, sockets[1]);
	assert(imsg_compose(&sender, 42, 7, 0, original, payload, 4) == 1);
	assert(msgbuf_write(&sender.w) == 0 && sender.w.queued == 0);
	assert(fcntl(original, F_GETFD) == -1 && errno == EBADF);
	assert(imsg_read(&receiver) > 0);
	assert(imsg_get(&receiver, &message) == IMSG_HEADER_SIZE + 4);
	assert(message.hdr.type == 42 && message.hdr.flags & IMSGF_HASFD);
	assert(message.fd >= 0 && memcmp(message.data, payload, 4) == 0);
	assert(write(pipefds[1], "Z", 1) == 1);
	assert(read(message.fd, &byte, 1) == 1 && byte == 'Z');
	close(message.fd);
	close(pipefds[1]);
	imsg_free(&message);
	imsg_clear(&sender);
	imsg_clear(&receiver);
	close(sockets[0]);
	close(sockets[1]);
	puts("ordinary host imsg still transfers real descriptors");
	return 0;
}
