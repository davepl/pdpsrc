/* Host filesystem tests; compile with ../fs.c and ../wire.c. */
#include "../smbd.h"
#include <fcntl.h>
#include <sys/time.h>
#ifdef __linux__
#include <sys/xattr.h>
#endif

struct config cfg;
struct authstate auth;
u8 request[SMBD_BUFSIZE], response[SMBD_BUFSIZE], related_file[16];
unsigned request_len, response_len;
int read_fd;
long read_offset, read_length;
int fs_state_fd = -1;
int fs_meta_fd = -1;
FILE *request_file;
long request_start, request_size;
static int checks;

#define CHECK(c) do { checks++; if (!(c)) { \
 fprintf(stderr, "fs test line %d: %s\n", __LINE__, #c); exit(1); \
 } } while (0)

static void
reset()
{
 memset(request, 0, sizeof(request)); memset(response, 0, sizeof(response));
 request_len = response_len = 64; read_fd = -1; read_length = 0;
}

static u32
create_name(name, access, options, disposition, share, id)
char *name;
u32 access, options, disposition, share;
u8 *id;
{
 unsigned n;
 u32 s;
 reset(); put16(request + 64, 57);
 put32(request + 88, access); put32(request + 96, share);
 put32(request + 100, disposition); put32(request + 104, options);
 n = utf16_encode(request + 120, name, SMBD_PATH * 2);
 put16(request + 108, 120); put16(request + 110, n);
 request_len = 120 + n;
 s = fs_dispatch(5);
 if (!s && id) memcpy(id, response + 128, 16);
 return s;
}

static u32
open_name(name, access, options, id)
char *name;
u32 access, options;
u8 *id;
{
 return create_name(name, access, options, 1UL, 7UL, id);
}

static u32
close_id(id)
u8 *id;
{
 reset(); put16(request + 64, 24); put16(request + 66, 1);
 memcpy(request + 72, id, 16); request_len = 88;
 return fs_dispatch(6);
}

static u32
read_id(id, offset, length)
u8 *id;
u32 offset, length;
{
 reset(); put16(request + 64, 49); put32(request + 68, length);
 put64(request + 72, offset, 0UL); memcpy(request + 80, id, 16);
 request_len = 112;
 return fs_dispatch(8);
}

static u32
directory(id, cls, flags, pattern, capacity)
u8 *id;
unsigned cls, flags, capacity;
char *pattern;
{
 unsigned n;
 reset(); put16(request + 64, 33); request[66] = cls; request[67] = flags;
 memcpy(request + 72, id, 16); put32(request + 92, (u32)capacity);
 n = pattern ? utf16_encode(request + 96, pattern, 512) : 0;
 put16(request + 88, 96); put16(request + 90, n); request_len = 96 + n;
 return fs_dispatch(14);
}

static u32
info(id, type, cls, capacity)
u8 *id;
unsigned type, cls, capacity;
{
 reset(); put16(request + 64, 41); request[66] = type; request[67] = cls;
 put32(request + 68, (u32)capacity); memcpy(request + 88, id, 16);
 request_len = 104;
 return fs_dispatch(16);
}

static u32
write_id(id, offset, count)
u8 *id;
u32 offset, count;
{
 u8 data[256];
 unsigned i, n;
 u32 left, status;
 reset(); put16(request + 64, 49); put16(request + 66, 112);
 put32(request + 68, count); put64(request + 72, offset, 0UL);
 memcpy(request + 80, id, 16); request_len = 112;
 request_start = 0; request_size = 112L + count;
 request_file = tmpfile(); CHECK(request_file != 0);
 CHECK(fwrite(request, 1, 112, request_file) == 112);
 for (i = 0; i < 256; i++) data[i] = i;
 for (left = count; left; left -= n) {
  n = left > 256UL ? 256 : (unsigned)left;
  CHECK(fwrite(data, 1, n, request_file) == n);
 }
 CHECK(fflush(request_file) == 0);
 status = fs_dispatch(9);
 fclose(request_file); request_file = 0;
 return status;
}

