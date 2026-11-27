#ifndef TMUX_FNMATCH_H
#define TMUX_FNMATCH_H
#define FNM_NOMATCH 1
/* tmux uses flags == 0 only. */
int fnmatch(const char *, const char *, int);
#endif
