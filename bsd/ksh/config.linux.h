/*	$NetBSD: config.h,v 1.8 2004/08/19 23:00:22 christos Exp $	*/

/* config.h.  Generated automatically by configure.  */
/* config.h.in.  Generated automatically from configure.in by autoheader.  */
/*
 * This file, acconfig.h, which is a part of pdksh (the public domain ksh),
 * is placed in the public domain.  It comes with no licence, warranty
 * or guarantee of any kind (i.e., at your own risk).
 */

#ifndef CONFIG_H
#define CONFIG_H

/* Define if on AIX 3.
   System headers sometimes define this.
   We just want to avoid a redefinition error message.  */
#ifndef _ALL_SOURCE
/* #undef _ALL_SOURCE */
#endif

/* Define if the closedir function returns void instead of int.  */
/* #undef CLOSEDIR_VOID */

/* Define to empty if the keyword does not work.  */
/* #undef const */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef gid_t */

/* Define to `int' if <sys/types.h> doesn't define. */
#define bool_t int

/* Define if you have a working `mmap' system call.  */
/* #undef HAVE_MMAP */

/* Define if your struct stat has st_rdev.  */
#define HAVE_ST_RDEV 1

/* Define if you have <sys/wait.h> that is POSIX.1 compatible.  */
#define HAVE_SYS_WAIT_H 1

/* Define if you have <unistd.h>.  */
#define HAVE_UNISTD_H 1

/* Define if on MINIX.  */
/* #undef _MINIX */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef mode_t */

/* Define to `long' if <sys/types.h> doesn't define.  */
/* #undef off_t */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef pid_t */

/* Define if the system does not provide POSIX.1 features except
   with this defined.  */
/* #undef _POSIX_1_SOURCE */

/* Define if you need to in order for stat and other things to work.  */
/* #undef _POSIX_SOURCE */

/* Define as the return type of signal handlers (int or void).  */
#define RETSIGTYPE void

/* Define if the `S_IS*' macros in <sys/stat.h> do not work properly.  */
/* #undef STAT_MACROS_BROKEN */

/* Define if `sys_siglist' is declared by <signal.h>.  */
/* #define SYS_SIGLIST_DECLARED 1 */

/* Define if you can safely include both <sys/time.h> and <time.h>.  */
/* #undef TIME_WITH_SYS_TIME */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef uid_t */

/* Define if the closedir function returns void instead of int.  */
/* #undef VOID_CLOSEDIR */

/* Define if your kernel doesn't handle scripts starting with #! */
#define SHARPBANG 1

/* Define if dup2() preserves the close-on-exec flag (ultrix does this) */
/* #undef DUP2_BROKEN */

/* Define as the return value of signal handlers (0 or ).  */
#define RETSIGVAL 

/* Define if you have posix signal routines (sigaction(), et. al.) */
#undef POSIX_SIGNALS

/* Define if you have BSD4.2 signal routines (sigsetmask(), et. al.) */
/* #define BSD44_SIGNALS 1 */

/* Define if you have BSD4.1 signal routines (sigset(), et. al.) */
/* #undef BSD41_SIGNALS */

/* Define if you have v7 signal routines (signal(), signal reset on delivery) */
/* #undef V7_SIGNALS */

/* Define to use the fake posix signal routines (sigact.[ch]) */
/* #undef USE_FAKE_SIGACT */

/* Define if signals don't interrupt read() */
/* #undef SIGNALS_DONT_INTERRUPT */

/* Define if you have bsd versions of the setpgrp() and getpgrp() routines */
/* #define BSD_PGRP 2 */

/* Define if you have POSIX versions of the setpgid() and getpgrp() routines */
#define POSIX_PGRP

/* Define if you have sysV versions of the setpgrp() and getpgrp() routines */
/* #undef SYSV_PGRP */

/* Define if you don't have setpgrp(), setpgid() or getpgrp() routines */
/* #undef NO_PGRP */

/* Define to char if your compiler doesn't like the void keyword */
/* #undef void */

/* Define to nothing if compiler doesn't like the volatile keyword */
/* #undef volatile */

/* Define if C compiler groks function prototypes */
#define HAVE_PROTOTYPES 1

/* Define if C compiler groks __attribute__((...)) (const, noreturn, format) */
#define HAVE_GCC_FUNC_ATTR

/* Define to 32-bit signed integer type if <sys/types.h> doesn't define */
/* #define clock_t long */

/* Define to the type of struct rlimit fields if the rlim_t type is missing */
/* #define rlim_t long */

/* Define if time() is declared in <time.h> */
#define TIME_DECLARED 1

/* Define to `unsigned' if <signal.h> doesn't define */
/* #undef sigset_t */

/* Define if sys_errlist[] and sys_nerr are in the C library */
/* #undef HAVE_SYS_ERRLIST */

/* Define if sys_errlist[] and sys_nerr are defined in <errno.h> */
/* #undef SYS_ERRLIST_DECLARED */

/* Define if sys_siglist[] is in the C library */
/* #define HAVE_SYS_SIGLIST 1 */

/* Define if you have a sane <termios.h> header file */
#define HAVE_TERMIOS_H

/* Define if you have a memset() function in your C library */
#define HAVE_MEMSET 1

/* Define if you have a memmove() function in your C library */
#define HAVE_MEMMOVE

/* Define if you have a bcopy() function in your C library */
#define HAVE_BCOPY

/* Define if you have a lstat() function in your C library */
#define HAVE_LSTAT 1

/* Define if you have a sane <termio.h> header file */
/* #undef HAVE_TERMIO_H */

/* Define if you don't have times() or if it always returns 0 */
/* #undef TIMES_BROKEN */

/* Define if opendir() will open non-directory files */
/* #undef OPENDIR_DOES_NONDIR */

