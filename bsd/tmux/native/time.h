/* The native time.h has no inclusion guard; sys/time.h also includes it. */
#ifndef TMUX_NATIVE_TIME_H
#define TMUX_NATIVE_TIME_H
#include "/usr/include/time.h"
struct tm *gmtime_r(const time_t *, struct tm *);
#endif