static u32
set_id(id, cls, data, length)
u8 *id, *data;
unsigned cls, length;
{
 reset(); put16(request + 64, 33); request[66] = 1; request[67] = cls;
 put32(request + 68, (u32)length); put16(request + 72, 96);
 memcpy(request + 80, id, 16); memcpy(request + 96, data, length);
 request_len = 96 + length;
 return fs_dispatch(17);
}

static u32
rename_id(id, name, replace)
u8 *id;
char *name;
int replace;
{
 u8 data[SMBD_PATH * 2 + 20];
 unsigned n;
 memset(data, 0, 20); data[0] = replace;
 n = utf16_encode(data + 20, name, SMBD_PATH * 2);
 put32(data + 16, (u32)n);
 return set_id(id, 10, data, n + 20);
}

static void
archive_storage(name, expected, initialize)
char *name;
int expected, initialize;
{
 int fd;
#ifdef __linux__
 unsigned char value;
#else
 struct stat st;
#ifdef UF_ARCHIVE
 unsigned long bit = UF_ARCHIVE;
#else
 unsigned long bit = 0x0800UL;
#endif
#endif
 fd = open(name, O_RDONLY); CHECK(fd >= 0);
#ifdef __linux__
 if (initialize) CHECK(fsetxattr(fd, "user.smbd.test", "x", 1, 0) == 0);
 CHECK(fgetxattr(fd, "user.smbd.archive", &value, 1) == 1);
 CHECK(value == expected);
 CHECK(fgetxattr(fd, "user.smbd.test", &value, 1) == 1 && value == 'x');
#else
 CHECK(fstat(fd, &st) == 0);
 if (initialize) {
  CHECK(fchflags(fd, st.st_flags | UF_NODUMP) == 0);
  CHECK(fstat(fd, &st) == 0);
 }
 CHECK(((st.st_flags & bit) != 0) == expected);
 CHECK((st.st_flags & UF_NODUMP) != 0);
#endif
 CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
}

