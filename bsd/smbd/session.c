/* Private, bounded session registry for authenticated reconnects. The single
 * configured SMB identity is shared by all workers in this daemon. */
#include "smbd.h"
#include <sys/file.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>

#define SESSION_ROWS 16
#define SESSION_SIZE 16
static int registry_fd = -1, own_slot = -1;
static char registry_path[SMBD_PATH];
static struct stat registry_stat;
extern int session_connection_fd;
extern volatile int session_revoked;

static int
row(slot, p, writing)
int slot, writing;
u8 *p;
{
 unsigned done;
 int n;
 if (lseek(registry_fd, (long)slot * SESSION_SIZE, 0) < 0) return -1;
 done = 0;
 while (done < SESSION_SIZE) {
  n = writing ? write(registry_fd, p + done, SESSION_SIZE - done) :
                read(registry_fd, p + done, SESSION_SIZE - done);
  if (n < 0 && errno == EINTR) continue;
  if (n <= 0) return -1;
  done += n;
 }
 return 0;
}

int
sessions_lock()
{
 while (flock(registry_fd, LOCK_EX) < 0) if (errno != EINTR) return -1;
 return 0;
}

void
sessions_unlock()
{
 flock(registry_fd, LOCK_UN);
}

int
sessions_init(directory)
char *directory;
{
 strcpy(registry_path, directory); strcat(registry_path, "/smbd-sessions.XXXXXX");
 registry_fd = mkstemp(registry_path);
 if (registry_fd < 0) return -1;
 if (fchmod(registry_fd, 0600) || fstat(registry_fd, &registry_stat) ||
     ftruncate(registry_fd, (long)SESSION_ROWS * SESSION_SIZE)) {
  close(registry_fd); registry_fd = -1; unlink(registry_path); return -1;
 }
 return 0;
}

int
sessions_open()
{
 struct stat st;
 int fd;
 fd = open(registry_path, O_RDWR);
 if (fd < 0) return -1;
 if (fstat(fd, &st) || st.st_dev != registry_stat.st_dev ||
     st.st_ino != registry_stat.st_ino || st.st_nlink != 1 || (st.st_mode & 077)) {
  close(fd); return -1;
 }
 return fd;
}

void
sessions_child(fd, socket_fd)
int fd, socket_fd;
{
 close(registry_fd); registry_fd = fd; session_connection_fd = socket_fd;
 own_slot = -1; session_revoked = 0;
 signal(SIGUSR1, session_revoke);
}

/* Called only after NTLM authentication succeeds. Unknown previous IDs,
 * including IDs from an earlier daemon run, require no processing. */
int
session_establish(lo, hi, previous_lo, previous_hi)
u32 lo, hi, previous_lo, previous_hi;
{
 u8 p[SESSION_SIZE];
 int i, available, previous, pid, waiting, signal_old;
 unsigned tries;
 available = previous = -1; pid = signal_old = 0;
 if (sessions_lock()) return -1;
 for (i = 0; i < SESSION_ROWS; i++) {
  if (row(i, p, 0)) goto bad;
  if (!get32(p)) { if (available < 0) available = i; continue; }
  if (get32(p + 4) == lo && get32(p + 8) == hi) goto bad;
  if ((previous_lo || previous_hi) &&
      get32(p + 4) == previous_lo && get32(p + 8) == previous_hi) {
   previous = i; pid = (int)get32(p); signal_old = get32(p + 12) == 1UL;
  }
 }
 if (available < 0 || session_revoked) goto bad;
 memset(p, 0, sizeof(p)); put32(p, (u32)(unsigned)getpid());
 put64(p + 4, lo, hi); put32(p + 12, 1UL);
 if (row(available, p, 1)) goto bad;
 own_slot = available;
 if (previous >= 0 && (lo != previous_lo || hi != previous_hi)) {
  if (row(previous, p, 0)) goto bad;
  put32(p + 12, 2UL);
  if (row(previous, p, 1)) goto bad;
  /* The parent holds this same lock while wait3 reaps and removes rows.
   * An unreaped PID cannot be reused by an unrelated process. */
  if (signal_old && kill(pid, SIGUSR1) < 0 && errno != ESRCH) goto bad;
 } else previous = -1;
 sessions_unlock();
 /* Never wait while holding the registry lock: old normal cleanup needs it.
  * A dead worker is cleared by the parent's reap pass after fs_reap. */
 for (tries = 0; previous >= 0 && tries < 60; tries++) {
  if (session_revoked || sessions_lock()) return -1;
  if (row(previous, p, 0)) { sessions_unlock(); return -1; }
  waiting = get32(p) == (u32)(unsigned)pid &&
            get32(p + 4) == previous_lo && get32(p + 8) == previous_hi;
  sessions_unlock();
  if (!waiting) return 0;
  sleep(1);
 }
 return previous < 0 && !session_revoked ? 0 : -1;
bad:
 sessions_unlock(); return -1;
}

void
session_end()
{
 u8 p[SESSION_SIZE];
 if (own_slot < 0 || sessions_lock()) return;
 if (!row(own_slot, p, 0) && get32(p) == (u32)(unsigned)getpid()) {
  memset(p, 0, sizeof(p)); row(own_slot, p, 1);
 }
 own_slot = -1; sessions_unlock();
}

/* Parent only, with the registry locked across wait3 and fs_reap. */
int
sessions_reaped(pid)
int pid;
{
 u8 p[SESSION_SIZE];
 int i;
 for (i = 0; i < SESSION_ROWS; i++) {
  if (row(i, p, 0)) return -1;
  if (get32(p) == (u32)(unsigned)pid) {
   memset(p, 0, sizeof(p)); if (row(i, p, 1)) return -1;
  }
 }
 return 0;
}

void
sessions_destroy()
{
 struct stat st;
 close(registry_fd); registry_fd = -1;
 if (!lstat(registry_path, &st) && st.st_dev == registry_stat.st_dev &&
     st.st_ino == registry_stat.st_ino) unlink(registry_path);
}
