/* BSD pty allocation and controlling-terminal setup for 2.11BSD. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "termios.h"

pid_t
forkpty(master, name, tio, ws)
int *master;
char *name;
struct termios *tio;
struct winsize *ws;
{
	char path[11], *bank, *unit;
	int m, s, fd, saved, pgrp, ready[2], n;
	char ok;
	pid_t pid;
	struct termios def;

	strcpy(path, "/dev/ptyp0");
	m = -1;
	for (bank = "pqrstuvwxyzPQRST"; *bank && m == -1; bank++) {
		path[8] = *bank;
		for (unit = "0123456789abcdef"; *unit; unit++) {
			path[9] = *unit;
			m = open(path, O_RDWR);
			if (m == -1)
				continue;
			path[5] = 't';
			if (access(path, R_OK|W_OK) == 0)
				break;
			path[5] = 'p';
			close(m);
			m = -1;
		}
	}
	if (m == -1) {
		errno = ENXIO;
		return (-1);
	}
	if (name != NULL)
		strcpy(name, path);
	if (pipe(ready) == -1) {
		saved = errno;
		close(m);
		errno = saved;
		return (-1);
	}
	pid = fork();
	if (pid == -1) {
		saved = errno;
		close(ready[0]); close(ready[1]);
		close(m);
		errno = saved;
		return (-1);
	}
	if (pid != 0) {
		close(ready[1]);
		do { n = read(ready[0], &ok, 1); } while (n == -1 && errno == EINTR);
		close(ready[0]);
		/* Do not expose a master whose slave has not yet been opened. */
		if (n != 1) {
			close(m);
			errno = EIO;
			return (-1);
		}
		*master = m;
		return (pid);
	}
	close(ready[0]);
	fd = open("/dev/tty", O_RDWR);
	if (fd != -1) {
		ioctl(fd, TIOCNOTTY, (char *)0);
		close(fd);
	}
	/* ttyopen assigns a controlling tty only to a process in group zero. */
	if (setpgrp(0, 0) == -1)
		_exit(127);
	if ((s = open(path, O_RDWR)) == -1)
		_exit(127);
	close(m);
	if (tio == NULL) {
		memset(&def, 0, sizeof def);
		def.sg.sg_ispeed = def.sg.sg_ospeed = B9600;
		def.sg.sg_flags = ECHO|CRMOD|ANYP;
		def.discipline = NTTYDISC;
		def.lmode = LCRTBS|LCRTERA|LCRTKIL|LCTLECH|LPASS8;
		memset(def.c_cc, 0377, sizeof def.c_cc);
		def.c_cc[VERASE] = 0177; def.c_cc[VKILL] = 025;
		def.c_cc[VINTR] = 003; def.c_cc[VQUIT] = 034;
		def.c_cc[VSTART] = 021; def.c_cc[VSTOP] = 023;
		def.c_cc[VEOF] = 004; def.c_cc[VSUSP] = 032;
		def.c_cc[VDSUSP] = 031; def.c_cc[VREPRINT] = 022;
		def.c_cc[VDISCARD] = 017; def.c_cc[VWERASE] = 027;
		def.c_cc[VLNEXT] = 026;
		tio = &def;
	}
	if (tcsetattr(s, TCSANOW, tio) == -1 ||
	    (ws != NULL && ioctl(s, TIOCSWINSZ, ws) == -1))
		_exit(127);
	pgrp = getpid();
	if (setpgrp(0, pgrp) == -1 || ioctl(s, TIOCSPGRP, &pgrp) == -1)
		_exit(127);
	if (dup2(s, 0) == -1 || dup2(s, 1) == -1 || dup2(s, 2) == -1)
		_exit(127);
	if (s > 2)
		close(s);
	ok = 1;
	do { n = write(ready[1], &ok, 1); } while (n == -1 && errno == EINTR);
	close(ready[1]);
	if (n != 1)
		_exit(127);
	return (0);
}