static void
metadata_fs_tests()
{
 u8 id[16], other[16], dir[16], data[40], birth[8], change[8], byte;
 struct stat root, st;
 struct timeval local[2];
 cfg.writable = 1;
 fs_state_fd = open(".test-state", O_CREAT | O_TRUNC | O_RDWR, 0600);
 fs_meta_fd = open(".test-meta", O_CREAT | O_TRUNC | O_RDWR, 0600);
 CHECK(fs_state_fd >= 0 && fs_meta_fd >= 0 && stat(".", &root) == 0);
 CHECK(metadata_init(fs_meta_fd, &root, 1) == 0); fs_init();
 CHECK(create_name("time.bin", 0xc0010000UL, 0, 2UL, 7UL, id) == ST_OK);
 archive_storage("time.bin", 1, 1);
 memset(data, 0, 40); filetime(data, 1500000000L);
 put32(data, get32(data) + 17UL); memcpy(birth, data, 8);
 filetime(data + 24, 1600000000L);
 put32(data + 24, get32(data + 24) + 23UL); memcpy(change, data + 24, 8);
 put32(data + 32, 0x80UL);
 CHECK(set_id(id, 4, data, 40) == ST_OK);
 archive_storage("time.bin", 0, 0);
 CHECK(info(id, 1, 4, 40) == ST_OK);
 CHECK(!memcmp(response + 72, birth, 8) && !memcmp(response + 96, change, 8));
 /* Positive ChangeTime suppresses automatic changes from this open, per
  * MS-FSA 2.1.5.15.2. A later open has independent update behavior. */
 CHECK(write_id(id, 0UL, 32UL) == ST_OK);
 CHECK(info(id, 1, 4, 40) == ST_OK);
 CHECK(!memcmp(response + 72, birth, 8) && !memcmp(response + 96, change, 8));
 archive_storage("time.bin", 1, 0);
 /* Positive native timestamps suppress later I/O updates from the same
  * open. Verify actual inode times, not only synthetic QUERY_INFO fields. */
 memset(data, 0, 40); filetime(data + 8, 1400000000L);
 filetime(data + 16, 1450000000L);
 CHECK(set_id(id, 4, data, 40) == ST_OK);
 CHECK(write_id(id, 0UL, 1UL) == ST_OK);
 CHECK(stat("time.bin", &st) == 0 && st.st_atime == 1400000000L && st.st_mtime == 1450000000L);
 memset(data, 0, 8); put32(data, 17UL);
 CHECK(set_id(id, 20, data, 8) == ST_OK);
 CHECK(stat("time.bin", &st) == 0 && st.st_atime == 1400000000L && st.st_mtime == 1450000000L);
 CHECK(read_id(id, 0UL, 1UL) == ST_OK);
 CHECK(lseek(read_fd, read_offset, 0) == read_offset && read(read_fd, &byte, 1) == 1);
 CHECK(fs_read_complete() == ST_OK && fs_read_complete() == ST_OK);
 CHECK(stat("time.bin", &st) == 0 && st.st_atime == 1400000000L && st.st_mtime == 1450000000L);
 /* Another open's explicit changes must survive this open's later I/O. */
 CHECK(open_name("time.bin", 0xc0010000UL, 0, other) == ST_OK);
 memset(data, 0, 40); filetime(data + 8, 1410000000L);
 filetime(data + 16, 1460000000L);
 CHECK(set_id(other, 4, data, 40) == ST_OK && close_id(other) == ST_OK);
 CHECK(write_id(id, 0UL, 1UL) == ST_OK);
 CHECK(read_id(id, 0UL, 1UL) == ST_OK);
 CHECK(lseek(read_fd, read_offset, 0) == read_offset && read(read_fd, &byte, 1) == 1);
 CHECK(fs_read_complete() == ST_OK);
 CHECK(stat("time.bin", &st) == 0 && st.st_atime == 1410000000L && st.st_mtime == 1460000000L);
 /* Local changes made between SMB operations are likewise preserved. */
 local[0].tv_sec = 1420000000L; local[1].tv_sec = 1470000000L;
 local[0].tv_usec = local[1].tv_usec = 0;
 CHECK(utimes("time.bin", local) == 0);
 CHECK(write_id(id, 0UL, 1UL) == ST_OK);
 CHECK(read_id(id, 0UL, 1UL) == ST_OK);
 CHECK(lseek(read_fd, read_offset, 0) == read_offset && read(read_fd, &byte, 1) == 1);
 CHECK(fs_read_complete() == ST_OK);
 CHECK(stat("time.bin", &st) == 0 && st.st_atime == 1420000000L && st.st_mtime == 1470000000L);
 CHECK(close_id(id) == ST_OK);
 CHECK(open_name("time.bin", 0xc0010000UL, 0, id) == ST_OK);
 CHECK(rename_id(id, "time-renamed.bin", 0) == ST_OK);
 CHECK(info(id, 1, 4, 40) == ST_OK);
 CHECK(!memcmp(response + 72, birth, 8) && memcmp(response + 96, change, 8));
 CHECK(open_name("", 1UL, 1UL, dir) == ST_OK);
 CHECK(directory(dir, 1, 1, "time-renamed.bin", 512) == ST_OK);
 CHECK(!memcmp(response + 80, birth, 8)); CHECK(close_id(dir) == ST_OK);
 CHECK(link("time-renamed.bin", "time-alias") == 0);
 CHECK(open_name("time-alias", 1UL, 0, other) == ST_OK);
 CHECK(!memcmp(response + 72, birth, 8)); CHECK(close_id(other) == ST_OK);
 CHECK(unlink("time-alias") == 0);
 CHECK(close_id(id) == ST_OK); fs_close_all(); close(fs_meta_fd);
 fs_meta_fd = open(".test-meta", O_RDWR); CHECK(fs_meta_fd >= 0);
 CHECK(metadata_init(fs_meta_fd, &root, 0) == 0); fs_init();
 CHECK(open_name("time-renamed.bin", 0xc0010000UL, 0, id) == ST_OK);
 CHECK(!memcmp(response + 72, birth, 8));
 memset(data, 0, 40); memset(data, 255, 8); filetime(data + 16, 1400000000L);
 CHECK(stat("time-renamed.bin", &st) == 0);
 CHECK(set_id(id, 4, data, 40) == ST_NOT_SUPPORTED);
 CHECK(info(id, 1, 4, 40) == ST_OK && !memcmp(response + 72, birth, 8));
 data[0] = 1; CHECK(set_id(id, 13, data, 1) == ST_OK);
 CHECK(close_id(id) == ST_OK);
 CHECK(create_name("time-dir", 0xc0010000UL, 1UL, 2UL, 7UL, dir) == ST_OK);
 memset(data, 0, 40); memcpy(data, birth, 8);
 filetime(data + 8, 1400000000L);
 CHECK(set_id(dir, 4, data, 40) == ST_OK);
 CHECK(directory(dir, 1, 1, "*", 512) == ST_NO_FILE);
 CHECK(stat("time-dir", &st) == 0 && st.st_atime == 1400000000L);
 CHECK(rename_id(dir, "time-dir-renamed", 0) == ST_OK);
 CHECK(info(dir, 1, 4, 40) == ST_OK && !memcmp(response + 72, birth, 8));
 data[0] = 1; CHECK(set_id(dir, 13, data, 1) == ST_OK && close_id(dir) == ST_OK);
 fs_close_all(); close(fs_meta_fd); fs_meta_fd = -1;
 close(fs_state_fd); fs_state_fd = -1; cfg.writable = 0;
 CHECK(unlink(".test-state") == 0 && unlink(".test-meta") == 0);
}

