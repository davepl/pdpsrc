/* Bounded persistent SMB creation/change times. No pathname is stored.
 * An inode marker prevents rows for deleted files attaching to reused inodes.
 * The table has two checksummed copies per row; updates flush the alternate
 * copy before changing the marker. One 128-byte buffer serves every operation.
 */
#include "smbd.h"
#include <sys/file.h>
#ifdef __linux__
#include <sys/xattr.h>
#else
#ifdef PDP11
#define TIME_MARKER 0x0040
#else
#ifdef __APPLE__
/* Reserved owner-changeable bit in the supported Darwin stat ABI. */
#define TIME_MARKER 0x1000
#else
#error No verified timestamp marker for this host
#endif
#endif
#endif

#define META_ROWS 2048
#define META_HEADER 64
#define META_COPY 64
#define META_ROW 128
#define META_LENGTH (64L + 128L * META_ROWS)
static u8 meta_buffer[META_ROW];

static unsigned
checksum(p)
const u8 *p;
{
 unsigned a, b, i;
 a = b = 0;
 for (i = 0; i < 62; i++) {
  a += p[i]; if (a >= 255) a -= 255;
  b += a; if (b >= 255) b -= 255;
 }
 return a | (b << 8);
}

static int
transfer(fd, offset, p, len, writing)
int fd, writing;
long offset;
u8 *p;
unsigned len;
{
 unsigned done;
 int n;
 if (lseek(fd, offset, 0) < 0) return -1;
 done = 0;
 while (done < len) {
  n = writing ? write(fd, p + done, len - done) : read(fd, p + done, len - done);
  if (n < 0 && errno == EINTR) continue;
  if (n <= 0) { if (!n) errno = EIO; return -1; }
  done += n;
 }
 return 0;
}

static int
lock_file(fd, operation)
int fd, operation;
{
 while (flock(fd, operation) < 0) if (errno != EINTR) return -1;
 return 0;
}

static int
unlock_result(fd, result)
int fd, result;
{
 int saved;
 saved = errno;
 flock(fd, LOCK_UN);
 errno = saved;
 return result;
}

static void
identity(p, st)
u8 *p;
struct stat *st;
{
 u32 n;
 n = (u32)st->st_dev; put64(p, n, (n >> 16) >> 16);
 n = (u32)st->st_ino; put64(p + 8, n, (n >> 16) >> 16);
}

static int
marker(fd, st)
int fd;
struct stat *st;
{
#ifdef __linux__
 u8 value;
 int n;
 (void)st;
 n = fgetxattr(fd, "user.smbd.has_times", &value, 1);
 if (n < 0 && errno == ENODATA) return 0;
 if (n != 1 || value != 1) { if (n >= 0) errno = EIO; return -1; }
 return 1;
#else
 (void)fd;
 return (st->st_flags & TIME_MARKER) != 0;
#endif
}

static int
set_marker(fd, st)
int fd;
struct stat *st;
{
 int present;
#ifdef __linux__
 u8 value;
#endif
 /* Refresh after any archive/mode mutation: never restore stale flag bits. */
 if (fstat(fd, st) < 0) return -1;
 present = marker(fd, st);
 if (present < 0) return -1;
 if (present) return 0;
#ifdef __linux__
 value = 1;
 if (fsetxattr(fd, "user.smbd.has_times", &value, 1, 0) < 0) return -1;
#else
 if (fchflags(fd, st->st_flags | TIME_MARKER) < 0) return -1;
#endif
 if (fsync(fd) < 0) return -1;
 return fstat(fd, st);
}

/* Returns selected copy offset, or -1 if both copies are corrupt. Empty
 * zero-filled slots are valid generation zero. Modular sequence comparison
 * is unnecessary: a row refuses updates before its 32-bit counter wraps. */
static int
read_row(slot)
unsigned slot;
{
 int good_a, good_b;
 u32 a, b;
 if (transfer(fs_meta_fd, META_HEADER + (long)slot * META_ROW,
              meta_buffer, META_ROW, 0)) return -1;
 good_a = get32(meta_buffer) <= 2UL && checksum(meta_buffer) == get16(meta_buffer + 62);
 good_b = get32(meta_buffer + 64) <= 2UL && checksum(meta_buffer + 64) == get16(meta_buffer + 126);
 if (!good_a && !good_b) { errno = EIO; return -1; }
 if (!good_a) return 64;
 if (!good_b) return 0;
 a = get32(meta_buffer + 4); b = get32(meta_buffer + 68);
 return b > a ? 64 : 0;
}

/* 0 found, 1 absent with insertion slot, -1 failure. Tombstones preserve
 * probe chains; stale rows for externally deleted files consume bounded space. */
