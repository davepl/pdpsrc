/* Drain TCP while signing on a slow CPU. The ring lives on disk, not in the
 * 64 KiB data space. Bytes are still framed, authenticated and dispatched by
 * serve in their original order. No prefetched request is executed here. */
#include "smbd.h"
#include <sys/time.h>

#define AHEAD_LIMIT (4L * SMBD_MAXFRAME)
static FILE *ahead;
static int connection, ended;
static long read_pos, write_pos, available;
extern volatile int session_revoked;

void
transport_init(fd, spool)
int fd;
FILE *spool;
{
 connection = fd; ahead = spool; ended = 0;
 read_pos = write_pos = available = 0;
}

int
transport_read(p, len)
u8 *p;
unsigned len;
{
 unsigned n;
 int got;
 while (len) {
  if (session_revoked) return -1;
  n = len;
  if (available) {
   if ((long)n > available) n = (unsigned)available;
   if ((long)n > AHEAD_LIMIT - read_pos) n = (unsigned)(AHEAD_LIMIT - read_pos);
   if (fseek(ahead, read_pos, 0) || fread(p, 1, n, ahead) != n) return -1;
   read_pos += n; available -= n;
   if (read_pos == AHEAD_LIMIT) read_pos = 0;
  } else {
   if (ended) return -1;
   got = read(connection, p, n);
   if (got < 0 && errno == EINTR) continue;
   if (got <= 0) return -1;
   n = got;
  }
  p += n; len -= n;
 }
 return 0;
}

int
transport_prefetch(buffer, capacity)
u8 *buffer;
unsigned capacity;
{
 fd_set ready;
 struct timeval poll;
 unsigned n, tries;
 int got;
 for (tries = 0; tries < 16; tries++) {
  if (session_revoked) return -1;
  if (ended || available == AHEAD_LIMIT) break;
  FD_ZERO(&ready); FD_SET(connection, &ready);
  poll.tv_sec = poll.tv_usec = 0;
  got = select(connection + 1, &ready, (fd_set *)0, (fd_set *)0, &poll);
  if (got < 0 && errno == EINTR) continue;
  if (got < 0) return -1;
  if (!got) break;
  n = capacity;
  if ((long)n > AHEAD_LIMIT - available) n = (unsigned)(AHEAD_LIMIT - available);
  if ((long)n > AHEAD_LIMIT - write_pos) n = (unsigned)(AHEAD_LIMIT - write_pos);
  got = read(connection, buffer, n);
  if (got < 0 && errno == EINTR) continue;
  if (got < 0) return -1;
  if (!got) { ended = 1; break; }
  if (fseek(ahead, write_pos, 0) || fwrite(buffer, 1, got, ahead) != (unsigned)got)
   return -1;
  write_pos += got; available += got;
  if (write_pos == AHEAD_LIMIT) write_pos = 0;
 }
 return 0;
}

/* Validate all headers before dispatch. A small unsigned metadata chain may
 * share one registry lock acquisition. Never hold that lock across bulk I/O,
 * signing, a network read, or more than SMBD_COMPOUNDS commands. */
int
frame_validate(f, total)
FILE *f;
long total;
{
 long pos, next, len;
 unsigned count, cmd;
 int batch;
 u8 h[120];
 pos = 0; count = 0; batch = total <= SMBD_BUFSIZE;
 while (pos < total) {
  if (++count > SMBD_COMPOUNDS || total - pos < 64 ||
      fseek(f, pos, 0) || fread(h, 1, 64, f) != 64) return -1;
  if (memcmp(h, "\376SMB", 4) || get16(h + 4) != 64 || (get32(h + 16) & 3))
   return -1;
  next = (long)get32(h + 20); len = next ? next : total - pos;
  if (next && (next < 64 || (next & 7) || next > total - pos - 64)) return -1;
  cmd = get16(h + 12);
  if ((get32(h + 16) & 8) || (cmd != 5 && cmd != 6 && cmd != 14 && cmd != 16))
   batch = 0;
  if (batch && cmd == 5) {
   if (len < 120) batch = 0;
   else {
    if (fread(h + 64, 1, 56, f) != 56) return -1;
    if (get32(h + 100) != 1UL || (get32(h + 104) & 0x1000UL)) batch = 0;
   }
  }
  if (!next) return batch && count > 1;
  pos += next;
 }
 return -1;
}