static void
mutable_tests()
{
 u8 id[16], other[16], dir[16], data[40], bytes[256];
 struct stat before, after;
 int fd;
 unsigned i;
 cfg.writable = 1;
 fs_state_fd = open(".test-state", O_CREAT | O_TRUNC | O_RDWR, 0600);
 CHECK(fs_state_fd >= 0); fs_init();
 CHECK(create_name("write.bin", 0xc0010000UL, 0x42UL, 2UL, 7UL, id) == ST_OK);
 CHECK(get32(response + 68) == 2UL);
 archive_storage("write.bin", 1, 1);
 CHECK(write_id(id, 0UL, 65536UL) == ST_OK && get32(response + 68) == 65536UL);
 CHECK(write_id(id, 65536UL, 123UL) == ST_OK);
 CHECK(write_id(id, 0x7fffffffUL, 1UL) == ST_INVALID);
 CHECK(read_id(id, 0UL, 256UL) == ST_OK && read_length == 256);
 CHECK(lseek(read_fd, 0L, 0) == 0 && read(read_fd, bytes, 256) == 256);
 for (i = 0; i < 256; i++) CHECK(bytes[i] == (u8)i);
 memset(data, 0, 8); put64(data, 100UL, 0UL);
 CHECK(set_id(id, 20, data, 8) == ST_OK);
 CHECK(stat("write.bin", &before) == 0 && before.st_size == 100);
 /* Every field is checked before applying any BASIC_INFO change. */
 memset(data, 0, 40); filetime(data, 1600000000L); filetime(data + 16, 1600000000L);
 put32(data + 32, 1UL);
 CHECK(set_id(id, 4, data, 40) == ST_NOT_SUPPORTED);
 CHECK(stat("write.bin", &after) == 0 && after.st_mtime == before.st_mtime && after.st_mode == before.st_mode);
 memset(data, 0, 40); filetime(data + 16, 1600000000L);
 CHECK(set_id(id, 4, data, 40) == ST_OK);
 CHECK(stat("write.bin", &after) == 0 && after.st_mtime == 1600000000L);
 memset(data + 16, 255, 8);
 CHECK(set_id(id, 4, data, 40) == ST_NOT_SUPPORTED);
 CHECK(stat("write.bin", &before) == 0);
 memset(data, 0, 40); filetime(data + 16, 1600000001L);
 put32(data + 32, 0x80UL);
 CHECK(set_id(id, 4, data, 40) == ST_OK);
 CHECK(stat("write.bin", &after) == 0 && after.st_mtime == 1600000001L && after.st_mode == before.st_mode);
 CHECK(info(id, 1, 4, 40) == ST_OK && get32(response + 104) == 0x80UL);
 put32(data + 32, 1UL);
 CHECK(set_id(id, 4, data, 40) == ST_OK);
 CHECK(stat("write.bin", &after) == 0 && !(after.st_mode & 0222));
 CHECK(info(id, 1, 4, 40) == ST_OK && get32(response + 104) == 1UL);
 memset(data, 0, 40); put32(data + 32, 0x21UL);
 CHECK(set_id(id, 4, data, 40) == ST_OK);
 CHECK(stat("write.bin", &after) == 0 && !(after.st_mode & 0222));
 put32(data + 32, 0x20UL);
 CHECK(set_id(id, 4, data, 40) == ST_OK);
 CHECK(stat("write.bin", &after) == 0 && (after.st_mode & 0200));
 CHECK(info(id, 1, 4, 40) == ST_OK && get32(response + 104) == 0x20UL);
 /* Clearing ARCHIVE persists beyond handles and filesystem initialization,
  * is visible in CREATE/QUERY_INFO/enumeration, and survives rename. */
 put32(data + 32, 0x80UL);
 CHECK(set_id(id, 4, data, 40) == ST_OK);
 CHECK(close_id(id) == ST_OK);
 fs_close_all(); fs_init();
 CHECK(open_name("write.bin", 0xc0010000UL, 0, id) == ST_OK);
 CHECK(get32(response + 120) == 0x80UL);
 archive_storage("write.bin", 0, 0);
 CHECK(info(id, 1, 4, 40) == ST_OK && get32(response + 104) == 0x80UL);
 CHECK(link("write.bin", "archive-link") == 0);
 CHECK(open_name("archive-link", 0x80000000UL, 0, other) == ST_OK);
 CHECK(get32(response + 120) == 0x80UL);
 CHECK(close_id(other) == ST_OK); CHECK(unlink("archive-link") == 0);
 CHECK(rename_id(id, "archive-renamed.bin", 0) == ST_OK);
 CHECK(open_name("", 1UL, 1UL, dir) == ST_OK);
 CHECK(directory(dir, 1, 1, "archive-renamed.bin", 512) == ST_OK);
 CHECK(get32(response + 128) == 0x80UL);
 CHECK(close_id(dir) == ST_OK);
 CHECK(rename_id(id, "write.bin", 0) == ST_OK);
 CHECK(write_id(id, 0UL, 1UL) == ST_OK);
 archive_storage("write.bin", 1, 0);
 CHECK(info(id, 1, 4, 40) == ST_OK && get32(response + 104) == 0x20UL);
 CHECK(set_id(id, 4, data, 40) == ST_OK);
 memset(data, 0, 8); put64(data, 101UL, 0UL);
 CHECK(set_id(id, 20, data, 8) == ST_OK);
 CHECK(info(id, 1, 4, 40) == ST_OK && get32(response + 104) == 0x20UL);
 CHECK(rename_id(id, "..\\escape.bin", 0) != ST_OK);
 CHECK(rename_id(id, "renamed.bin", 0) == ST_OK);
 CHECK(stat("write.bin", &after) < 0 && stat("renamed.bin", &after) == 0);
 CHECK(open_name("renamed.bin", 1UL, 0, other) == ST_OK);
 data[0] = 1;
 CHECK(set_id(id, 13, data, 1) == ST_OK);
 CHECK(open_name("renamed.bin", 1UL, 0, dir) == 0xc0000056UL);
 CHECK(close_id(id) == ST_OK && stat("renamed.bin", &after) == 0);
 CHECK(read_id(other, 0UL, 10UL) == ST_OK);
 CHECK(close_id(other) == ST_OK && stat("renamed.bin", &after) < 0);

 CHECK(create_name("shared.bin", 0xc0010000UL, 0, 2UL, 1UL, id) == ST_OK);
 CHECK(create_name("shared.bin", 0x40000000UL, 0, 1UL, 7UL, other) == 0xc0000043UL);
 CHECK(close_id(id) == ST_OK);
 CHECK(create_name("shared.bin", 0x80000000UL, 0, 1UL, 7UL, id) == ST_OK);
 CHECK(create_name("shared.bin", 0x80000000UL, 0, 1UL, 0UL, other) == 0xc0000043UL);
 CHECK(close_id(id) == ST_OK);
 CHECK(create_name("shared.bin", 0xc0010000UL, 0, 0UL, 7UL, id) == ST_OK);
 CHECK(get32(response + 68) == 0UL); CHECK(close_id(id) == ST_OK);
 CHECK(create_name("shared.bin", 0xc0010000UL, 0x1000UL, 1UL, 7UL, id) == ST_OK);
 CHECK(close_id(id) == ST_OK && stat("shared.bin", &after) < 0);

 CHECK(create_name("newdir", 0xc0010000UL, 1UL, 2UL, 7UL, dir) == ST_OK);
 memset(data, 0, 40); put32(data + 32, 0x80UL);
 CHECK(set_id(dir, 4, data, 40) == ST_OK);
 CHECK(stat("newdir", &before) == 0);
 filetime(data + 16, 1600000000L); put32(data + 32, 1UL);
 CHECK(set_id(dir, 4, data, 40) == ST_NOT_SUPPORTED);
 CHECK(stat("newdir", &after) == 0 && after.st_mtime == before.st_mtime && after.st_mode == before.st_mode);
 put32(data + 32, 0x20UL);
 CHECK(set_id(dir, 4, data, 40) == ST_OK);
 CHECK(info(dir, 1, 4, 40) == ST_OK && get32(response + 104) == 0x30UL);
 memset(data, 0, 40); put32(data + 32, 0x80UL);
 CHECK(set_id(dir, 4, data, 40) == ST_OK);
 CHECK(info(dir, 1, 4, 40) == ST_OK && get32(response + 104) == 0x10UL);
 CHECK(chmod("newdir", 0500) == 0);
 memset(data, 0, 40); put32(data + 32, 1UL);
 CHECK(set_id(dir, 4, data, 40) == ST_OK);
 put32(data + 32, 0x80UL);
 CHECK(set_id(dir, 4, data, 40) == ST_NOT_SUPPORTED);
 CHECK(chmod("newdir", 0700) == 0);
 CHECK(create_name("newdir/child", 0xc0010000UL, 0, 2UL, 7UL, id) == ST_OK);
 CHECK(rename_id(dir, "moved-dir", 0) == 0xc0000043UL);
 data[0] = 1; CHECK(set_id(dir, 13, data, 1) == 0xc0000101UL);
 CHECK(set_id(id, 13, data, 1) == ST_OK && close_id(id) == ST_OK);
 CHECK(rename_id(dir, "moved-dir", 0) == ST_OK);
 CHECK(set_id(dir, 13, data, 1) == ST_OK && close_id(dir) == ST_OK);
 CHECK(stat("moved-dir", &after) < 0);
 /* Unsupported CREATE attributes are rejected before making an object. */
 CHECK(create_name("bad-attribute", 0xc0010000UL, 0, 1UL, 7UL, id) == ST_NOT_FOUND);
 put32(request + 100, 2UL); put32(request + 92, 0x100UL);
 CHECK(fs_dispatch(5) == ST_NOT_SUPPORTED);
 CHECK(stat("bad-attribute", &after) < 0);
 put32(request + 104, 1UL); put32(request + 92, 1UL);
 CHECK(fs_dispatch(5) == ST_NOT_SUPPORTED);
 CHECK(stat("bad-attribute", &after) < 0);
 put32(request + 104, 0x1000UL); put32(request + 92, 1UL);
 CHECK(fs_dispatch(5) == ST_DENIED);
 CHECK(stat("bad-attribute", &after) < 0);
 fs_close_all(); close(fs_state_fd); fs_state_fd = -1;
 CHECK(unlink(".test-state") == 0); cfg.writable = 0;
 fd = open("Zero", O_RDONLY); CHECK(fd >= 0); close(fd);
}

