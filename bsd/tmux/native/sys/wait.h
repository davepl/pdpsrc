/* tmux uses POSIX integer wait statuses; the system macros expect union wait. */
#ifndef TMUX_NATIVE_WAIT_H
#define TMUX_NATIVE_WAIT_H
#include "/usr/include/sys/wait.h"
#undef WIFSTOPPED
#undef WIFSIGNALED
#undef WIFEXITED
#undef WEXITSTATUS
#undef WSTOPSIG
#undef WTERMSIG
#define WIFSTOPPED(s) (((s) & 0377) == 0177)
#define WIFSIGNALED(s) (!WIFSTOPPED(s) && ((s) & 0177) != 0)
#define WIFEXITED(s) (((s) & 0177) == 0)
#define WEXITSTATUS(s) (((unsigned)(s) >> 8) & 0377)
#define WSTOPSIG(s) (((unsigned)(s) >> 8) & 0377)
#define WTERMSIG(s) ((s) & 0177)
#endif
