#include <sys/types.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdio.h>
#include "pdp11_compat.h"
#include "terminal.h"

#if defined(pdp11) || defined(__pdp11__)
#define PICO_SGTTY 1
#endif

#ifdef PICO_SGTTY
#include <sgtty.h>
static struct sgttyb saved_tty;
#else
#include <termios.h>
static struct termios saved_tty;
#endif

static int opened;

int
term_open(void)
{
#ifdef PICO_SGTTY
	struct sgttyb t;

	if (gtty(0, &saved_tty) < 0)
		return -1;
	t = saved_tty;
	t.sg_flags &= ~(ECHO | CRMOD);
	t.sg_flags |= RAW;
	if (stty(0, &t) < 0)
		return -1;
#else
	struct termios t;

	if (tcgetattr(0, &saved_tty) < 0)
		return -1;
	t = saved_tty;
	t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	t.c_oflag &= ~(OPOST);
	t.c_cflag |= CS8;
	t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(0, TCSAFLUSH, &t) < 0)
		return -1;
#endif
	opened = 1;
	term_put("\033[?25l");
	return 0;
}

void
term_close(void)
{
	if (!opened)
		return;
	term_put("\033[?25h\033[0m\033[H\033[J");
#ifdef PICO_SGTTY
	(void)stty(0, &saved_tty);
#else
	(void)tcsetattr(0, TCSAFLUSH, &saved_tty);
#endif
	opened = 0;
}

void
term_size(int *rows, int *cols)
{
#ifdef TIOCGWINSZ
	struct winsize w;

	if (ioctl(1, TIOCGWINSZ, (char *)&w) == 0 && w.ws_row && w.ws_col) {
		*rows = w.ws_row;
		*cols = w.ws_col;
		return;
	}
#endif
	*rows = 24;
	*cols = 80;
}

void
term_write(char *s, int n)
{
	int done;
	int k;

	done = 0;
	while (done < n) {
		k = write(1, s + done, n - done);
		if (k <= 0)
			return;
		done += k;
	}
}

void
term_put(char *s)
{
	term_write(s, strlen(s));
}

void
term_move(int row, int col)
{
	char seq[32];

	(void)sprintf(seq, "\033[%d;%dH", row + 1, col + 1);
	term_put(seq);
}

void
term_clear(void)
{
	term_put("\033[H\033[J");
}

static int
readone(void)
{
	unsigned char c;

	if (read(0, (char *)&c, 1) != 1)
		return -1;
	return c;
}

int
term_key(void)
{
	int a;
	int b;
	int c;

	a = readone();
	if (a != 27)
		return a;
	b = readone();
	if (b != '[' && b != 'O')
		return 27;
	c = readone();
	if (c == 'A') return KEY_UP;
	if (c == 'B') return KEY_DOWN;
	if (c == 'C') return KEY_RIGHT;
	if (c == 'D') return KEY_LEFT;
	if (c == 'H') return KEY_HOME;
	if (c == 'F') return KEY_END;
	if (c >= '0' && c <= '9') {
		a = c - '0';
		c = readone();
		if (c >= '0' && c <= '9') {
			a = a * 10 + c - '0';
			c = readone();
		}
		if (c == '~') {
			if (a == 1 || a == 7) return KEY_HOME;
			if (a == 3) return KEY_DELETE;
			if (a == 4 || a == 8) return KEY_END;
			if (a == 5) return KEY_PGUP;
			if (a == 6) return KEY_PGDN;
		}
	}
	return 27;
}
