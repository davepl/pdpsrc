/* Host-only deterministic concurrent-reconnect cleanup regression. */
#include "../session.c"
#include <sys/wait.h>
#include <sys/time.h>

static int children[3], count, checks;
static char work[] = "/tmp/smbd-session-test.XXXXXX";

static void
fail(line)
int line;
{
 int i;
 fprintf(stderr, "session test line %d failed, errno=%d\n", line, errno);
 for (i = 0; i < count; i++) kill(children[i], SIGKILL);
 while (wait((int *)0) > 0) ;
 sessions_destroy(); rmdir(work); exit(1);
}
#define CHECK(c) do { checks++; if (!(c)) fail(__LINE__); } while (0)

static int
has_row(id, state)
u32 id, state;
{
 u8 p[SESSION_SIZE];
 int i, found;
 found = 0; CHECK(sessions_lock() == 0);
 for (i = 0; i < SESSION_ROWS; i++) {
  CHECK(row(i, p, 0) == 0);
  if (get32(p) && get32(p + 4) == id && get32(p + 12) == state) found = 1;
 }
 sessions_unlock(); return found;
}

static int
start_child(id, previous, control, report)
u32 id, previous;
int control, report;
{
 int fd, pid;
 char c;
 fd = sessions_open(); CHECK(fd >= 0);
 pid = fork(); CHECK(pid >= 0);
 if (!pid) {
  sessions_child(fd, -1);
  if (session_establish(id, 1UL, previous, previous ? 1UL : 0UL)) _exit(2);
  if (control >= 0) {
   if (write(report, "S", 1) != 1) _exit(3);
   while (read(control, &c, 1) < 0) if (errno != EINTR) _exit(4);
   if (!session_revoked) _exit(5);
  }
  session_end();
  if (control < 0 && write(report, "S", 1) != 1) _exit(3);
  _exit(0);
 }
 close(fd); children[count++] = pid; return pid;
}

int
main()
{
 int control[2], old[2], first[2], second[2], pid, status, i, highest;
 char c;
 fd_set readers;
 struct timeval wait_time;
 CHECK(mkdtemp(work) != (char *)0);
 CHECK(sessions_init(work) == 0);
 CHECK(pipe(control) == 0 && pipe(old) == 0 && pipe(first) == 0 && pipe(second) == 0);
 start_child(1000UL, 0UL, control[0], old[1]);
 CHECK(read(old[0], &c, 1) == 1 && c == 'S');
 start_child(2000UL, 1000UL, -1, first[1]);
 for (i = 0; i < 1000 && !has_row(1000UL, 2UL); i++) usleep(1000);
 CHECK(i < 1000);
 start_child(2001UL, 1000UL, -1, second[1]);
 for (i = 0; i < 1000 && !has_row(2001UL, 1UL); i++) usleep(1000);
 CHECK(i < 1000);
 FD_ZERO(&readers); FD_SET(first[0], &readers); FD_SET(second[0], &readers);
 highest = first[0] > second[0] ? first[0] : second[0];
 wait_time.tv_sec = 0; wait_time.tv_usec = 100000;
 CHECK(select(highest + 1, &readers, (fd_set *)0, (fd_set *)0, &wait_time) == 0);
 /* Both replacements must wait even though the second saw a revoking row.
  * Releasing old normal cleanup is the only event that permits either ack. */
 CHECK(write(control[1], "C", 1) == 1);
 CHECK(read(first[0], &c, 1) == 1 && c == 'S');
 CHECK(read(second[0], &c, 1) == 1 && c == 'S');
 CHECK(sessions_lock() == 0);
 while ((pid = wait(&status)) > 0) {
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  CHECK(sessions_reaped(pid) == 0);
 }
 sessions_unlock(); count = 0;
 sessions_destroy(); CHECK(rmdir(work) == 0);
 puts("PASS concurrent authenticated replacement waits for old cleanup, including an already revoking session");
 return 0;
}
