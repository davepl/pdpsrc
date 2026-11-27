/*
 * fdtest - exercise sendmsg(2)/recvmsg(2) and access rights passing
 * on 2.11BSD.  Build natively: cc -O -o fdtest fdtest.c
 *
 * A kernel without the recvmsg() fix in recvmsg.patch panics on the
 * first recvmsg() call.  "pstat -T" before and after shows whether
 * file table entries leaked.
 *
 *	fdtest loop n		pass n descriptors one at a time
 *	fdtest multi		pass three descriptors in one message
 *	fdtest dgram n		the same over a datagram socketpair
 *	fdtest drop n		send n descriptors that are never received
 *	fdtest iov		recvmsg into three iovecs, no rights
 *	fdtest norights		recv() a message that carries rights
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <stdio.h>
#include <errno.h>

extern int errno;
char *file = "/etc/motd";

sendfds(s, fds, nfds, c)
	int s, *fds, nfds;
	char c;
{
	struct msghdr msg;
	struct iovec iov;

	bzero((char *)&msg, sizeof (msg));
	iov.iov_base = &c;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_accrights = (caddr_t)fds;
	msg.msg_accrightslen = nfds * sizeof (int);
	return (sendmsg(s, &msg, 0));
}

recvfds(s, fds, nfds, cp)
	int s, *fds, nfds;
	char *cp;
{
	struct msghdr msg;
	struct iovec iov;
	int n;

	bzero((char *)&msg, sizeof (msg));
	iov.iov_base = cp;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_accrights = (caddr_t)fds;
	msg.msg_accrightslen = nfds * sizeof (int);
	n = recvmsg(s, &msg, 0);
	if (n < 0)
		return (-1);
	return (msg.msg_accrightslen / sizeof (int));
}

openfds()
{
	int i, n = 0;

	for (i = 0; i < 30; i++)
		if (fcntl(i, F_GETFL, 0) != -1)
			n++;
	return (n);
}

pair(type, sv)
	int type, *sv;
{
	if (socketpair(AF_UNIX, type, 0, sv) < 0) {
		perror("socketpair");
		exit(1);
	}
}

loop(n, type)
	int n, type;
{
	int sv[2], fd, nfd, i, got, pid, status, ok = 0;
	char c, buf[8];

	pair(type, sv);
	pid = fork();
	if (pid == 0) {
		close(sv[0]);
		for (i = 0; i < n; i++) {
			nfd = -1;
			got = recvfds(sv[1], &nfd, 1, &c);
			if (got != 1 || nfd < 0) {
				printf("child: %d: got=%d nfd=%d errno=%d\n",
				    i, got, nfd, errno);
				exit(1);
			}
			if (read(nfd, buf, 4) != 4 || buf[0] != '2')
				printf("child: %d: bad read on %d\n", i, nfd);
			else
				ok++;
			close(nfd);
			write(sv[1], "k", 1);
		}
		printf("child: received and read %d of %d, %d fds open\n",
		    ok, n, openfds());
		exit(0);
	}
	close(sv[1]);
	for (i = 0; i < n; i++) {
		fd = open(file, O_RDONLY, 0);
		if (sendfds(sv[0], &fd, 1, 'a') != 1) {
			printf("parent: %d: sendmsg errno=%d\n", i, errno);
			break;
		}
		close(fd);
		read(sv[0], &c, 1);
	}
	wait(&status);
	printf("parent: sent %d, child status %o, %d fds open\n", i, status,
	    openfds());
}

multi()
{
	int sv[2], fds[3], nfds[3], i, got, pid, status;
	char c, buf[8];

	pair(SOCK_STREAM, sv);
	fds[0] = open("/etc/motd", O_RDONLY, 0);
	fds[1] = open("/etc/passwd", O_RDONLY, 0);
	fds[2] = open("/etc/group", O_RDONLY, 0);
	pid = fork();
	if (pid == 0) {
		close(sv[0]);
		for (i = 0; i < 3; i++)
			close(fds[i]);
		nfds[0] = nfds[1] = nfds[2] = -1;
		got = recvfds(sv[1], nfds, 3, &c);
		printf("child: got %d rights: %d %d %d\n", got, nfds[0],
		    nfds[1], nfds[2]);
		for (i = 0; i < got; i++) {
			bzero(buf, sizeof (buf));
			read(nfds[i], buf, 5);
			printf("child: fd %d starts \"%s\"\n", nfds[i], buf);
		}
		exit(0);
	}
	close(sv[1]);
	printf("parent: sendmsg=%d errno=%d\n", sendfds(sv[0], fds, 3, 'm'),
	    errno);
	wait(&status);
	printf("parent: child status %o\n", status);
}

drop(n)
	int n;
{
	int sv[2], fd, i, r;

	for (i = 0; i < n; i++) {
		pair(SOCK_STREAM, sv);
		fd = open(file, O_RDONLY, 0);
		r = sendfds(sv[0], &fd, 1, 'd');
		if (r != 1) {
			printf("drop %d: sendmsg=%d errno=%d\n", i, r, errno);
			break;
		}
		close(fd);
		close(sv[0]);
		close(sv[1]);
	}
	printf("dropped %d messages with rights, %d fds open\n", i, openfds());
}

iovtest()
{
	int sv[2], n;
	struct msghdr msg;
	struct iovec iov[3];
	char a[4], b[4], c[12];

	pair(SOCK_STREAM, sv);
	write(sv[0], "ABCDEFGHIJ", 10);
	bzero(a, sizeof (a)); bzero(b, sizeof (b)); bzero(c, sizeof (c));
	bzero((char *)&msg, sizeof (msg));
	iov[0].iov_base = a; iov[0].iov_len = 3;
	iov[1].iov_base = b; iov[1].iov_len = 3;
	iov[2].iov_base = c; iov[2].iov_len = 10;
	msg.msg_iov = iov;
	msg.msg_iovlen = 3;
	n = recvmsg(sv[1], &msg, 0);
	printf("recvmsg=%d errno=%d \"%s\" \"%s\" \"%s\"\n", n, errno, a, b, c);
}

norights()
{
	int sv[2], fd, n, before;
	char c;

	pair(SOCK_STREAM, sv);
	fd = open(file, O_RDONLY, 0);
	before = openfds();
	sendfds(sv[0], &fd, 1, 'n');
	n = recv(sv[1], &c, 1, 0);
	printf("recv=%d c=%c fds open before %d after %d\n", n, c, before,
	    openfds());
}

main(argc, argv)
	int argc;
	char **argv;
{
	char *t = argc > 1 ? argv[1] : "";
	int n = argc > 2 ? atoi(argv[2]) : 1;

	if (!strcmp(t, "loop"))
		loop(n, SOCK_STREAM);
	else if (!strcmp(t, "multi"))
		multi();
	else if (!strcmp(t, "dgram"))
		loop(n, SOCK_DGRAM);
	else if (!strcmp(t, "drop"))
		drop(n);
	else if (!strcmp(t, "iov"))
		iovtest();
	else if (!strcmp(t, "norights"))
		norights();
	else {
		fprintf(stderr, "usage: fdtest test [arg]\n");
		exit(2);
	}
	exit(0);
}
