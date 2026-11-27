/*
 * fdpass - pass an open file descriptor over a UNIX domain stream
 * socketpair with sendmsg(2)/recvmsg(2) access rights, 4.3BSD style.
 * The child reads from the descriptor it received.
 *
 * Build natively on 2.11BSD: cc -O -o fdpass fdpass.c
 *
 * A kernel without the recvmsg() fix in recvmsg.patch panics with
 * "trap type 0" when the child calls recvmsg().  Run sync first.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <stdio.h>
#include <errno.h>

extern int errno;

main(argc, argv)
	int argc;
	char **argv;
{
	int sv[2], fd, newfd, n, pid, status;
	struct msghdr msg;
	struct iovec iov;
	char buf[64], c;
	char *file;

	file = argc > 1 ? argv[1] : "/etc/motd";
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		perror("socketpair");
		exit(1);
	}
	fd = open(file, O_RDONLY, 0);
	if (fd < 0) {
		perror(file);
		exit(1);
	}
	printf("parent: sv=%d,%d fd=%d\n", sv[0], sv[1], fd);
	fflush(stdout);
	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(1);
	}
	if (pid == 0) {
		close(sv[0]);
		close(fd);
		bzero((char *)&msg, sizeof (msg));
		c = '?';
		iov.iov_base = &c;
		iov.iov_len = 1;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		newfd = -1;
		msg.msg_accrights = (caddr_t)&newfd;
		msg.msg_accrightslen = sizeof (newfd);
		errno = 0;
		n = recvmsg(sv[1], &msg, 0);
		printf("child: recvmsg=%d errno=%d rightslen=%d newfd=%d c=%c\n",
		    n, errno, msg.msg_accrightslen, newfd, c);
		fflush(stdout);
		if (newfd >= 0) {
			n = read(newfd, buf, 40);
			if (n < 0)
				perror("child: read");
			else {
				buf[n] = 0;
				printf("child: read %d bytes: %s\n", n, buf);
			}
		}
		exit(0);
	}
	close(sv[1]);
	bzero((char *)&msg, sizeof (msg));
	c = 'X';
	iov.iov_base = &c;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_accrights = (caddr_t)&fd;
	msg.msg_accrightslen = sizeof (fd);
	errno = 0;
	n = sendmsg(sv[0], &msg, 0);
	printf("parent: sendmsg=%d errno=%d\n", n, errno);
	fflush(stdout);
	close(fd);
	wait(&status);
	printf("parent: child status %o\n", status);
	exit(0);
}
