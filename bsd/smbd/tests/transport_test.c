/* Host regression: fill the bounded disk ring, wrap it, preserve byte order. */
#include "../transport.c"
#include <sys/socket.h>
#include <fcntl.h>

volatile int session_revoked;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"transport line %d errno %d\n",__LINE__,errno); exit(1); } } while (0)

static void
frames()
{
 FILE *f;
 u8 b[208];
 unsigned i;
 f = tmpfile(); CHECK(f != 0);
 memset(b, 0, sizeof(b));
 for (i = 0; i < 2; i++) {
  memcpy(b + i * 120, "\376SMB", 4); put16(b + i * 120 + 4, 64);
 }
 put16(b + 12, 5); put32(b + 20, 120UL); put32(b + 100, 1UL);
 put16(b + 132, 6);
 CHECK(fwrite(b, 1, sizeof(b), f) == sizeof(b) && fflush(f) == 0);
 CHECK(frame_validate(f, (long)sizeof(b)) == 1);
 /* Signing, data transfers, and mutating CREATE must not batch locks. */
 put32(b + 16, 8UL);
 rewind(f); CHECK(fwrite(b, 1, sizeof(b), f) == sizeof(b) && fflush(f) == 0);
 CHECK(frame_validate(f, (long)sizeof(b)) == 0);
 put32(b + 16, 0UL); put32(b + 104, 0x1000UL);
 rewind(f); CHECK(fwrite(b, 1, sizeof(b), f) == sizeof(b) && fflush(f) == 0);
 CHECK(frame_validate(f, (long)sizeof(b)) == 0);
 put32(b + 104, 0UL); put16(b + 132, 8);
 rewind(f); CHECK(fwrite(b, 1, sizeof(b), f) == sizeof(b) && fflush(f) == 0);
 CHECK(frame_validate(f, (long)sizeof(b)) == 0);
 put32(b + 20, 121UL);
 rewind(f); CHECK(fwrite(b, 1, sizeof(b), f) == sizeof(b) && fflush(f) == 0);
 CHECK(frame_validate(f, (long)sizeof(b)) == -1);
 fclose(f);
}

int
main()
{
 int pair[2], flags, got, closed;
 unsigned n, i;
 long sent, consumed, total;
 u8 outgoing[2003], incoming[2039], scratch[2048];
 FILE *spool;
 frames();
 CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
 flags = fcntl(pair[1], F_GETFL, 0);
 CHECK(flags >= 0 && fcntl(pair[1], F_SETFL, flags | O_NONBLOCK) == 0);
 spool = tmpfile(); CHECK(spool != 0);
 transport_init(pair[0], spool);
 sent = consumed = 0; closed = 0; total = 3L * 1024 * 1024 + 17;
 while (consumed < total) {
  if (sent < total) {
   n = sizeof(outgoing);
   if (total - sent < (long)n) n = (unsigned)(total - sent);
   for (i = 0; i < n; i++) outgoing[i] = (u8)((sent + i) % 251);
   got = write(pair[1], outgoing, n);
   CHECK(got > 0 || errno == EAGAIN || errno == EWOULDBLOCK);
   if (got > 0) sent += got;
  } else if (!closed) {
   close(pair[1]); closed = 1;
  }
  CHECK(transport_prefetch(scratch, sizeof(scratch)) == 0);
  CHECK(available >= 0 && available <= AHEAD_LIMIT);
  /* First fill to the exact bound, then exercise wraparound while sending. */
  if (!consumed && available < AHEAD_LIMIT) continue;
  n = sizeof(incoming);
  if (sent - consumed < (long)n) n = (unsigned)(sent - consumed);
  if (n) {
   CHECK(transport_read(incoming, n) == 0);
   for (i = 0; i < n; i++) CHECK(incoming[i] == (u8)((consumed + i) % 251));
   consumed += n;
  }
 }
 if (!closed) close(pair[1]);
 CHECK(transport_prefetch(scratch, sizeof(scratch)) == 0);
 CHECK(transport_read(incoming, 1) == -1);
 CHECK(write_pos == total % AHEAD_LIMIT);
 session_revoked = 1;
 CHECK(transport_prefetch(scratch, sizeof(scratch)) == -1);
 CHECK(transport_read(incoming, 1) == -1);
 fclose(spool); close(pair[0]);
 puts("PASS transport: 2 MiB bound, ring wrap, 3 MiB byte order, EOF, revocation");
 return 0;
}
