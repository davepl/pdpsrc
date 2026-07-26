#ifndef PDP11_COMPAT_H
#define PDP11_COMPAT_H

#include <sys/types.h>

#if defined(pdp11) || defined(__pdp11__)
/*
 * Some 2.11BSD installations have a newer unistd.h but no stdint.h.
 * Keep the declarations whose return types matter on a 16-bit target here.
 */
int close(int);
int ftruncate(int, off_t);
off_t lseek(int, off_t, int);
ssize_t read(int, void *, size_t);
int unlink(const char *);
ssize_t write(int, const void *, size_t);
#else
#include <unistd.h>
#endif

#endif