int
main()
{
 char root[] = "/tmp/smbd-fs-XXXXXX";
 char name[256];
 u8 id[16], old[16], dir[16], ids[SMBD_HANDLES][16], data[1000];
 FILE *f;
 unsigned i, count, cls, base, n, off;
 u32 s;
 int fd;
 CHECK(mkdtemp(root) != 0); CHECK(chdir(root) == 0);
 CHECK(mkdir("Nested", 0700) == 0); CHECK(mkdir("Empty", 0700) == 0);
 f = fopen("Nested/Binary File.bin", "wb"); CHECK(f != 0);
 for (i = 0; i < sizeof(data); i++) data[i] = i;
 for (i = 0; i < 100; i++) CHECK(fwrite(data, 1, sizeof(data), f) == sizeof(data));
 CHECK(fclose(f) == 0);
 f = fopen("Zero", "wb"); CHECK(f != 0); CHECK(fclose(f) == 0);
 CHECK(symlink("/etc/passwd", "escape") == 0);
 CHECK(symlink("/", "outside") == 0);
 cfg.share = "test"; fs_init();

 CHECK(open_name("nested\\binary file.BIN", 0x80000000UL, 0, id) == ST_OK);
 CHECK(get16(response + 64) == 89); CHECK(get32(response + 112) == 100000UL);
 memcpy(old, id, 16);
 CHECK(read_id(id, 0UL, 65536UL) == ST_OK);
 CHECK(read_length == 65536L && read_offset == 0 && response_len == 80);
 CHECK(get32(response + 68) == 65536UL);
 CHECK(lseek(read_fd, 0L, 0) == 0L && read(read_fd, data, 1000) == 1000);
 for (i = 0; i < 1000; i++) CHECK(data[i] == (u8)i);
 CHECK(read_id(id, 65536UL, 65536UL) == ST_OK && read_length == 34464L);
 CHECK(read_id(id, 100000UL, 1UL) == ST_EOF);
 CHECK(read_id(id, 0UL, 65537UL) == ST_INVALID);
 CHECK(read_id(id, 0x80000000UL, 1UL) == ST_INVALID);
 CHECK(info(id, 1, 18, 100) == ST_OK && response_len == 172);
 CHECK(get32(response + 120) == 100000UL);
 CHECK(info(id, 1, 5, 23) == ST_TOO_SMALL);
 CHECK(info(id, 1, 22, 100) == ST_OK && get32(response + 76) == 14UL);
 CHECK(info(id, 1, 250, 100) == ST_NOT_SUPPORTED);
 for (i = 1; i <= 7; i++) if (i != 2 && i != 6)
  CHECK(info(id, 2, i, 512) == ST_OK);
 CHECK(close_id(id) == ST_OK);
 CHECK(close_id(old) == ST_BAD_HANDLE);

 CHECK(open_name("..\\etc\\passwd", 1UL, 0, id) != ST_OK);
 CHECK(open_name("/etc/passwd", 1UL, 0, id) != ST_OK);
 CHECK(open_name("Nested\\..\\Zero", 1UL, 0, id) != ST_OK);
 CHECK(open_name("escape", 1UL, 0, id) != ST_OK);
 CHECK(open_name("outside\\etc\\passwd", 1UL, 0, id) != ST_OK);
 CHECK(open_name("Zero:stream", 1UL, 0, id) != ST_OK);
 CHECK(open_name("Zero", 0x40000000UL, 0, id) == ST_DENIED);
 CHECK(open_name("Zero", 1UL, 0x1000UL, id) == ST_DENIED);
 CHECK(open_name("Zero", 1UL, 1UL, id) != ST_OK);
 CHECK(open_name("Nested", 1UL, 0x40UL, id) != ST_OK);
 CHECK(open_name("Zero", 1UL, 0UL, id) == ST_OK);
 CHECK(read_id(id, 0UL, 1UL) == ST_EOF);
 CHECK(close_id(id) == ST_OK);

 CHECK(open_name("", 1UL, 1UL, dir) == ST_OK);
 for (cls = 1; cls <= 38; cls++) {
  if (cls != 1 && cls != 2 && cls != 3 && cls != 12 && cls != 37 && cls != 38)
   continue;
  count = 0;
  s = directory(dir, cls, 1, "*", 160);
  while (s == ST_OK) {
   off = 72;
   do {
    base = cls == 12 ? 12 : cls == 1 ? 64 : cls == 2 ? 68 :
           cls == 3 ? 94 : cls == 37 ? 104 : 80;
    n = (unsigned)get32(response + off + (cls == 12 ? 8 : 60));
    CHECK(utf16_decode(name, sizeof(name), response + off + base, n) == 0);
    CHECK(!strcmp(name, "Nested") || !strcmp(name, "Empty") || !strcmp(name, "Zero"));
    count++;
    n = (unsigned)get32(response + off);
    if (n) CHECK((n & 7) == 0 && off + n < response_len);
    off += n;
   } while (n);
   s = directory(dir, cls, 0, 0, 160);
  }
  CHECK(count == 3 && s == ST_NO_MORE);
 }
 CHECK(directory(dir, 1, 17, "z*", 512) == ST_OK);
 CHECK(directory(dir, 1, 0, 0, 512) == ST_NO_MORE);
 CHECK(directory(dir, 1, 17, "not here", 512) == ST_NO_FILE);
 CHECK(directory(dir, 1, 17, "*", 64) == ST_TOO_SMALL);
 CHECK(directory(dir, 1, 0, 0, 512) == ST_OK);
 CHECK(close_id(dir) == ST_OK);
 CHECK(open_name("Empty", 1UL, 1UL, dir) == ST_OK);
 CHECK(directory(dir, 1, 1, "*", 512) == ST_NO_FILE);
 CHECK(directory(dir, 1, 0, 0, 512) == ST_NO_MORE);
 CHECK(close_id(dir) == ST_OK);

 for (i = 0; i < SMBD_HANDLES; i++) CHECK(open_name("Zero", 1UL, 0, ids[i]) == ST_OK);
 CHECK(open_name("Zero", 1UL, 0, id) == 0xc000011fUL);
 for (i = 0; i < SMBD_HANDLES; i++) CHECK(close_id(ids[i]) == ST_OK);
 fs_close_all();
 CHECK(open_name("Zero", 1UL, 0, id) == ST_OK);
 CHECK(close_id(id) == ST_OK);
 fs_close_all();

 /* No malformed wire request may make a filename pointer escape request[]. */
 for (i = 0; i < 1000; i++) {
  reset(); put16(request + 64, 57); put32(request + 100, 1UL);
  put16(request + 108, (i * 71) & 65535); put16(request + 110, 600);
  request_len = 120; CHECK(fs_dispatch(5) == ST_INVALID);
 }
 fd = open("Zero", O_RDONLY); CHECK(fd >= 0); close(fd);
 mutable_tests();
 metadata_fs_tests();
 unlink("Nested/Binary File.bin"); rmdir("Nested"); rmdir("Empty");
 unlink("Zero"); unlink("escape"); unlink("outside");
 CHECK(chdir("/") == 0); CHECK(rmdir(root) == 0);
 printf("fs: %d checks passed\n", checks);
 return 0;
}
