/* Small libc interfaces absent from stock 2.11BSD. */
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include "libgen.h"
#include "fnmatch.h"

char *
basename(char *path)
{
    char *end, *p;
    if (path == 0 || *path == 0)
        return ".";
    end = path + strlen(path) - 1;
    while (end > path && *end == '/')
        *end-- = 0;
    if (end == path && *end == '/')
        return path;
    p = strrchr(path, '/');
    return p == 0 ? path : p + 1;
}

/* Wildcards, escaped literals, ranges and negated bracket classes. */
int
fnmatch(const char *pat, const char *str, int flags)
{
    int c, first, last, match, negate;
    const char *start;
    if (flags != 0) {
        errno = EINVAL;
        return FNM_NOMATCH;
    }
    while ((c = (unsigned char)*pat++) != 0) {
        switch (c) {
        case '?':
            if (*str++ == 0) return FNM_NOMATCH;
            break;
        case '*':
            while (*pat == '*') pat++;
            if (*pat == 0) return 0;
            do {
                if (fnmatch(pat, str, 0) == 0) return 0;
            } while (*str++ != 0);
            return FNM_NOMATCH;
        case '[':
            if (*str == 0) return FNM_NOMATCH;
            start = pat;
            negate = (*pat == '!' || *pat == '^');
            if (negate) pat++;
            match = 0;
            first = 1;
            while (*pat != 0 && (*pat != ']' || first)) {
                first = 0;
                c = (unsigned char)*pat++;
                if (c == '\\' && *pat) c = (unsigned char)*pat++;
                last = c;
                if (*pat == '-' && pat[1] != 0 && pat[1] != ']') {
                    pat++;
                    last = (unsigned char)*pat++;
                    if (last == '\\' && *pat) last = (unsigned char)*pat++;
                }
                if ((unsigned char)*str >= c && (unsigned char)*str <= last)
                    match = 1;
            }
            if (*pat == 0) {
                pat = start;
                if (*str++ != '[') return FNM_NOMATCH;
            } else {
                pat++;
                if (match == negate) return FNM_NOMATCH;
                str++;
            }
            break;
        case '\\':
            if (*pat != 0) c = (unsigned char)*pat++;
            /* FALLTHROUGH */
        default:
            if ((unsigned char)*str++ != c) return FNM_NOMATCH;
        }
    }
    return *str == 0 ? 0 : FNM_NOMATCH;
}
