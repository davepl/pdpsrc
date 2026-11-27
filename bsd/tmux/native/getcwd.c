#include <sys/param.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static char *
getcwd_small(char *buf, size_t size)
{
    char path[MAXPATHLEN];
    size_t len;
    if (getwd(path) == 0)
        return 0;
    len = strlen(path) + 1;
    if (size < len) {
        errno = ERANGE;
        return 0;
    }
    memcpy(buf, path, len);
    return buf;
}

char *
getcwd(char *buf, size_t size)
{
    /* Both tmux callers already provide MAXPATHLEN bytes. Avoid nesting
     * another 1K temporary buffer on top of the native getwd stack. */
    if (size >= MAXPATHLEN)
        return getwd(buf);
    return getcwd_small(buf, size);
}
