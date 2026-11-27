/* Preserve all native tty settings, including the local mode word. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include "termios.h"

int
tcgetattr(fd, t)
int fd;
struct termios *t;
{
	if (ioctl(fd, TIOCGETP, &t->sg) == -1 ||
	    ioctl(fd, TIOCGETC, &t->tc) == -1 ||
	    ioctl(fd, TIOCGLTC, &t->ltc) == -1 ||
	    ioctl(fd, TIOCLGET, &t->lmode) == -1 ||
	    ioctl(fd, TIOCGETD, &t->discipline) == -1)
		return (-1);
	memset(t->c_cc, 0377, sizeof t->c_cc);
	t->c_cc[VERASE] = t->sg.sg_erase;
	t->c_cc[VKILL] = t->sg.sg_kill;
	t->c_cc[VINTR] = t->tc.t_intrc;
	t->c_cc[VQUIT] = t->tc.t_quitc;
	t->c_cc[VSTART] = t->tc.t_startc;
	t->c_cc[VSTOP] = t->tc.t_stopc;
	t->c_cc[VEOF] = t->tc.t_eofc;
	t->c_cc[VEOL] = t->tc.t_brkc;
	t->c_cc[VSUSP] = t->ltc.t_suspc;
	t->c_cc[VDSUSP] = t->ltc.t_dsuspc;
	t->c_cc[VREPRINT] = t->ltc.t_rprntc;
	t->c_cc[VDISCARD] = t->ltc.t_flushc;
	t->c_cc[VWERASE] = t->ltc.t_werasc;
	t->c_cc[VLNEXT] = t->ltc.t_lnextc;
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
	t->tmux_raw = 0;
	return (0);
}

void
tmux_cfmakeraw(t)
struct termios *t;
{
	t->tmux_raw = 1;
}

int
tcsetattr(fd, action, t)
int fd, action;
const struct termios *t;
{
	struct sgttyb sg;
	struct tchars tc;
	struct ltchars ltc;
	int lmode, discipline, what;

	if (action != TCSANOW && action != TCSAFLUSH) {
		errno = EINVAL;
		return (-1);
	}
	sg = t->sg;
	tc = t->tc;
	ltc = t->ltc;
	lmode = t->lmode;
	discipline = t->discipline;
	sg.sg_erase = t->c_cc[VERASE];
	sg.sg_kill = t->c_cc[VKILL];
	tc.t_intrc = t->c_cc[VINTR];
	tc.t_quitc = t->c_cc[VQUIT];
	tc.t_startc = t->c_cc[VSTART];
	tc.t_stopc = t->c_cc[VSTOP];
	tc.t_eofc = t->c_cc[VEOF];
	tc.t_brkc = t->c_cc[VEOL];
	ltc.t_suspc = t->c_cc[VSUSP];
	ltc.t_dsuspc = t->c_cc[VDSUSP];
	ltc.t_rprntc = t->c_cc[VREPRINT];
	ltc.t_flushc = t->c_cc[VDISCARD];
	ltc.t_werasc = t->c_cc[VWERASE];
	ltc.t_lnextc = t->c_cc[VLNEXT];
	if (t->tmux_raw) {
		sg.sg_flags &= ~(ECHO|CRMOD|CBREAK|TANDEM|XTABS);
		sg.sg_flags |= RAW;
		lmode |= LLITOUT|LPASS8;
		lmode &= ~LFLUSHO;
	}
	if (ioctl(fd, TIOCSETD, &discipline) == -1 ||
	    ioctl(fd, TIOCSETN, &sg) == -1 ||
	    ioctl(fd, TIOCSETC, &tc) == -1 ||
	    ioctl(fd, TIOCSLTC, &ltc) == -1 ||
	    ioctl(fd, TIOCLSET, &lmode) == -1)
		return (-1);
	if (action == TCSAFLUSH) {
		what = 1; /* FREAD: discard pending input, not output. */
		if (ioctl(fd, TIOCFLUSH, &what) == -1)
			return (-1);
	}
	return (0);
}