static int
find_row(key, slot, copy)
const u8 *key;
unsigned *slot;
int *copy;
{
 unsigned start, i, at, spare;
 int chosen;
 u32 kind;
 start = (unsigned)((get32(key) ^ get32(key + 4) ^
                    get32(key + 8) ^ get32(key + 12)) % META_ROWS);
 spare = META_ROWS;
 for (i = 0; i < META_ROWS; i++) {
  at = (start + i) % META_ROWS;
  chosen = read_row(at); if (chosen < 0) return -1;
  kind = get32(meta_buffer + chosen);
  if (kind == 1 && !memcmp(meta_buffer + chosen + 8, key, 16)) {
   *slot = at; *copy = chosen; return 0;
  }
  if (kind != 1 && spare == META_ROWS) spare = at;
  if (!kind) break;
 }
 if (spare == META_ROWS) { errno = ENOSPC; return -1; }
 *slot = spare; *copy = read_row(spare);
 return *copy < 0 ? -1 : 1;
}

static int
write_row(slot, copy, key, times, kind)
unsigned slot;
int copy;
const u8 *key, *times;
u32 kind;
{
 u32 sequence;
 u8 *p;
 sequence = get32(meta_buffer + copy + 4);
 if (sequence == MASK32) { errno = ENOSPC; return -1; }
 copy = copy ? 0 : 64; p = meta_buffer + copy;
 memset(p, 0, META_COPY);
 put32(p, kind); put32(p + 4, sequence + 1UL); memcpy(p + 8, key, 16);
 if (times) memcpy(p + 24, times, 16);
 put16(p + 62, checksum(p));
 if (transfer(fs_meta_fd, META_HEADER + (long)slot * META_ROW + copy,
              p, META_COPY, 1) < 0 || fsync(fs_meta_fd) < 0) return -1;
 return 0;
}

int
metadata_init(fd, share, writable)
int fd, writable;
struct stat *share;
{
 struct stat st;
 u8 key[16];
 int result;
 if (lock_file(fd, writable ? LOCK_EX : LOCK_SH)) return -1;
 result = -1;
 if (fstat(fd, &st) < 0) return unlock_result(fd, -1);
 identity(key, share);
 if (!st.st_size && writable) {
  memset(meta_buffer, 0, META_HEADER); memcpy(meta_buffer, "SMBTIME1", 8);
  put32(meta_buffer + 8, 1UL); put32(meta_buffer + 12, META_ROWS);
  memcpy(meta_buffer + 16, key, 16); put16(meta_buffer + 62, checksum(meta_buffer));
  if (ftruncate(fd, META_LENGTH) == 0 &&
      transfer(fd, 0L, meta_buffer, META_HEADER, 1) == 0 && fsync(fd) == 0) result = 0;
 } else if (st.st_size == META_LENGTH &&
            transfer(fd, 0L, meta_buffer, META_HEADER, 0) == 0) {
  if (!memcmp(meta_buffer, "SMBTIME1", 8) && get32(meta_buffer + 8) == 1UL &&
      get32(meta_buffer + 12) == META_ROWS && !memcmp(meta_buffer + 16, key, 16) &&
      checksum(meta_buffer) == get16(meta_buffer + 62)) result = 0;
  else errno = EINVAL;
 } else if (st.st_size != META_LENGTH) errno = EINVAL;
 return unlock_result(fd, result);
}

int
metadata_get(fd, st, times)
int fd;
struct stat *st;
u8 *times;
{
 u8 key[16];
 unsigned slot;
 int result, copy;
 result = marker(fd, st);
 if (result < 0) return -1;
 if (!result) return 1;
 if (fs_meta_fd < 0) { errno = EIO; return -1; }
 if (lock_file(fs_meta_fd, LOCK_SH)) return -1;
 identity(key, st); result = find_row(key, &slot, &copy);
 if (!result) memcpy(times, meta_buffer + copy + 24, 16);
 else { errno = EIO; result = -1; }
 return unlock_result(fs_meta_fd, result);
}

int
metadata_set(fd, st, times)
int fd;
struct stat *st;
const u8 *times;
{
 u8 key[16];
 unsigned slot;
 int result, copy, present;
 if (fs_meta_fd < 0) { errno = EINVAL; return -1; }
 if (fstat(fd, st) < 0) return -1;
 present = marker(fd, st); if (present < 0) return -1;
 if (lock_file(fs_meta_fd, LOCK_EX)) return -1;
 identity(key, st); result = find_row(key, &slot, &copy);
 if (result == 1 && present) { errno = EIO; result = -1; }
 if (result >= 0) result = write_row(slot, copy, key, times, 1UL);
 if (!result) result = set_marker(fd, st);
 return unlock_result(fs_meta_fd, result);
}

int
metadata_remove(st)
struct stat *st;
{
 u8 key[16];
 unsigned slot;
 int result, copy;
 if (fs_meta_fd < 0) return 0;
 if (lock_file(fs_meta_fd, LOCK_EX)) return -1;
 identity(key, st); result = find_row(key, &slot, &copy);
 if (!result) result = write_row(slot, copy, key, (u8 *)0, 2UL);
 else if (result == 1) result = 0;
 return unlock_result(fs_meta_fd, result);
}
