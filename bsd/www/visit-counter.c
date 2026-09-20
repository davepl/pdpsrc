/* One-shot, unprivileged 2.11BSD visitor counter. No request input.
 * The homepage selects this endpoint once per browser session; reloads
 * read the static visits.txt symlink instead. State survives reboot.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#ifndef DATAFILE
#define DATAFILE "/home/www-visits/total"
#endif
#ifndef LOCKFILE
#define LOCKFILE "/home/www-visits/lock"
#endif
#ifndef NEWFILE
#define NEWFILE "/home/www-visits/total.new"
#endif

int fail()
{
    printf("HTTP/1.0 503 Service Unavailable\r\n");
    printf("Content-Type: text/plain\r\nCache-Control: no-store\r\n");
    printf("Connection: close\r\n\r\nCount unavailable.\n");
    return 1;
}

int main()
{
    int lockfd, fd, len, i, digit, off, wrote, ok;
    long count;
    char buf[32];

    /* Bound lock waits and response writes as well as disk work. */
    signal(SIGALRM, SIG_DFL);
    alarm(8);
    umask(077);
    lockfd = open(LOCKFILE, O_RDWR);
    if (lockfd < 0)
        return fail();
    if (flock(lockfd, LOCK_EX) < 0) {
        close(lockfd);
        return fail();
    }

    /* Never silently reset a missing or damaged total to zero. */
    fd = open(DATAFILE, O_RDONLY);
    if (fd < 0) {
        close(lockfd);
        return fail();
    }
    len = read(fd, buf, sizeof(buf));
    close(fd);
    if (len < 1 || len >= sizeof(buf)) {
        close(lockfd);
        return fail();
    }
    count = 0;
    for (i = 0; i < len && buf[i] >= '0' && buf[i] <= '9'; i++) {
        digit = buf[i] - '0';
        if (count > (2147483647L - digit) / 10) {
            close(lockfd);
            return fail();
        }
        count = count * 10 + digit;
    }
    if (i == 0 || count == 2147483647L ||
        !(i == len || (i == len - 1 && buf[i] == '\n'))) {
        close(lockfd);
        return fail();
    }
    count++;
    /* Fixed width also keeps static httpd's Content-Length consistent
     * if an increment occurs between its stat() and fopen(). */
    sprintf(buf, "%010ld\n", count);
    for (len = 0; buf[len]; len++)
        ;

    /* The private directory and stable flock serialize atomic updates.
     * A leftover temporary file after a crash is safe to overwrite.
     */
    fd = open(NEWFILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        close(lockfd);
        return fail();
    }
    off = 0;
    while (off < len) {
        wrote = write(fd, buf + off, len - off);
        if (wrote < 0 && errno == EINTR)
            continue;
        if (wrote <= 0)
            break;
        off += wrote;
    }
    ok = off == len && fsync(fd) == 0;
    if (close(fd) < 0)
        ok = 0;
    if (!ok || rename(NEWFILE, DATAFILE) < 0) {
        unlink(NEWFILE);
        close(lockfd);
        return fail();
    }
    close(lockfd);

    printf("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n");
    printf("Cache-Control: no-store\r\nConnection: close\r\n");
    printf("Content-Length: %d\r\n\r\n%s", len, buf);
    fflush(stdout);
    return 0;
}
