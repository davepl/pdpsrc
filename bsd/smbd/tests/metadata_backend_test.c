/* Host-only fault and lifecycle checks, linked with wire.c under sanitizers. */
#include "../metadata.c"
#include <fcntl.h>

int fs_meta_fd = -1;
static int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
 fprintf(stderr, "metadata test line %d: %s (errno=%d)\n", __LINE__, #c, errno); exit(1); \
 } } while (0)

static void
clear_marker(fd, st)
int fd;
struct stat *st;
{
#ifdef __linux__
 CHECK(fremovexattr(fd, "user.smbd.has_times") == 0);
#else
 CHECK(fchflags(fd, st->st_flags & ~TIME_MARKER) == 0);
#endif
 CHECK(fstat(fd, st) == 0);
}

int
main()
{
 char directory[] = "/private/tmp/smbd-meta-backend.XXXXXX";
 struct stat share, st, alternate;
 u8 first[16], second[16], result[16], key[16], saved[META_ROW], corrupt[META_ROW];
 unsigned i, slot;
 int fd, other, copy, saved_fd;
 long row_offset;
#ifdef __linux__
 char linux_directory[] = "/tmp/smbd-meta-backend.XXXXXX";
 char *work = linux_directory;
#else
 char *work = directory;
#endif
 CHECK(mkdtemp(work) != (char *)0);
 CHECK(chdir(work) == 0);
 CHECK(stat(".", &share) == 0);
 fs_meta_fd = open("table", O_RDWR | O_CREAT | O_EXCL, 0600);
 CHECK(fs_meta_fd >= 0);
 CHECK(metadata_init(fs_meta_fd, &share, 1) == 0);
 fd = open("file", O_RDWR | O_CREAT | O_EXCL, 0600);
 CHECK(fd >= 0);
 CHECK(fstat(fd, &st) == 0);
 CHECK(metadata_get(fd, &st, result) == 1);
 for (i = 0; i < 16; i++) { first[i] = (u8)(i + 1); second[i] = (u8)(i + 65); }
#ifndef __linux__
 CHECK(fchflags(fd, st.st_flags | UF_NODUMP) == 0);
 CHECK(fstat(fd, &st) == 0);
#endif
 CHECK(metadata_set(fd, &st, first) == 0);
 CHECK(metadata_get(fd, &st, result) == 0 && !memcmp(first, result, 16));
#ifndef __linux__
 CHECK((st.st_flags & UF_NODUMP) != 0);
#endif
 CHECK(close(fs_meta_fd) == 0);
 fs_meta_fd = open("table", O_RDWR);
 CHECK(fs_meta_fd >= 0 && metadata_init(fs_meta_fd, &share, 1) == 0);
 CHECK(rename("file", "renamed") == 0);
 CHECK(link("renamed", "alias") == 0);
 other = open("alias", O_RDONLY);
 CHECK(other >= 0 && fstat(other, &alternate) == 0);
 CHECK(metadata_get(other, &alternate, result) == 0 && !memcmp(first, result, 16));
 CHECK(close(other) == 0);
 CHECK(unlink("alias") == 0);

 /* A new/reused inode has marker zero even if an old table key remains. */
 clear_marker(fd, &st);
 CHECK(metadata_get(fd, &st, result) == 1);
 CHECK(metadata_set(fd, &st, second) == 0);
 CHECK(metadata_get(fd, &st, result) == 0 && !memcmp(second, result, 16));

 /* Marked files fail closed when their table is unavailable. */
 saved_fd = fs_meta_fd; fs_meta_fd = -1;
 CHECK(metadata_get(fd, &st, result) == -1);
 fs_meta_fd = saved_fd;
 identity(key, &st);
 CHECK(find_row(key, &slot, &copy) == 0);
 row_offset = META_HEADER + (long)slot * META_ROW;
 memcpy(saved, meta_buffer, META_ROW); memcpy(corrupt, saved, META_ROW);
 corrupt[10] ^= 1; corrupt[74] ^= 1;
 CHECK(transfer(fs_meta_fd, row_offset, corrupt, META_ROW, 1) == 0);
 CHECK(metadata_get(fd, &st, result) == -1);
 CHECK(transfer(fs_meta_fd, row_offset, saved, META_ROW, 1) == 0);
 CHECK(metadata_get(fd, &st, result) == 0 && !memcmp(second, result, 16));

 /* Failed writes must not replace acknowledged values. */
 saved_fd = fs_meta_fd; fs_meta_fd = open("table", O_RDONLY);
 CHECK(fs_meta_fd >= 0);
 CHECK(metadata_set(fd, &st, first) == -1);
 CHECK(close(fs_meta_fd) == 0); fs_meta_fd = saved_fd;
 CHECK(metadata_get(fd, &st, result) == 0 && !memcmp(second, result, 16));

 /* A hard-link identity survives removing one name; only final unlink
  * permits the filesystem layer to remove its metadata row. */
 CHECK(unlink("renamed") == 0);
 CHECK(fstat(fd, &st) == 0 && st.st_nlink == 0);
 CHECK(metadata_remove(&st) == 0);
 CHECK(metadata_get(fd, &st, result) == -1);
 CHECK(close(fd) == 0);

 /* Full-width modern inode identity must not alias its low 32 bits. */
#ifndef PDP11
 if (sizeof(unsigned long) > 4) {
  alternate = share;
  alternate.st_ino = ((unsigned long)1 << 33) | 17;
  identity(key, &alternate);
  CHECK(get32(key + 8) == 17UL && get32(key + 12) == 2UL);
 }
#endif
 /* An exhausted table rejects insertion without setting an inode marker.
  * Construct only this disposable table, avoiding thousands of durable
  * filesystem operations just to exercise the bounded search failure. */
 fd = open("capacity-file", O_RDWR | O_CREAT | O_EXCL, 0600);
 CHECK(fd >= 0 && fstat(fd, &st) == 0);
 identity(key, &st); key[0] ^= 128;
 for (i = 0; i < META_ROWS; i++) {
  memset(corrupt, 0, META_ROW);
  put32(corrupt, 1UL); put32(corrupt + 4, 1UL);
  memcpy(corrupt + 8, key, 16); put32(corrupt + 20, (u32)i);
  memcpy(corrupt + 24, first, 16); put16(corrupt + 62, checksum(corrupt));
  CHECK(transfer(fs_meta_fd, META_HEADER + (long)i * META_ROW,
                 corrupt, META_ROW, 1) == 0);
 }
 CHECK(metadata_set(fd, &st, first) == -1 && errno == ENOSPC);
 CHECK(metadata_get(fd, &st, result) == 1);
 CHECK(close(fd) == 0 && unlink("capacity-file") == 0);
 CHECK(close(fs_meta_fd) == 0);
 CHECK(unlink("table") == 0);
 CHECK(chdir("/") == 0);
 CHECK(rmdir(work) == 0);
 printf("metadata backend %d persistence, marker, identity and fault checks PASS\n", checks);
 return 0;
}
