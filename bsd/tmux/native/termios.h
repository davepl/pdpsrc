/* The terminal state tmux needs, backed by the native 2.11BSD tty ioctls. */
#ifndef TMUX_NATIVE_TERMIOS_H
#define TMUX_NATIVE_TERMIOS_H
#include <sys/types.h>
#include <sys/ioctl.h>

typedef unsigned char cc_t;

#define NCC 20
#define NCCS NCC
#define VEOF 0
#define VEOL 1
#define VEOL2 2
#define VERASE 3
#define VWERASE 4
#define VKILL 5
#define VREPRINT 6
#define VINTR 8
#define VQUIT 9
#define VSUSP 10
#define VDSUSP 11
#define VSTART 12
#define VSTOP 13
#define VLNEXT 14
#define VDISCARD 15
#define VMIN 16
#define VTIME 17
#define TCSANOW 0
#define TCSAFLUSH 1

struct termios {
	struct sgttyb sg;
	struct tchars tc;
	struct ltchars ltc;
	int lmode;
	int discipline;
	unsigned char c_cc[NCCS];
	int tmux_raw;
};
int tcgetattr(int, struct termios *);
int tcsetattr(int, int, const struct termios *);
void tmux_cfmakeraw(struct termios *);
#endif
