#ifndef PDPSRC_UNISTD_H
#define PDPSRC_UNISTD_H

#if defined(pdp11) || defined(__pdp11__)
/* Some 2.11BSD installations have a newer unistd.h but no stdint.h.
 * Declare the system calls used here without that header dependency.
 * Preserve pointer and long return types on the 16-bit compiler.
 */
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

unsigned alarm(unsigned);
int close(int);
char *crypt(const char *, const char *);
void _exit(int);
int execv(const char *, char *const []);
int execve(const char *, char *const [], char *const []);
int execvp(const char *, char *const []);
int fsync(int);
uid_t geteuid(void);
int gethostname(char *, size_t);
int getopt(int, char *const [], const char *);
char *getpass(const char *);
uid_t getuid(void);
int isatty(int);
off_t lseek(int, off_t, int);
ssize_t read(int, void *, size_t);
int setgid(gid_t);
int setuid(uid_t);
unsigned sleep(unsigned);
char *ttyname(int);
int unlink(const char *);
pid_t vfork(void);
ssize_t write(int, const void *, size_t);

extern char *optarg;
extern int opterr, optind, optopt;
#else
#include <unistd.h>
#endif

#endif