/* Define if you have a dup2() function in your C library */
#define	HAVE_DUP2 1

/* Define if the pgrp of setpgrp() can't be the pid of a zombie process */
/* #undef NEED_PGRP_SYNC */

/* Define if you arg running SCO unix */
/* #undef OS_SCO */

/* Define if you arg running ISC unix */
/* #undef OS_ISC */

/* Define if you arg running OS2 with the EMX library */
/* #undef OS2 */

/* Define if you have a POSIX.1 compatible <sys/wait.h> */
#define POSIX_SYS_WAIT

/* Define if your OS maps references to /dev/fd/n to file descriptor n */
#define HAVE_DEV_FD 1

/* Define if you have BSD strlcpy and strlcat functions */
#define HAVE_BSD_STRL

/* Define if you have -lbsd #includes */
#define HAVE_BSD_H 1

/* Default PATH */
#ifdef RESCUEDIR
#define DEFAULT_PATH RESCUEDIR ":/bin:/usr/bin:/sbin:/usr/sbin"
#else
#define DEFAULT_PATH "/bin:/usr/bin:/sbin:/usr/sbin"
#endif

/* Define if your C library's getwd/getcwd function dumps core in unreadable
 * directories.  */
/* #undef HPUX_GETWD_BUG */

/* Include ksh features? */
#define KSH 1

/* Include emacs editing? */
#define EMACS 1

/* Include vi editing? */
#define VI 1

/* Include job control? */
#define JOBS 1 

/* Include brace-expansion? */
#define BRACE_EXPAND 1

/* Include any history? */
#define HISTORY 1

/* Include complex history? */
#define COMPLEX_HISTORY

/* Strict POSIX behaviour? */
/* #define POSIXLY_CORRECT 1 */

/* Specify default $ENV? */
#define DEFAULT_ENV	"$HOME/.kshrc"

/* Include shl(1) support? */
/* #undef SWTCH */

/* Include game-of-life? */
/* #undef SILLY */

/* The number of bytes in a int.  */
#define SIZEOF_INT 4

/* The number of bytes in a long.  */
#define SIZEOF_LONG 8

/* Define if you have the _setjmp function.  */
#define HAVE__SETJMP

/* Define if you have the confstr function.  */
#define HAVE_CONFSTR

/* Define if you have the flock function.  */
#define HAVE_FLOCK 1

/* Define if you have the getcwd function.  */
#define HAVE_GETCWD

/* Define if you have the getgroups function.  */
#define HAVE_GETGROUPS

/* Define if you have the getpagesize function.  */
#define HAVE_GETPAGESIZE 1

/* Define if you have the getrusage function.  */
#define HAVE_GETRUSAGE

/* Define if you have the getwd function.  */
#define HAVE_GETWD 1

/* Define if you have the killpg function.  */
#define HAVE_KILLPG 1

/* Define if you have the nice function.  */
#define HAVE_NICE 1

/* Define if you have the setrlimit function.  */
#define HAVE_SETRLIMIT 1

/* Define if you have the sigsetjmp function.  */
#define HAVE_SIGSETJMP

/* Define if you have the strcasecmp function.  */
#define HAVE_STRCASECMP 1

/* Define if you have the strerror function.  */
#define HAVE_STRERROR 1

/* Define if you have the strstr function.  */
#define HAVE_STRSTR 1

/* Define if you have the sysconf function.  */
/* #undef HAVE_SYSCONF */

/* Define if you have the tcsetpgrp function.  */
#define HAVE_TCSETPGRP

/* Define if you have the ulimit function.  */
#define HAVE_ULIMIT

/* Define if you have the valloc function.  */
#define HAVE_VALLOC 1

/* Define if you have the wait3 function.  */
#define HAVE_WAIT3 1

/* Define if you have the waitpid function.  */
#define HAVE_WAITPID 1

/* Define if you have the <dirent.h> header file.  */
#define HAVE_DIRENT_H

/* Define if you have the <fcntl.h> header file.  */
#define HAVE_FCNTL_H 1

/* Define if you have the <limits.h> header file.  */
#define HAVE_LIMITS_H 1

/* Define if you have the <memory.h> header file.  */
#define HAVE_MEMORY_H 1

/* Define if you have the <ndir.h> header file.  */
/* #undef HAVE_NDIR_H */

/* Define if you have the <paths.h> header file.  */
#define HAVE_PATHS_H 1

/* Define if you have the <stddef.h> header file.  */
#define HAVE_STDDEF_H

/* Define if you have the <stdlib.h> header file.  */
#define HAVE_STDLIB_H 1

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <sys/dir.h> header file.  */
#define HAVE_SYS_DIR_H

/* Define if you have the <sys/ndir.h> header file.  */
/* #undef HAVE_SYS_NDIR_H */

/* Define if you have the <sys/param.h> header file.  */
/* #define HAVE_SYS_PARAM_H 1 */

/* Define if you have the <sys/resource.h> header file.  */
#define HAVE_SYS_RESOURCE_H 1

/* Define if you have the <sys/time.h> header file.  */
/* #define HAVE_SYS_TIME_H 1 */

/* Define if you have the <sys/wait.h> header file.  */
/* #define HAVE_SYS_WAIT_H 1 */

/* Define if you have the <ulimit.h> header file.  */
/* #undef HAVE_ULIMIT_H */

/* Define if you have the <values.h> header file.  */
#define HAVE_VALUES_H

/* Define if you have the <locale.h> header file. */
#define HAVE_LOCALE_H

/* Define if you have the <sys/cdefs.h> header file. */
/* #undef HAVE_SYS_CDEFS_H */

/* Need to use a separate file to keep the configure script from commenting
 * out the undefs....
 */
#include "conf-end.h"

#endif /* CONFIG_H */
