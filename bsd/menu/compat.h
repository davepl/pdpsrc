#ifndef COMPAT_H
#define COMPAT_H

#ifdef __pdp11__
#include <sys/types.h>

int compat_snprintf(char *buf, size_t len, const char *fmt, ...);

#ifndef snprintf
#define snprintf compat_snprintf
#endif

#ifndef remove
#define remove unlink
#endif

/* Some toolchains lack a prototype for unlink; declare a fallback. */
extern int unlink();
#endif /* __pdp11__ */

#endif /* COMPAT_H */
