/* tmux's terminal capability interface, backed by native termcap. */
#ifndef TMUX_NATIVE_TERM_H
#define TMUX_NATIVE_TERM_H
#define OK 0
#define ERR (-1)
extern char *cur_term;
int setupterm(char *, int, int *);
int del_curterm(char *);
char *tigetstr(char *);
int tigetnum(char *);
int tigetflag(char *);
char *tparm(char *, int, int, int, int, int, int, int, int, int);
#endif
