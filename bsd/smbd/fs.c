/* Bounded filesystem service. Wire layouts are from MS-SMB2 / MS-FSCC.
 * No native C structure is copied onto the wire. Paths and names are ASCII;
 * unsupported encodings and alternate data streams are rejected explicitly.
 */
#include "smbd.h"
#include <fcntl.h>
#include <sys/file.h>
#ifndef PDP11
#include <sys/time.h>
#endif
#ifdef __linux__
#include <sys/vfs.h>
#include <sys/xattr.h>
#else
#include <sys/mount.h>
#endif

/* 2.11BSD reserves user flag bits 0x08..0x80; macOS reserves 0x0100..0x4000.
 * These inode flags store SMB ARCHIVE without changing Unix permission bits.
 * They are deliberately platform-specific: Darwin 0x80 is UF_DATAVAULT. */
#ifdef PDP11
#define SMBD_ARCHIVE 0x0080
#else
#ifdef UF_ARCHIVE
#define SMBD_ARCHIVE UF_ARCHIVE
#else
#ifndef __linux__
#define SMBD_ARCHIVE 0x0800
#endif
#endif
#endif

#ifdef PDP11
#define DIRENT struct direct
#else
#define DIRENT struct dirent
#endif
#ifndef MAXNAMLEN
#define MAXNAMLEN 255
#endif
#define NAME_LIMIT 255
#define ST_NAME_INVALID 0xc0000033UL
#define ST_NOT_DIRECTORY 0xc0000103UL
#define ST_IS_DIRECTORY 0xc00000baUL
#define ST_TOO_MANY 0xc000011fUL
#define ST_NO_EAS 0xc0000052UL
#define READ_ACCESS 0x001200a9UL
#define WRITE_ACCESS 0x500d0156UL
#define FULL_ACCESS 0x001301ffUL
#define ST_SHARING 0xc0000043UL
#define ST_DELETE_PENDING 0xc0000056UL
#define ST_COLLISION 0xc0000035UL
#define ST_NOT_EMPTY 0xc0000101UL
#define ST_DISK_FULL 0xc000007fUL
#define STATE_ROWS (SMBD_HANDLES * 16 * 2)
#define STATE_SIZE (SMBD_PATH + 40)
#define STATE_DELETE 1UL
#define STATE_DOC 2UL

struct state_entry {
 u32 pid, serial, device, inode, access, share, flags;
 char path[SMBD_PATH];
};

struct handle {
 int fd;
 DIR *dir;
 u32 serial, access, tree;
 int searched, state_slot, write_through, user_change, user_times;
 char path[SMBD_PATH];
 char pattern[NAME_LIMIT + 1];
};
static struct handle handles[SMBD_HANDLES];
static u32 next_serial;
static int initialized, rootfd = -1;
/* Scratch is shared by sequential commands, keeping the PDP-11 stack small. */
static char input_path[SMBD_PATH], resolved_path[SMBD_PATH];
static char component[NAME_LIMIT + 1], actual_name[NAME_LIMIT + 1];
static u8 match_a[NAME_LIMIT + 1], match_b[NAME_LIMIT + 1];
static u8 state_buffer[STATE_SIZE];
static unsigned state_rows;
static int state_depth;
static struct handle *pending_read_times;
static struct timeval saved_read_times[2];

static u32 state_sweep();
static u32 delete_path();
static int archive_set();
static int time_prepare();
static int time_change();

/* The parent opens the same private state inode independently for each
 * worker. flock on duplicated descriptions would not serialize siblings.
 * Disk rows use explicit encodings; only one row is buffered in memory. */
static int
state_enter()
{
 long end;
 if (!cfg.writable) return 0;
 if (state_depth) { state_depth++; return 0; }
 if (fs_state_fd < 0) return -1;
 while (flock(fs_state_fd, LOCK_EX) < 0) if (errno != EINTR) return -1;
 end = lseek(fs_state_fd, 0L, 2);
 if (end < 0 || end % STATE_SIZE || end / STATE_SIZE > STATE_ROWS) {
  flock(fs_state_fd, LOCK_UN); return -1;
 }
 state_rows = (unsigned)(end / STATE_SIZE); state_depth = 1;
 return 0;
}

static void
state_leave()
{
 if (cfg.writable && state_depth && !--state_depth)
  flock(fs_state_fd, LOCK_UN);
}

static unsigned
state_checksum()
{
 unsigned a, b, i;
 a = b = 0;
 for (i = 0; i < STATE_SIZE; i++) {
  if (i == 38 || i == 39) continue;
  a += state_buffer[i]; if (a >= 255) a -= 255;
  b += a; if (b >= 255) b -= 255;
 }
 return a | (b << 8);
}

static int
state_read(slot, e)
unsigned slot;
struct state_entry *e;
{
 unsigned done;
 int n;
 memset(e, 0, sizeof(*e));
 if (slot >= state_rows) return 0;
 if (lseek(fs_state_fd, (long)slot * STATE_SIZE, 0) < 0) return -1;
 done = 0;
 while (done < STATE_SIZE) {
  n = read(fs_state_fd, state_buffer + done, STATE_SIZE - done);
  if (n < 0 && errno == EINTR) continue;
  if (n <= 0) return -1;
  done += n;
 }
 /* A torn registry update fails closed rather than dropping a share lock. */
 if (get16(state_buffer + 38) != state_checksum()) return -1;
 e->pid = get32(state_buffer); e->serial = get32(state_buffer + 4);
 e->device = get32(state_buffer + 8) | ((get32(state_buffer + 12) << 16) << 16);
 e->inode = get32(state_buffer + 16) | ((get32(state_buffer + 20) << 16) << 16);
 e->access = get32(state_buffer + 24); e->share = get32(state_buffer + 28);
 e->flags = get32(state_buffer + 32);
 n = get16(state_buffer + 36);
 if (n >= SMBD_PATH) return -1;
 memcpy(e->path, state_buffer + 40, n); e->path[n] = 0;
 return 0;
}

static int
state_write(slot, e)
unsigned slot;
struct state_entry *e;
{
 unsigned done, len;
 int n;
 if (slot >= STATE_ROWS) return -1;
 memset(state_buffer, 0, sizeof(state_buffer));
 put32(state_buffer, e->pid); put32(state_buffer + 4, e->serial);
 put64(state_buffer + 8, e->device, (e->device >> 16) >> 16);
 put64(state_buffer + 16, e->inode, (e->inode >> 16) >> 16);
 put32(state_buffer + 24, e->access); put32(state_buffer + 28, e->share);
 put32(state_buffer + 32, e->flags);
 len = strlen(e->path); put16(state_buffer + 36, len);
 memcpy(state_buffer + 40, e->path, len);
 put16(state_buffer + 38, state_checksum());
 if (lseek(fs_state_fd, (long)slot * STATE_SIZE, 0) < 0) return -1;
 done = 0;
 while (done < STATE_SIZE) {
  n = write(fs_state_fd, state_buffer + done, STATE_SIZE - done);
  if (n < 0 && errno == EINTR) continue;
  if (n <= 0) return -1;
  done += n;
 }
 if (slot >= state_rows) state_rows = slot + 1;
 return 0;
}

static int
state_same(e, st)
struct state_entry *e;
struct stat *st;
{
 return e->device == (u32)st->st_dev && e->inode == (u32)st->st_ino;
}

static unsigned
share_bits(access)
u32 access;
{
 unsigned bits;
 bits = 0;
 if (access & 0x21UL) bits |= 1;
 if (access & 6UL) bits |= 2;
 if (access & 0x10000UL) bits |= 4;
 return bits;
}

static u32
state_check(st, access, share)
struct stat *st;
u32 access, share;
{
 struct state_entry e;
 unsigned i, needed;
 if (!cfg.writable) return ST_OK;
 needed = share_bits(access);
 for (i = 0; i < state_rows; i++) {
  if (state_read(i, &e)) return ST_DENIED;
  if ((!e.pid && !e.flags) || !state_same(&e, st)) continue;
  if (e.flags & STATE_DELETE) return ST_DELETE_PENDING;
  if (e.pid && ((needed & ~e.share) || (share_bits(e.access) & ~share)))
   return ST_SHARING;
 }
 return ST_OK;
}

static int
state_register(h, st, share, options)
struct handle *h;
struct stat *st;
u32 share, options;
{
 struct state_entry e;
 unsigned i;
 for (i = 0; i < state_rows; i++) {
  if (state_read(i, &e)) return -1;
  if (!e.pid && !e.flags) break;
 }
 if (i >= STATE_ROWS) return -1;
 memset(&e, 0, sizeof(e));
 e.pid = (u32)(unsigned)getpid(); e.serial = h->serial;
 e.device = (u32)st->st_dev; e.inode = (u32)st->st_ino;
 e.access = h->access; e.share = share;
 if (options & 0x1000UL) e.flags = STATE_DELETE | STATE_DOC;
 strcpy(e.path, h->path);
 if (state_write(i, &e)) return -1;
 h->state_slot = i;
 return 0;
}

static int
state_release(h)
struct handle *h;
{
 struct state_entry e;
 if (!cfg.writable || h->state_slot < 0) return 0;
 if (state_read(h->state_slot, &e)) return -1;
 if (e.pid != (u32)(unsigned)getpid() || e.serial != h->serial) return -1;
 if (e.flags & STATE_DELETE) { e.pid = 0; e.access = 0; }
 else memset(&e, 0, sizeof(e));
 return state_write(h->state_slot, &e);
}

/* Replaced inodes can outlive their pathname through another SMB handle.
 * Keep their metadata until that last open closes. */
static int
metadata_unused(st)
struct stat *st;
{
 struct state_entry e;
 unsigned i;
 if (!cfg.writable || fs_meta_fd < 0 || st->st_nlink) return 0;
 for (i = 0; i < state_rows; i++) {
  if (state_read(i, &e)) return -1;
  if (e.pid && state_same(&e, st)) return 0;
 }
 return metadata_remove(st);
}

/* Never use worker-controlled paths in the privileged parent. Dead opens
 * become harmless pending-delete rows; a confined worker removes the name. */
void
fs_reap(pid)
int pid;
{
 struct state_entry e;
 unsigned i;
 if (!cfg.writable || state_enter()) return;
 for (i = 0; i < state_rows; i++) {
  if (state_read(i, &e)) break;
  if (e.pid != (u32)(unsigned)pid) continue;
  if (e.flags & STATE_DELETE) { e.pid = 0; e.access = 0; }
  else memset(&e, 0, sizeof(e));
  if (state_write(i, &e)) break;
 }
 state_leave();
}

static int
isdir(st)
struct stat *st;
{
 return (st->st_mode & S_IFMT) == S_IFDIR;
}

static int
isregular(st)
struct stat *st;
{
 return (st->st_mode & S_IFMT) == S_IFREG;
}

static u32
os_error()
{
 switch (errno) {
 case ENOENT: return ST_NOT_FOUND;
 case ENOTDIR: return ST_NOT_DIRECTORY;
 case EACCES: case EPERM: return ST_DENIED;
 case EMFILE: case ENFILE: return ST_TOO_MANY;
 case EEXIST: return ST_COLLISION;
 case ENOTEMPTY: return ST_NOT_EMPTY;
 case ENOSPC: return ST_DISK_FULL;
 default: return ST_DENIED;
 }
}

static int
lower(c)
int c;
{
 if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
 return c;
}

static int
same_name(a, b)
const char *a, *b;
{
 while (*a && *b) {
  if (lower((unsigned char)*a++) != lower((unsigned char)*b++)) return 0;
 }
 return *a == *b;
}

/* The on-disk namespace is deliberately a subset of ASCII. No replacement
 * characters, truncation, stream syntax, or implicit dot-component removal. */
static int
valid_name(s, pattern)
const char *s;
int pattern;
{
 unsigned n;
 int c;
 n = 0;
 while ((c = (unsigned char)*s++) != 0) {
  if (++n > NAME_LIMIT || c < 32 || c > 126 || c == '/' ||
      c == '\\' || c == ':' || c == '|') return 0;
  if (!pattern && (c == '*' || c == '?' || c == '<' ||
                   c == '>' || c == '"')) return 0;
 }
 return n != 0;
}

/* Wildcards include the DOS_STAR / DOS_QM / DOS_DOT forms in MS-FSCC 2.1.4.3.
 * Dynamic programming bounds work to 256*256, with no recursive stack. */
static int
matches(pattern, name)
const char *pattern, *name;
{
 unsigned i, j, n, lastdot;
 int c;
 u8 *a, *b, *t;
 n = strlen(name);
 if (n > NAME_LIMIT) return 0;
 lastdot = n;
 for (i = 0; i < n; i++) if (name[i] == '.') lastdot = i;
 a = match_a; b = match_b;
 memset(a, 0, n + 1); a[0] = 1;
 for (i = 0; pattern[i]; i++) {
  c = (unsigned char)pattern[i];
  memset(b, 0, n + 1);
  for (j = 0; j <= n; j++) {
   if (c == '*' || c == '<') {
    if (a[j]) b[j] = 1;
    if (j && b[j-1] && (c == '*' || j-1 != lastdot)) b[j] = 1;
   } else if (c == '>') {
    if (a[j]) {
     if (j == n || name[j] == '.') b[j] = 1;
     else b[j+1] = 1;
    }
   } else if (c == '"') {
    if (a[j] && j == n) b[j] = 1;
    if (a[j] && j < n && name[j] == '.') b[j+1] = 1;
   } else if (a[j] && j < n &&
              (c == '?' || lower(c) == lower((unsigned char)name[j]))) {
    b[j+1] = 1;
   }
  }
  t = a; a = b; b = t;
 }
 return a[n] != 0;
}

void
fs_init()
{
 unsigned i;
 if (initialized) {
  fs_close_all();
  if (rootfd >= 0) close(rootfd);
 }
 for (i = 0; i < SMBD_HANDLES; i++) {
  handles[i].fd = -1; handles[i].state_slot = -1;
 }
 next_serial = 0;
 rootfd = open(".", O_RDONLY);
 if (rootfd < 0 && cfg.verbose) perror("smbd: open share root");
 initialized = 1;
}

void
fs_close_all()
{
 unsigned i;
 struct stat st;
 if (!initialized) return;
 fs_read_complete();
 if (state_enter()) return;
 for (i = 0; i < SMBD_HANDLES; i++) {
  if (handles[i].fd >= 0) state_release(&handles[i]);
  if (handles[i].fd >= 0 && fstat(handles[i].fd, &st) == 0) metadata_unused(&st);
  if (handles[i].dir) closedir(handles[i].dir);
  if (handles[i].fd >= 0) close(handles[i].fd);
  memset(&handles[i], 0, sizeof(handles[i]));
  handles[i].fd = -1;
  handles[i].state_slot = -1;
 }
 if (cfg.writable) state_sweep();
 state_leave();
 /* Tree disconnect and logoff close handles but permit a later tree connect
  * on this transport. The process owns rootfd until reinitialization/exit. */
}

void
fs_close_tree(tree)
u32 tree;
{
 unsigned i;
 struct stat st;
 fs_read_complete();
 if (state_enter()) return;
 for (i = 0; i < SMBD_HANDLES; i++) {
  if (handles[i].fd < 0 || handles[i].tree != tree) continue;
  state_release(&handles[i]);
  if (fstat(handles[i].fd, &st) == 0) metadata_unused(&st);
  if (handles[i].dir) closedir(handles[i].dir);
  close(handles[i].fd);
  memset(&handles[i], 0, sizeof(handles[i]));
  handles[i].fd = -1;
  handles[i].state_slot = -1;
 }
 if (cfg.writable) state_sweep();
 state_leave();
}

static void
file_id(p, h)
u8 *p;
struct handle *h;
{
 put64(p, (u32)(h - handles + 1), 0UL);
 put64(p + 8, h->serial, 0UL);
}

static struct handle *
find_handle(p)
const u8 *p;
{
 unsigned i;
 u32 slot;
 struct state_entry e;
 for (i = 0; i < 16 && p[i] == 255; i++) ;
 if (i == 16) {
  if (!(get32(request + 16) & 4UL)) return 0;
  p = related_file;
 }
 slot = get32(p);
 if (slot < 1 || slot > SMBD_HANDLES || get32(p + 4) ||
     get32(p + 12)) return 0;
 i = (unsigned)slot - 1;
 if (handles[i].fd < 0 || handles[i].serial != get32(p + 8) ||
     handles[i].tree != get32(request + 36)) return 0;
 if (cfg.writable) {
  if (handles[i].state_slot < 0 || state_read(handles[i].state_slot, &e) ||
      e.pid != (u32)(unsigned)getpid() || e.serial != handles[i].serial) return 0;
  strcpy(handles[i].path, e.path);
 }
 return &handles[i];
}

/* Resolve one component at a time. Modern hosts use directory descriptors
 * and O_NOFOLLOW, including during case-insensitive lookup. Native 2.11BSD
 * workers are chrooted before fs_init; lstat refuses every symlink component.
 */
static u32
open_path(name, result)
const char *name;
int *result;
{
 const char *p;
 unsigned n, used;
 int fd, newfd, exact, hits;
 DIR *d;
 DIRENT *de;
 struct stat st;
 u32 status;
 if (rootfd < 0) return ST_DENIED;
 if (*name == '/' || *name == '\\') return ST_NAME_INVALID;
 fd = dup(rootfd);
 if (fd < 0) return os_error();
 strcpy(resolved_path, "."); used = 1;
 p = name;
 while (*p) {
  n = 0;
  while (*p && *p != '/' && *p != '\\') {
   if (n == NAME_LIMIT) { close(fd); return ST_NAME_INVALID; }
   component[n++] = *p++;
  }
  component[n] = 0;
  if (!valid_name(component, 0) || !strcmp(component, ".") ||
      !strcmp(component, "..")) { close(fd); return ST_NAME_INVALID; }
  if (*p) p++;
  if (*p == '/' || *p == '\\') { close(fd); return ST_NAME_INVALID; }
#ifdef PDP11
  d = opendir(resolved_path);
#else
  newfd = dup(fd);
  d = newfd < 0 ? 0 : fdopendir(newfd);
  if (!d && newfd >= 0) close(newfd);
#endif
  if (!d) { status = os_error(); close(fd); return status; }
  rewinddir(d);
  hits = exact = 0;
  while ((de = readdir(d)) != 0) {
   if (!valid_name(de->d_name, 0)) continue;
   if (!same_name(component, de->d_name)) continue;
   hits++;
   strcpy(actual_name, de->d_name);
   if (!strcmp(component, de->d_name)) { exact = 1; break; }
  }
  closedir(d);
  if (!hits) { close(fd); return ST_NOT_FOUND; }
  if (!exact && hits > 1) { close(fd); return ST_NAME_INVALID; }
  n = strlen(actual_name);
  if (used + 1 + n >= SMBD_PATH) { close(fd); return ST_NAME_INVALID; }
  resolved_path[used++] = '/';
  strcpy(resolved_path + used, actual_name); used += n;
#ifdef PDP11
  if (lstat(resolved_path, &st) < 0) {
   status = os_error(); close(fd); return status;
  }
  if (!isdir(&st) && !isregular(&st)) { close(fd); return ST_DENIED; }
  newfd = open(resolved_path, O_RDONLY | O_NDELAY);
#else
  newfd = openat(fd, actual_name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
#endif
  if (newfd < 0) { status = os_error(); close(fd); return status; }
  close(fd); fd = newfd;
  if (fstat(fd, &st) < 0) { status = os_error(); close(fd); return status; }
  if ((!isdir(&st) && !isregular(&st)) || (*p && !isdir(&st))) {
   close(fd); return *p ? ST_NOT_DIRECTORY : ST_DENIED;
  }
  if (cfg.writable && *p && (status = state_check(&st, 0UL, 7UL)) != ST_OK) {
   close(fd); return status;
  }
 }
 *result = fd;
 return ST_OK;
}

static int
join_path(out, parent, leaf)
char *out;
const char *parent, *leaf;
{
 unsigned n, len;
 n = strlen(parent); len = strlen(leaf);
 if (n + len + 2 > SMBD_PATH) return -1;
 if (out != parent) strcpy(out, parent);
 out[n++] = '/'; strcpy(out + n, leaf);
 return 0;
}

/* Resolve and retain a directory descriptor before any host mutation. */
static u32
parent_path(name, fd, leaf)
const char *name;
int *fd;
char *leaf;
{
 char path[SMBD_PATH], *p, *last;
 struct stat st;
 u32 status;
 if (!*name || *name == '/' || *name == '\\' || strlen(name) >= SMBD_PATH)
  return ST_NAME_INVALID;
 strcpy(path, name); last = 0;
 for (p = path; *p; p++) {
  if (*p == '\\') *p = '/';
  if (*p == '/') last = p;
 }
 p = last ? last + 1 : path;
 if (!valid_name(p, 0) || !strcmp(p, ".") || !strcmp(p, "..")) return ST_NAME_INVALID;
 strcpy(leaf, p);
 if (last) *last = 0; else path[0] = 0;
 status = open_path(path, fd);
 if (status) return status;
 if (fstat(*fd, &st) < 0) { status = os_error(); close(*fd); return status; }
 if (!isdir(&st)) { close(*fd); return ST_NOT_DIRECTORY; }
 status = state_check(&st, 0UL, 7UL);
 if (status) close(*fd);
 return status;
}

static u32
entry_stat(fd, parent, leaf, st)
int fd;
const char *parent, *leaf;
struct stat *st;
{
#ifdef PDP11
 char path[SMBD_PATH];
 if (join_path(path, parent, leaf)) return ST_NAME_INVALID;
 if (lstat(path, st) < 0) return os_error();
#else
 (void)parent;
 if (fstatat(fd, leaf, st, AT_SYMLINK_NOFOLLOW) < 0) return os_error();
#endif
 if (!isregular(st) && !isdir(st)) return ST_DENIED;
 return ST_OK;
}

/* A missing name is distinct from an ambiguous case-insensitive match. */
static u32
entry_lookup(fd, parent, leaf, st)
int fd;
const char *parent;
char *leaf;
struct stat *st;
{
 DIR *dir;
 DIRENT *de;
 int other, hits, exact;
 char selected[NAME_LIMIT + 1];
#ifdef PDP11
 dir = opendir(parent);
#else
 other = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
 dir = other < 0 ? 0 : fdopendir(other);
 if (!dir && other >= 0) close(other);
#endif
 if (!dir) return os_error();
 hits = exact = 0;
 while ((de = readdir(dir)) != 0) {
  if (!valid_name(de->d_name, 0) || !same_name(leaf, de->d_name)) continue;
  hits++; strcpy(selected, de->d_name);
  if (!strcmp(leaf, de->d_name)) { exact = 1; break; }
 }
 closedir(dir);
 if (!hits) return ST_NOT_FOUND;
 if (!exact && hits > 1) return ST_NAME_INVALID;
 strcpy(leaf, selected);
 return entry_stat(fd, parent, leaf, st);
}

static u32
delete_path(e)
struct state_entry *e;
{
 char leaf[NAME_LIMIT + 1];
 struct stat st;
 int fd, result;
 u32 status;
 if (!e->path[0]) return ST_OK; /* replaced/unlinked while handle stayed open */
 if (e->path[0] != '.' || e->path[1] != '/') return ST_DENIED;
 status = parent_path(e->path + 2, &fd, leaf);
 if (status) return status == ST_NOT_FOUND ? ST_OK : status;
 status = entry_stat(fd, resolved_path, leaf, &st);
 if (status || !state_same(e, &st)) {
  close(fd); return status == ST_NOT_FOUND || !status ? ST_OK : status;
 }
#ifdef PDP11
 result = isdir(&st) ? rmdir(e->path) : unlink(e->path);
#else
 result = unlinkat(fd, leaf, isdir(&st) ? AT_REMOVEDIR : 0);
#endif
 status = result < 0 ? os_error() : ST_OK;
 if (!status && (isdir(&st) || st.st_nlink == 1) && metadata_remove(&st) < 0)
  status = os_error();
 close(fd); return status;
}

static u32
state_sweep()
{
 struct state_entry e, other;
 unsigned i, j;
 int active;
 u32 status, result;
 result = ST_OK;
 for (i = 0; i < state_rows; i++) {
  if (state_read(i, &e)) return ST_DENIED;
  if (e.pid || !(e.flags & STATE_DELETE)) continue;
  active = 0;
  for (j = 0; j < state_rows; j++) {
   if (state_read(j, &other)) return ST_DENIED;
   if (other.pid && e.device == other.device && e.inode == other.inode) {
    active = 1; break;
   }
  }
  if (active) continue;
  status = delete_path(&e);
  if (status && !result) result = status;
  memset(&e, 0, sizeof(e));
  if (state_write(i, &e)) return ST_DENIED;
 }
 return result;
}

static int
state_path_change(from, to)
const char *from, *to;
{
 struct state_entry e;
 unsigned i;
 for (i = 0; i < state_rows; i++) {
  if (state_read(i, &e)) return -1;
  if ((!e.pid && !e.flags) || strcmp(e.path, from)) continue;
  strcpy(e.path, to);
  if (state_write(i, &e)) return -1;
 }
 return 0;
}

static u32
directory_empty(fd, path)
int fd;
const char *path;
{
 DIR *dir;
 DIRENT *de;
 int other, empty;
#ifdef PDP11
 dir = opendir(path);
#else
 (void)path;
 other = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
 dir = other < 0 ? 0 : fdopendir(other);
 if (!dir && other >= 0) close(other);
#endif
 if (!dir) return os_error();
 empty = 1;
 while ((de = readdir(dir)) != 0)
  if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) { empty = 0; break; }
 closedir(dir);
 return empty ? ST_OK : ST_NOT_EMPTY;
}

/* All server-side creators run under the registry lock. O_EXCL and the
 * descriptor-relative host calls also avoid following replaced symlinks. */
static u32
writable_open(name, access, share, disposition, options, result, st, action)
const char *name;
u32 access, share, disposition, options;
int *result;
struct stat *st;
u32 *action;
{
 char leaf[NAME_LIMIT + 1], parent[SMBD_PATH], full[SMBD_PATH];
 char temporary[40];
 struct stat replaced;
 int dirfd, fd, mode, exists, i, rc;
 unsigned j;
 u32 status, token;
 if (!*name) {
  if ((disposition != 1 && disposition != 3) || (options & 0x1000UL)) return ST_DENIED;
  status = open_path(name, &fd);
  if (status) return status;
  if (fstat(fd, st) < 0) { status = os_error(); close(fd); return status; }
  status = state_check(st, access, share);
  if (status) { close(fd); return status; }
  *result = fd; *action = 1UL; return ST_OK;
 }
 status = parent_path(name, &dirfd, leaf);
 if (status) return status;
 strcpy(parent, resolved_path);
 status = entry_lookup(dirfd, parent, leaf, st);
 exists = !status;
 if (status && status != ST_NOT_FOUND) { close(dirfd); return status; }
 if (join_path(full, parent, leaf)) { close(dirfd); return ST_NAME_INVALID; }
 if (exists) {
  if (disposition == 2) { close(dirfd); return ST_COLLISION; }
  if (isdir(st) && disposition != 1 && disposition != 3) {
   close(dirfd); return ST_IS_DIRECTORY;
  }
  if ((options & 1UL) && !isdir(st)) { close(dirfd); return ST_NOT_DIRECTORY; }
  if ((options & 0x40UL) && isdir(st)) { close(dirfd); return ST_IS_DIRECTORY; }
  if ((disposition == 0 || disposition == 4 || disposition == 5) &&
      !(st->st_mode & 0222)) { close(dirfd); return ST_DENIED; }
  status = state_check(st, access, share);
  if (status) { close(dirfd); return status; }
 } else if (disposition == 1 || disposition == 4) {
  close(dirfd); return ST_NOT_FOUND;
 }
 if ((disposition == 4 || disposition == 5) && !(access & 2UL)) {
  close(dirfd); return ST_DENIED;
 }
 if (exists && disposition == 0 && !(access & 0x10000UL)) {
  close(dirfd); return ST_DENIED;
 }
 mode = (access & 6UL) ? ((access & 1UL) ? O_RDWR : O_WRONLY) : O_RDONLY;
 if ((exists && isdir(st)) || (!exists && (options & 1UL))) mode = O_RDONLY;
 fd = -1;
 if (!exists && (options & 1UL)) {
#ifdef PDP11
  rc = mkdir(full, 0777);
#else
  rc = mkdirat(dirfd, leaf, 0777);
#endif
  if (rc < 0) { status = os_error(); close(dirfd); return status; }
 } else if (exists && disposition == 0) {
  replaced = *st;
  /* Supersede replaces the directory entry, leaving already-open readers on
   * the old inode. Share-delete was checked before creating the replacement. */
  for (i = 0; i < 16; i++) {
   strcpy(temporary, ".smbd-"); token = (u32)(unsigned)getpid();
   for (j = 0; j < 8; j++) {
    temporary[6+j] = "0123456789abcdef"[(unsigned)((token >> (j*4)) & 15UL)];
    temporary[14+j] = "0123456789abcdef"[(unsigned)((next_serial >> (j*4)) & 15UL)];
   }
   temporary[22] = "0123456789abcdef"[i]; temporary[23] = 0;
#ifdef PDP11
   if (join_path(resolved_path, parent, temporary)) { close(dirfd); return ST_NAME_INVALID; }
   fd = open(resolved_path, mode | O_CREAT | O_EXCL, 0666);
#else
   fd = openat(dirfd, temporary, mode | O_CREAT | O_EXCL | O_NOFOLLOW, 0666);
#endif
   if (fd >= 0 || errno != EEXIST) break;
  }
  if (fd < 0) { status = os_error(); close(dirfd); return status; }
#ifdef PDP11
  rc = rename(resolved_path, full);
#else
  rc = renameat(dirfd, temporary, dirfd, leaf);
#endif
  if (rc < 0) {
   status = os_error(); close(fd);
#ifdef PDP11
   unlink(resolved_path);
#else
   unlinkat(dirfd, temporary, 0);
#endif
   close(dirfd); return status;
  }
  if (state_path_change(full, "")) { close(fd); close(dirfd); return ST_DENIED; }
  if (replaced.st_nlink) replaced.st_nlink--;
  if (metadata_unused(&replaced) < 0) {
   status = os_error(); close(fd); close(dirfd); return status;
  }
 }
 if (fd < 0) {
  if (!exists && !(options & 1UL)) mode |= O_CREAT | O_EXCL;
#ifdef PDP11
  fd = open(full, mode | O_NDELAY, 0666);
#else
  fd = openat(dirfd, leaf, mode | O_NOFOLLOW | O_NONBLOCK, 0666);
#endif
 }
 status = fd < 0 ? os_error() : ST_OK;
 close(dirfd);
 if (status) return status;
 if (fstat(fd, st) < 0) { status = os_error(); close(fd); return status; }
 if (!isregular(st) && !isdir(st)) { close(fd); return ST_DENIED; }
 if (exists && (disposition == 4 || disposition == 5)) {
  if (time_prepare(fd, st) < 0) { status = os_error(); close(fd); return status; }
  if (archive_set(fd, st, 1) < 0) { status = os_error(); close(fd); return status; }
  if (ftruncate(fd, (off_t)0) < 0) { status = os_error(); close(fd); return status; }
  if (fstat(fd, st) < 0) { status = os_error(); close(fd); return status; }
 }
 strcpy(resolved_path, full);
 *result = fd;
 *action = !exists ? 2UL : disposition == 0 ? 0UL :
           (disposition == 4 || disposition == 5) ? 3UL : 1UL;
 return ST_OK;
}

static int
archive_get(fd, st)
int fd;
struct stat *st;
{
#ifdef __linux__
 unsigned char value;
 ssize_t n;
 (void)st;
 n = fgetxattr(fd, "user.smbd.archive", &value, 1);
 if (n < 0 && errno == ENODATA) return 0;
 if (n != 1 || value > 1) { if (n >= 0) errno = EIO; return -1; }
 return value;
#else
 (void)fd;
 return (st->st_flags & SMBD_ARCHIVE) != 0;
#endif
}

static int
archive_set(fd, st, on)
int fd, on;
struct stat *st;
{
 int current;
#ifdef __linux__
 unsigned char value;
#else
#ifdef PDP11
 unsigned flags;
#else
 unsigned long flags;
#endif
#endif
 current = archive_get(fd, st);
 if (current < 0) return -1;
 if (current == on) return 0;
#ifdef __linux__
 value = on ? 1 : 0;
 if (fsetxattr(fd, "user.smbd.archive", &value, 1, 0) < 0) return -1;
#else
 flags = st->st_flags;
 if (on) flags |= SMBD_ARCHIVE; else flags &= ~SMBD_ARCHIVE;
 if (fchflags(fd, flags) < 0) return -1;
#endif
 return fstat(fd, st);
}

static u32
attributes(st, fd)
struct stat *st;
int fd;
{
 u32 attrs;
 int archive;
 archive = archive_get(fd, st);
 if (archive < 0) return MASK32;
 attrs = (isdir(st) ? 0x10UL : 0UL) | (archive ? 0x20UL : 0UL) |
         ((!cfg.writable || !(st->st_mode & 0222)) ? 1UL : 0UL);
 return attrs ? attrs : 0x80UL;
}

static u32
file_size(st)
struct stat *st;
{
 return isdir(st) ? 0UL : (u32)st->st_size;
}

static u32
allocation_size(st)
struct stat *st;
{
 u32 n;
 n = file_size(st);
 return (n + 511UL) & 0xfffffe00UL;
}

static int
time_values(fd, st, values)
int fd;
struct stat *st;
u8 *values;
{
 int result;
 result = metadata_get(fd, st, values);
 if (result < 0) return -1;
 if (result) {
  filetime(values, (long)st->st_ctime);
  filetime(values + 8, (long)st->st_ctime);
 }
 return 0;
}

static void
native_times(st, tv)
struct stat *st;
struct timeval *tv;
{
 tv[0].tv_sec = st->st_atime; tv[1].tv_sec = st->st_mtime;
#ifdef __APPLE__
 tv[0].tv_usec = st->st_atimespec.tv_nsec / 1000;
 tv[1].tv_usec = st->st_mtimespec.tv_nsec / 1000;
#else
#ifdef __linux__
 tv[0].tv_usec = st->st_atim.tv_nsec / 1000;
 tv[1].tv_usec = st->st_mtim.tv_nsec / 1000;
#else
 tv[0].tv_usec = tv[1].tv_usec = 0;
#endif
#endif
}

/* Native utimes has no descriptor form. Refuse unsupported replaced handles
 * before I/O and verify identity again before changing the pathname's times. */
static u32
restorable_times(h, st)
struct handle *h;
struct stat *st;
{
#ifdef PDP11
 struct stat actual;
 if (!h->path[0]) return ST_NOT_SUPPORTED;
 if (lstat(h->path, &actual) < 0) return os_error();
 if (actual.st_dev != st->st_dev || actual.st_ino != st->st_ino ||
     (!isregular(&actual) && !isdir(&actual))) return ST_NOT_FOUND;
#else
 (void)h; (void)st;
#endif
 return ST_OK;
}

static u32
restore_times(h, before, flags)
struct handle *h;
struct timeval *before;
int flags;
{
 struct stat st;
 struct timeval after[2];
 u32 status;
 int changed;
 if (!flags) return ST_OK;
 if (fstat(h->fd, &st) < 0) return os_error();
 status = restorable_times(h, &st); if (status) return status;
 native_times(&st, after); changed = 0;
 if ((flags & 1) && (after[0].tv_sec != before[0].tv_sec ||
                    after[0].tv_usec != before[0].tv_usec)) {
  after[0] = before[0]; changed = 1;
 }
 if ((flags & 2) && (after[1].tv_sec != before[1].tv_sec ||
                    after[1].tv_usec != before[1].tv_usec)) {
  after[1] = before[1]; changed = 1;
 }
 if (!changed) return ST_OK;
#ifdef PDP11
 if (utimes(h->path, after) < 0) return os_error();
#else
 if (futimes(h->fd, after) < 0) return os_error();
#endif
 return ST_OK;
}

static u32
begin_read_times(h, st)
struct handle *h;
struct stat *st;
{
 u32 status;
 if (!(h->user_times & 1)) return ST_OK;
 if (pending_read_times) return ST_INVALID;
 status = restorable_times(h, st); if (status) return status;
 if (state_enter()) return ST_DENIED;
 native_times(st, saved_read_times); pending_read_times = h;
 return ST_OK;
}

/* READ bytes are snapshotted by the transport after fs_dispatch returns.
 * Its extra lock reference keeps other SMB mutations out until restoration.
 * Save the current value for each operation, preserving changes made by
 * other opens between I/O operations rather than replaying the original SET. */
u32
fs_read_complete()
{
 struct handle *h;
 u32 status;
 h = pending_read_times;
 if (!h) return ST_OK;
 pending_read_times = 0;
 status = restore_times(h, saved_read_times, 1);
 state_leave();
 return status;
}

static int
time_prepare(fd, st)
int fd;
struct stat *st;
{
 u8 values[16];
 int result;
 result = metadata_get(fd, st, values);
 if (result <= 0) return result;
 if (fs_meta_fd < 0) return 0;
 filetime(values, (long)st->st_ctime); filetime(values + 8, (long)st->st_ctime);
 return metadata_set(fd, st, values);
}

static int
time_change(fd, st, fixed)
int fd, fixed;
struct stat *st;
{
 u8 values[16];
 if (fs_meta_fd < 0 || fixed) return 0;
 if (fstat(fd, st) < 0 || time_values(fd, st, values) < 0) return -1;
 filetime(values + 8, (long)time((time_t *)0));
 return metadata_set(fd, st, values);
}

static int
times(p, st, fd)
u8 *p;
struct stat *st;
int fd;
{
 u8 values[16];
 if (time_values(fd, st, values) < 0) return -1;
 memcpy(p, values, 8);
 filetime(p + 8, (long)st->st_atime);
 filetime(p + 16, (long)st->st_mtime);
 memcpy(p + 24, values + 8, 8);
 return 0;
}

/* CREATE/CLOSE place allocation before EOF; directory records reverse it. */
static int
open_info(p, st, fd)
u8 *p;
struct stat *st;
int fd;
{
 u32 attrs;
 attrs = attributes(st, fd);
 if (attrs == MASK32) return -1;
 if (times(p, st, fd) < 0) return -1;
 put64(p + 32, allocation_size(st), 0UL);
 put64(p + 40, file_size(st), 0UL);
 put32(p + 48, attrs);
 return 0;
}

static u32
create_file()
{
 u8 *b, *r, *ctx;
 unsigned off, len, i, slot, cend, pos, n, dout, previous;
 u32 disposition, options, access, share, status, co, cl, next, dl, action, attrs;
 int fd, mxac, qfid;
 struct stat st;
 struct handle *h;
 b = request + 64; r = response + 64;
 if (request_len < 120 || get16(b) != 57) return ST_INVALID;
 disposition = get32(b + 36); options = get32(b + 40);
 access = get32(b + 24); share = get32(b + 32); attrs = get32(b + 28);
 if (share & ~7UL || disposition > 5) return ST_INVALID;
 if (!cfg.writable) {
  if (access & WRITE_ACCESS) return ST_DENIED;
  if (disposition != 1 && disposition != 3) return ST_DENIED;
  if (options & 0x00001000UL) return ST_DENIED;
 }
 if (cfg.writable && disposition != 1) {
  if (attrs & ~0xb1UL) return ST_NOT_SUPPORTED;
  if ((options & 1UL) && (attrs & 1UL)) return ST_NOT_SUPPORTED;
  if (!(options & 1UL) && (attrs & 0x10UL)) return ST_NOT_SUPPORTED;
  if ((options & 0x1000UL) && (attrs & 1UL)) return ST_DENIED;
 }
 if (access & 0x010c0000UL) return ST_DENIED; /* no SACL, WRITE_DAC or WRITE_OWNER */
 if (access & 0x80000000UL) access |= READ_ACCESS;
 if (access & 0x40000000UL) access |= 0x00120116UL;
 if (access & 0x20000000UL) access |= 0x001200a0UL;
 if (access & (0x10000000UL | 0x02000000UL))
  access |= cfg.writable ? FULL_ACCESS : READ_ACCESS;
 access &= cfg.writable ? FULL_ACCESS : READ_ACCESS;
 if ((options & 0x1000UL) && !(access & 0x10000UL)) return ST_DENIED;
 if ((options & 1UL) && disposition != 1 && disposition != 2 && disposition != 3)
  return ST_INVALID;
 if (options & 0x00002000UL) return ST_NOT_SUPPORTED; /* open by file ID */
 if ((options & 0x41UL) == 0x41UL) return ST_INVALID;
 off = get16(b + 44); len = get16(b + 46);
 if (len && (off < 120 || !bounds(off, len, request_len))) return ST_INVALID;
 if (!len) input_path[0] = 0;
 else if (utf16_decode(input_path, sizeof(input_path), request + off, len))
  return ST_NAME_INVALID;
 mxac = qfid = 0;
 co = get32(b + 48); cl = get32(b + 52);
 if (cl) {
  if (co < 120 || co > request_len || cl > request_len - co || (co & 7UL))
   return ST_INVALID;
  pos = (unsigned)co; cend = (unsigned)(co + cl);
  for (i = 0; ; i++) {
   if (i >= 16 || !bounds(pos, 16, cend)) return ST_INVALID;
   ctx = request + pos; next = get32(ctx); dl = get32(ctx + 12);
   n = cend - pos;
   if (next) {
    if (next < 16 || next > n || (next & 7UL)) return ST_INVALID;
    n = (unsigned)next;
   }
   off = get16(ctx + 4); len = get16(ctx + 6); dout = get16(ctx + 10);
   if (off < 16 || !bounds(off, len, n) || dl > n ||
       (dl && (dout < 16 || !bounds(dout, (unsigned)dl, n)))) return ST_INVALID;
   if (len == 4) {
    if (!memcmp(ctx + off, "MxAc", 4)) mxac = 1;
    if (!memcmp(ctx + off, "QFid", 4)) qfid = 1;
    if (!memcmp(ctx + off, "ExtA", 4) && dl) return ST_NO_EAS;
    if (!memcmp(ctx + off, "TWrp", 4)) return ST_NOT_SUPPORTED;
   }
   if (!next) break;
   pos += (unsigned)next;
  }
 }
 slot = SMBD_HANDLES;
 for (i = 0; i < SMBD_HANDLES; i++) if (handles[i].fd < 0) { slot = i; break; }
 if (slot == SMBD_HANDLES) return ST_TOO_MANY;
 action = 1UL;
 if (cfg.writable)
  status = writable_open(input_path, access, share, disposition, options, &fd, &st, &action);
 else status = open_path(input_path, &fd);
 if (status) return status;
 if (fstat(fd, &st) < 0) { status = os_error(); close(fd); return status; }
 if ((options & 1UL) && !isdir(&st)) { close(fd); return ST_NOT_DIRECTORY; }
 if ((options & 0x40UL) && isdir(&st)) { close(fd); return ST_IS_DIRECTORY; }
 if (!isdir(&st) && (!isregular(&st) || st.st_size < 0 ||
                     (u32)st.st_size > 0x7fffffffUL)) {
  close(fd); return ST_NOT_SUPPORTED;
 }
 if (cfg.writable && (options & 0x1000UL)) {
  if (!strcmp(resolved_path, ".")) { close(fd); return ST_DENIED; }
  if (!(st.st_mode & 0222)) { close(fd); return ST_DENIED; }
  if (isdir(&st) && (status = directory_empty(fd, resolved_path)) != ST_OK) {
   close(fd); return status;
  }
 }
 if (cfg.writable && action != 1 && time_prepare(fd, &st) < 0) {
  status = os_error(); close(fd); return status;
 }
 if (cfg.writable && action != 1 && (attrs & 1UL)) {
  if (fchmod(fd, st.st_mode & 07777 & ~0222) < 0 || fstat(fd, &st) < 0) {
   status = os_error(); close(fd); return status;
  }
 }
 if (cfg.writable && action != 1 &&
     archive_set(fd, &st, !isdir(&st) || (attrs & 0x20UL) != 0) < 0) {
  status = os_error(); close(fd); return status;
 }
 if (cfg.writable && action != 1 && time_change(fd, &st, 0) < 0) {
  status = os_error(); close(fd); return status;
 }
 memset(r, 0, 88); put16(r, 89); put32(r + 4, action);
 if (open_info(r + 8, &st, fd) < 0) { status = os_error(); close(fd); return status; }
 h = &handles[slot];
 memset(h, 0, sizeof(*h)); h->fd = fd; h->state_slot = -1;
 next_serial = (next_serial + 1UL) & MASK32;
 if (!next_serial) next_serial = 1;
 h->serial = next_serial;
 h->tree = get32(request + 36);
 h->access = access;
 h->write_through = (options & 2UL) != 0;
 strcpy(h->path, resolved_path); strcpy(h->pattern, "*");
 if (cfg.writable && state_register(h, &st, share, options)) {
  close(fd); h->fd = -1; return ST_NO_MEMORY;
 }
 file_id(r + 64, h);
 memcpy(related_file, r + 64, 16);
 response_len = 152; previous = 0;
 if (mxac) {
  ctx = response + response_len; memset(ctx, 0, 32);
  put16(ctx + 4, 16); put16(ctx + 6, 4); put16(ctx + 10, 24);
  put32(ctx + 12, 8UL); memcpy(ctx + 16, "MxAc", 4);
  put32(ctx + 28, cfg.writable ? FULL_ACCESS : READ_ACCESS);
  previous = response_len; response_len += 32;
 }
 if (qfid) {
  ctx = response + response_len; memset(ctx, 0, 56);
  put16(ctx + 4, 16); put16(ctx + 6, 4); put16(ctx + 10, 24);
  put32(ctx + 12, 32UL); memcpy(ctx + 16, "QFid", 4);
  put64(ctx + 24, (u32)st.st_ino, 0UL);
  put64(ctx + 32, (u32)st.st_dev, 0UL);
  if (previous) put32(response + previous, (u32)(response_len - previous));
  response_len += 56;
 }
 if (response_len > 152) {
  put32(r + 80, 152UL); put32(r + 84, (u32)(response_len - 152));
 }
 return ST_OK;
}

static u32
close_file()
{
 struct handle *h;
 struct stat st;
 u8 *r;
 u32 status, cleanup;
 if (request_len < 88 || get16(request + 64) != 24) return ST_INVALID;
 h = find_handle(request + 72);
 if (!h) return ST_BAD_HANDLE;
 if (state_release(h)) return ST_DENIED;
 r = response + 64; memset(r, 0, 60); put16(r, 60);
 status = ST_OK;
 if ((get16(request + 66) & 1) && fstat(h->fd, &st) == 0 &&
     open_info(r + 8, &st, h->fd) == 0) put16(r + 2, 1);
 if (fstat(h->fd, &st) == 0 && metadata_unused(&st) < 0) status = os_error();
 if (h->dir) closedir(h->dir);
 close(h->fd); h->fd = -1; h->dir = 0; h->state_slot = -1;
 response_len = 124;
 cleanup = cfg.writable ? state_sweep() : ST_OK;
 if (!status) status = cleanup;
 return status;
}

static u32
read_file()
{
 struct handle *h;
 struct stat st;
 u32 count, offset, minimum, size, status;
 u8 *b, *r;
 b = request + 64; r = response + 64;
 if (request_len < 112 || get16(b) != 49) return ST_INVALID;
 h = find_handle(b + 16);
 if (!h) return ST_BAD_HANDLE;
 if (!(h->access & 1UL)) return ST_DENIED;
 count = get32(b + 4); offset = get32(b + 8); minimum = get32(b + 32);
 if (count > SMBD_TRANSFER || minimum > count || get32(b + 12) ||
     offset > 0x7fffffffUL) return ST_INVALID;
 if (get32(b + 36)) return ST_NOT_SUPPORTED;
 if (fstat(h->fd, &st) < 0) return os_error();
 if (isdir(&st)) return ST_IS_DIRECTORY;
 if (!isregular(&st) || st.st_size < 0 || (u32)st.st_size > 0x7fffffffUL)
  return ST_NOT_SUPPORTED;
 size = (u32)st.st_size;
 if (count && offset >= size) return ST_EOF;
 if (offset < size && count > size - offset) count = size - offset;
 if (count < minimum) return ST_EOF;
 if (count && (status = begin_read_times(h, &st)) != ST_OK) return status;
 memset(r, 0, 16); put16(r, 17); r[2] = 80;
 put32(r + 4, count); response_len = 80;
 read_fd = h->fd; read_offset = (long)offset; read_length = (long)count;
 return ST_OK;
}

static u32
write_file()
{
 struct handle *h;
 struct stat st;
 struct timeval before[2];
 u8 *b;
 u32 offset, count, flags, left, status, restored;
 unsigned off, n, done;
 int got;
 if (!cfg.writable) return ST_DENIED;
 b = request + 64;
 if (request_len < 112 || get16(b) != 49) return ST_INVALID;
 h = find_handle(b + 16);
 if (!h) return ST_BAD_HANDLE;
 if (!(h->access & 6UL)) return ST_DENIED;
 count = get32(b + 4); offset = get32(b + 8); off = get16(b + 2);
 flags = get32(b + 44);
 if (get32(b + 12) || offset > 0x7fffffffUL || count > SMBD_TRANSFER ||
     count > 0x7fffffffUL - offset || off < 112 || !request_file ||
     off > request_size || count > (u32)(request_size - off)) return ST_INVALID;
 if (get32(b + 32) || get16(b + 42) || (flags & ~1UL)) return ST_NOT_SUPPORTED;
 if (fstat(h->fd, &st) < 0) return os_error();
 if (!isregular(&st)) return ST_IS_DIRECTORY;
 if (!(h->access & 2UL)) {
  if (st.st_size < 0 || (u32)st.st_size > 0x7fffffffUL - count) return ST_INVALID;
  offset = (u32)st.st_size; /* append-only grants cannot overwrite existing bytes */
 }
 if (fseek(request_file, request_start + off, 0) ||
     lseek(h->fd, (off_t)offset, 0) < 0) return os_error();
 if (count && (time_prepare(h->fd, &st) < 0 || archive_set(h->fd, &st, 1) < 0))
  return os_error();
 if (count && h->user_times && (status = restorable_times(h, &st)) != ST_OK) return status;
 native_times(&st, before); status = ST_OK;
 left = count;
 while (left) {
  n = left > 2048UL ? 2048 : (unsigned)left;
  if (fread(response + 64, 1, n, request_file) != n) { status = ST_INVALID; goto finished; }
  done = 0;
  while (done < n) {
   got = write(h->fd, response + 64 + done, n - done);
   if (got < 0 && errno == EINTR) continue;
   if (got <= 0) { status = got < 0 ? os_error() : ST_DISK_FULL; goto finished; }
   done += got;
  }
  left -= n;
 }
finished:
 restored = count ? restore_times(h, before, h->user_times) : ST_OK;
 if (!status) status = restored;
 if (status) return status;
 if ((h->write_through || (flags & 1UL)) && fsync(h->fd) < 0) return os_error();
 if (count && time_change(h->fd, &st, h->user_change) < 0) return os_error();
 memset(response + 64, 0, 16); put16(response + 64, 17);
 put32(response + 68, count); response_len = 80;
 return ST_OK;
}

static u32
flush_file()
{
 struct handle *h;
 if (!cfg.writable) return ST_NOT_SUPPORTED;
 if (request_len < 88 || get16(request + 64) != 24) return ST_INVALID;
 h = find_handle(request + 72);
 if (!h) return ST_BAD_HANDLE;
 if (fsync(h->fd) < 0) return os_error();
 memset(response + 64, 0, 4); put16(response + 64, 4); response_len = 68;
 return ST_OK;
}

/* Divide a 64-bit FILETIME using 32-bit remainders; no native long long.
 * Zero means unchanged. Freeze/special negative values are unsupported. */
static int
unix_time(p, seconds)
const u8 *p;
long *seconds;
{
 u32 lo, hi, borrow, remainder, quotient, bit;
 int i;
 lo = get32(p); hi = get32(p + 4);
 if (!lo && !hi) return 1;
 if (hi < 0x019db1deUL || (hi == 0x019db1deUL && lo < 0xd53e8000UL)) return -1;
 borrow = lo < 0xd53e8000UL ? 1UL : 0UL;
 lo = (lo - 0xd53e8000UL) & MASK32; hi = (hi - 0x019db1deUL - borrow) & MASK32;
 remainder = quotient = 0;
 for (i = 63; i >= 0; i--) {
  bit = i >= 32 ? (hi >> (i - 32)) & 1UL : (lo >> i) & 1UL;
  remainder = (remainder << 1) | bit;
  if (remainder >= 10000000UL) {
   remainder -= 10000000UL;
   if (i >= 31) return -1;
   quotient |= 1UL << i;
  }
 }
 *seconds = (long)quotient;
 return 0;
}

static u32
basic_unsupported(reason)
char *reason;
{
 if (cfg.verbose) fprintf(stderr, "smbd: basic info unsupported %s\n", reason);
 return ST_NOT_SUPPORTED;
}

static u32
basic_set(h, p, len)
struct handle *h;
const u8 *p;
unsigned len;
{
 struct stat st;
 struct timeval tv[2];
 u8 values[16];
 u32 attrs;
 int a, m, mode, birth, change, changed, archive;
 if (len != 40) return ST_INVALID;
 if (!(h->access & 0x100UL)) return ST_DENIED;
 if (fstat(h->fd, &st) < 0) return os_error();
 if (time_values(h->fd, &st, values) < 0) return os_error();
 birth = (get32(p) || get32(p + 4)) != 0;
 change = (get32(p + 24) || get32(p + 28)) != 0;
 if (get32(p + 4) & 0x80000000UL) return basic_unsupported("creation time sentinel");
 if (get32(p + 28) & 0x80000000UL) return basic_unsupported("change time sentinel");
 if (fs_meta_fd < 0) {
  if (birth && memcmp(p, values, 8)) return basic_unsupported("creation time");
  if (change && memcmp(p + 24, values + 8, 8)) return basic_unsupported("change time");
 }
 tv[0].tv_sec = st.st_atime; tv[1].tv_sec = st.st_mtime;
 tv[0].tv_usec = tv[1].tv_usec = 0;
 a = unix_time(p + 8, &tv[0].tv_sec); m = unix_time(p + 16, &tv[1].tv_sec);
 if (a < 0) return basic_unsupported("access time");
 if (m < 0) return basic_unsupported("write time");
 attrs = get32(p + 32);
 mode = st.st_mode & 07777;
 changed = birth || a == 0 || m == 0;
 if (attrs) {
  if (attrs & ~0xb1UL) return basic_unsupported("attribute flags");
  if (isdir(&st)) {
   /* DOS read-only directories still permit child creation/deletion. Native
    * chmod cannot express that; only an unchanged value is supported. */
   if (((attrs & 1UL) != 0) != ((st.st_mode & 0222) == 0))
    return basic_unsupported("directory read-only attribute");
  } else {
   if (attrs & 0x10UL) return basic_unsupported("file directory attribute");
   if (attrs & 1UL) mode &= ~0222;
   else if (!(mode & 0222)) mode |= 0200;
  }
  archive = archive_get(h->fd, &st);
  if (archive < 0) return os_error();
  if (archive != ((attrs & 0x20UL) != 0) || mode != (st.st_mode & 07777)) changed = 1;
 }
 /* Validate all fields before any mutation, and preserve the original birth
  * time before native chmod/utimes can change ctime. */
 if ((changed || change) && time_prepare(h->fd, &st) < 0) return os_error();
 if (attrs && archive_set(h->fd, &st, (attrs & 0x20UL) != 0) < 0)
  return os_error();
 if ((a == 0 && tv[0].tv_sec != st.st_atime) ||
     (m == 0 && tv[1].tv_sec != st.st_mtime)) {
#ifdef PDP11
  if (!h->path[0]) return ST_NOT_FOUND;
  if (utimes(h->path, tv) < 0) return os_error();
#else
  if (futimes(h->fd, tv) < 0) return os_error();
#endif
 }
 if (mode != (st.st_mode & 07777)) {
  if (fchmod(h->fd, mode) < 0) return os_error();
 }
 if (fs_meta_fd >= 0 && (changed || change)) {
  if (birth) memcpy(values, p, 8);
  if (change) memcpy(values + 8, p + 24, 8);
  else if (!h->user_change) filetime(values + 8, (long)time((time_t *)0));
  if (metadata_set(h->fd, &st, values) < 0) return os_error();
  /* MS-FSA 2.1.5.15.2 sets UserSetChangeTime for positive values too,
   * suppressing later automatic changes from this same open handle. */
  if (change) h->user_change = 1;
 }
 if (a == 0) h->user_times |= 1;
 if (m == 0) h->user_times |= 2;
 return ST_OK;
}

static u32
rename_file(h, p, len)
struct handle *h;
const u8 *p;
unsigned len;
{
 struct stat source, target, actual;
 struct state_entry e;
 char src_leaf[NAME_LIMIT + 1], dst_leaf[NAME_LIMIT + 1], wanted[NAME_LIMIT + 1];
 char src_parent[SMBD_PATH], destination[SMBD_PATH];
 int srcfd, dstfd, exists, result;
 unsigned i, prefix;
 u32 n, status;
 if (!(h->access & 0x10000UL)) return ST_DENIED;
 if (len < 20 || p[0] > 1 || get32(p + 8) || get32(p + 12)) return ST_INVALID;
 n = get32(p + 16);
 if (n > len - 20 || utf16_decode(input_path, sizeof(input_path), p + 20, (unsigned)n))
  return ST_NAME_INVALID;
 if (h->path[0] != '.' || h->path[1] != '/') return ST_DENIED;
 if (fstat(h->fd, &source) < 0) return os_error();
 status = parent_path(h->path + 2, &srcfd, src_leaf);
 if (status) return status;
 strcpy(src_parent, resolved_path);
 status = entry_stat(srcfd, src_parent, src_leaf, &actual);
 if (status || source.st_ino != actual.st_ino || source.st_dev != actual.st_dev) {
  close(srcfd); return status ? status : ST_NOT_FOUND;
 }
 status = parent_path(input_path, &dstfd, dst_leaf);
 if (status) { close(srcfd); return status; }
 strcpy(wanted, dst_leaf);
 status = entry_lookup(dstfd, resolved_path, dst_leaf, &target);
 exists = !status;
 if (status && status != ST_NOT_FOUND) goto done;
 status = ST_OK;
 if (exists && source.st_ino == target.st_ino && source.st_dev == target.st_dev) {
  if (strcmp(src_parent, resolved_path) || !same_name(src_leaf, wanted)) {
   status = ST_COLLISION; goto done;
  }
  strcpy(dst_leaf, wanted); /* case-only rename */
  exists = 0;
 } else if (exists) {
  if (!p[0]) { status = ST_COLLISION; goto done; }
  if (isdir(&target) || isdir(&source)) { status = ST_DENIED; goto done; }
  status = state_check(&target, 0x10000UL, 7UL);
  if (status) goto done;
  if (!(target.st_mode & 0222) || strcmp(dst_leaf, wanted)) { status = ST_DENIED; goto done; }
 }
 if (join_path(destination, resolved_path, dst_leaf)) { status = ST_NAME_INVALID; goto done; }
 if (isdir(&source)) {
  prefix = strlen(h->path);
  if (!strncmp(destination, h->path, prefix) && destination[prefix] == '/') {
   status = ST_INVALID; goto done;
  }
  for (i = 0; i < state_rows; i++) {
   if (state_read(i, &e)) { status = ST_DENIED; goto done; }
   if ((e.pid || e.flags) && !strncmp(e.path, h->path, prefix) && e.path[prefix] == '/') {
    status = ST_SHARING; goto done;
   }
  }
 }
 if (time_prepare(h->fd, &source) < 0) { status = os_error(); goto done; }
#ifdef PDP11
 result = rename(h->path, destination);
#else
 result = renameat(srcfd, src_leaf, dstfd, dst_leaf);
#endif
 if (result < 0) { status = os_error(); goto done; }
 if ((exists && state_path_change(destination, "")) || state_path_change(h->path, destination)) {
  status = ST_DENIED; goto done;
 }
 strcpy(h->path, destination);
 if (time_change(h->fd, &source, h->user_change) < 0) status = os_error();
 if (exists) {
  if (target.st_nlink) target.st_nlink--;
  if (metadata_unused(&target) < 0) status = os_error();
 }
done:
 close(srcfd); close(dstfd); return status;
}

static u32
set_info()
{
 struct handle *h;
 struct stat st;
 struct timeval before[2];
 struct state_entry e;
 u8 *b, *p;
 u32 n, status, size, restored;
 unsigned off, cls;
 if (!cfg.writable) return ST_DENIED;
 b = request + 64;
 if (request_len < 96 || get16(b) != 33) return ST_INVALID;
 h = find_handle(b + 16);
 if (!h) return ST_BAD_HANDLE;
 if (b[2] != 1) return ST_NOT_SUPPORTED;
 cls = b[3]; n = get32(b + 4); off = get16(b + 8);
 if (off < 96 || n > request_len || !bounds(off, (unsigned)n, request_len)) return ST_INVALID;
 p = request + off; status = ST_OK;
 if (cls == 4) status = basic_set(h, p, (unsigned)n);
 else if (cls == 10) status = rename_file(h, p, (unsigned)n);
 else if (cls == 20) {
  if (n != 8 || get32(p + 4) || get32(p) > 0x7fffffffUL) return ST_INVALID;
  if (!(h->access & 2UL)) return ST_DENIED;
  if (fstat(h->fd, &st) < 0) return os_error();
  if (!isregular(&st)) return ST_IS_DIRECTORY;
  size = get32(p);
  if ((off_t)size != st.st_size) {
   if (time_prepare(h->fd, &st) < 0 || archive_set(h->fd, &st, 1) < 0) return os_error();
   if (h->user_times && (status = restorable_times(h, &st)) != ST_OK) return status;
   native_times(&st, before);
   if (ftruncate(h->fd, (off_t)size) < 0) status = os_error();
   restored = restore_times(h, before, h->user_times);
   if (!status) status = restored;
   if (status) return status;
   if (time_change(h->fd, &st, h->user_change) < 0) return os_error();
  }
 } else if (cls == 13) {
  if (n != 1 || p[0] > 1) return ST_INVALID;
  if (!(h->access & 0x10000UL)) return ST_DENIED;
  if (h->path[0] != '.' || h->path[1] != '/') return ST_DENIED;
  if (state_read(h->state_slot, &e)) return ST_DENIED;
  if (p[0]) {
   if (fstat(h->fd, &st) < 0) return os_error();
   if (!(st.st_mode & 0222)) return ST_DENIED;
   if (isdir(&st) && (status = directory_empty(h->fd, h->path)) != ST_OK) return status;
   e.flags |= STATE_DELETE;
  } else {
   if (e.flags & STATE_DOC) return ST_DENIED;
   e.flags &= ~STATE_DELETE;
  }
  if (state_write(h->state_slot, &e)) return ST_DENIED;
 } else return ST_NOT_SUPPORTED;
 if (status) return status;
 put16(response + 64, 2); response_len = 66;
 return ST_OK;
}

static u32
query_directory()
{
 struct handle *h;
 struct stat st;
 DIR *d;
 DIRENT *de;
 u8 *b, *p;
 unsigned cls, flags, off, len, base, limit, used, n, padded, previous;
 u32 output, attrs, status;
 long cookie;
 int initial, fd;
 b = request + 64;
 if (request_len < 96 || get16(b) != 33) return ST_INVALID;
 h = find_handle(b + 8);
 if (!h) return ST_BAD_HANDLE;
 if (!(h->access & 1UL)) return ST_DENIED;
 if (fstat(h->fd, &st) < 0) return os_error();
 if (!isdir(&st)) return ST_NOT_DIRECTORY;
 cls = b[2]; flags = b[3];
 if (cfg.verbose)
  fprintf(stderr, "smbd: directory class=%u flags=%02x name=%u:%u output=%lu\n",
          cls, flags, get16(b + 24), get16(b + 26), get32(b + 28));
 switch (cls) {
 case 1: base = 64; break;
 case 2: base = 68; break;
 case 3: base = 94; break;
 case 12: base = 12; break;
 case 37: base = 104; break;
 case 38: base = 80; break;
 default: return ST_NOT_SUPPORTED;
 }
 if (flags & ~0x17) return ST_INVALID;
 if (flags & 4) return ST_NOT_SUPPORTED; /* index-specified enumeration */
 off = get16(b + 24); len = get16(b + 26); output = get32(b + 28);
 if (len && (off < 96 || !bounds(off, len, request_len))) return ST_INVALID;
 if (len) {
  if (utf16_decode(input_path, sizeof(input_path), request + off, len) ||
      !valid_name(input_path, 1)) return ST_NAME_INVALID;
  if (!h->searched || (flags & 0x10)) strcpy(h->pattern, input_path);
 }
 if (flags & 0x10) {
  if (h->dir) closedir(h->dir);
  h->dir = 0; h->searched = 0;
 }
 if ((status = begin_read_times(h, &st)) != ST_OK) return status;
 if (!h->dir) {
#ifdef PDP11
  h->dir = opendir(h->path);
#else
  fd = openat(h->fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  h->dir = fd < 0 ? 0 : fdopendir(fd);
  if (!h->dir && fd >= 0) close(fd);
#endif
  if (!h->dir) return os_error();
 }
 d = h->dir;
 if (flags & 1) { rewinddir(d); h->searched = 0; }
 initial = !h->searched;
 limit = output > SMBD_BUFSIZE - 72 ? SMBD_BUFSIZE - 72 : (unsigned)output;
 if (limit < base) return ST_TOO_SMALL;
 used = 0; previous = 0;
 for (;;) {
  cookie = telldir(d);
  de = readdir(d);
  if (!de) break;
  if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") ||
      !valid_name(de->d_name, 0) || !matches(h->pattern, de->d_name)) continue;
#ifdef PDP11
  off = strlen(h->path); len = strlen(de->d_name);
  if (off + 1 + len >= SMBD_PATH) continue;
  strcpy(resolved_path, h->path); resolved_path[off++] = '/';
  strcpy(resolved_path + off, de->d_name);
  if (lstat(resolved_path, &st) < 0) continue;
#else
  if (fstatat(h->fd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0) continue;
#endif
  if ((!isdir(&st) && !isregular(&st)) || st.st_size < 0 ||
      (!isdir(&st) && (u32)st.st_size > 0x7fffffffUL)) continue;
  len = strlen(de->d_name) * 2; n = base + len; padded = (n + 7) & ~7;
  if (n > limit - used) {
   seekdir(d, cookie);
   if (!used) return ST_TOO_SMALL;
   break;
  }
  p = response + 72 + used;
  memset(p, 0, n);
  if (used) put32(response + 72 + previous, (u32)(used - previous));
  if (cls == 12) put32(p + 8, (u32)len);
  else {
#ifdef __linux__
   fd = openat(h->fd, de->d_name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
   if (fd < 0) return os_error();
   {
    struct stat actual;
    if (fstat(fd, &actual) < 0 || actual.st_dev != st.st_dev ||
        actual.st_ino != st.st_ino || (!isregular(&actual) && !isdir(&actual))) {
     close(fd); return ST_DENIED;
    }
   }
   attrs = attributes(&st, fd);
   if (attrs != MASK32 && times(p + 8, &st, fd) < 0) attrs = MASK32;
   close(fd);
#else
   attrs = attributes(&st, -1);
   if (attrs != MASK32 && times(p + 8, &st, -1) < 0) attrs = MASK32;
#endif
   if (attrs == MASK32) return os_error();
   put64(p + 40, file_size(&st), 0UL);
   put64(p + 48, allocation_size(&st), 0UL);
   put32(p + 56, attrs); put32(p + 60, (u32)len);
   if (cls == 37) put64(p + 96, (u32)st.st_ino, 0UL);
   if (cls == 38) put64(p + 72, (u32)st.st_ino, 0UL);
  }
  utf16_encode(p + base, de->d_name, len);
  previous = used; used += n;
  if ((flags & 2) || padded > limit - previous) break;
  memset(response + 72 + used, 0, padded - n);
  used = previous + padded;
 }
 h->searched = 1;
 if (!used) return initial ? ST_NO_FILE : ST_NO_MORE;
 memset(response + 64, 0, 8); put16(response + 64, 9);
 put16(response + 66, 72); put32(response + 68, (u32)used);
 response_len = 72 + used;
 return ST_OK;
}

static int
basic_info(p, st, fd)
u8 *p;
struct stat *st;
int fd;
{
 u32 attrs;
 attrs = attributes(st, fd);
 if (attrs == MASK32) return -1;
 if (times(p, st, fd) < 0) return -1;
 put32(p + 32, attrs);
 return 0;
}

static void
standard_info(p, st)
u8 *p;
struct stat *st;
{
 put64(p, allocation_size(st), 0UL); put64(p + 8, file_size(st), 0UL);
 put32(p + 16, (u32)st->st_nlink);
 p[20] = cfg.writable && state_check(st, 0UL, 7UL) == ST_DELETE_PENDING ? 1 : 0;
 p[21] = isdir(st) ? 1 : 0;
}

static u32
query_info()
{
 struct handle *h;
 struct stat st;
 struct statfs fs;
 u8 *b, *p;
 unsigned n, cls, type, off;
 u32 output, ilen, units, avail, blocksize, attrs;
 b = request + 64; p = response + 72;
 if (request_len < 104 || get16(b) != 41) return ST_INVALID;
 h = find_handle(b + 24);
 if (!h) return ST_BAD_HANDLE;
 off = get16(b + 8); ilen = get32(b + 12);
 if (ilen && (off < 104 || ilen > request_len ||
               !bounds(off, (unsigned)ilen, request_len))) return ST_INVALID;
 if (fstat(h->fd, &st) < 0) return os_error();
 if (isregular(&st) && (st.st_size < 0 || (u32)st.st_size > 0x7fffffffUL))
  return ST_NOT_SUPPORTED;
 type = b[2]; cls = b[3]; output = get32(b + 4);
 memset(p, 0, 512); n = 0;
 if (type == 1) {
  switch (cls) {
  case 4:
   if (basic_info(p, &st, h->fd) < 0) return os_error();
   n = 40; break;
  case 5: standard_info(p, &st); n = 24; break;
  case 6: put64(p, (u32)st.st_ino, 0UL); n = 8; break;
  case 7: n = 4; break; /* no extended attributes */
  case 8: put32(p, h->access); n = 4; break;
  case 14: n = 8; break; /* SMB byte position is always zero */
  case 16: case 17: n = 4; break;
  case 18:
   if (basic_info(p, &st, h->fd) < 0) return os_error();
   standard_info(p + 40, &st);
   put64(p + 64, (u32)st.st_ino, 0UL); put32(p + 76, h->access);
   n = 100; break; /* MS-SMB2 permits empty FileNameInformation */
  case 22:
   if (!isdir(&st)) {
    put32(p + 4, 14UL); put64(p + 8, file_size(&st), 0UL);
    put64(p + 16, allocation_size(&st), 0UL);
    utf16_encode(p + 24, "::$DATA", 14); n = 38;
   }
   break;
  case 28: put64(p, file_size(&st), 0UL); n = 16; break;
  case 34:
   if (open_info(p, &st, h->fd) < 0) return os_error();
   n = 56; break;
  case 35:
   attrs = attributes(&st, h->fd);
   if (attrs == MASK32) return os_error();
   put32(p, attrs); n = 8; break;
  case 15: return ST_NO_EAS;
  default: return ST_NOT_SUPPORTED;
  }
 } else if (type == 2) {
  switch (cls) {
  case 1:
   if (fstat(rootfd, &st) < 0) return os_error();
   filetime(p, (long)st.st_ctime); put32(p + 8, (u32)st.st_dev);
   n = utf16_encode(p + 18, cfg.share, 256);
   put32(p + 12, (u32)n); n += 18; break;
  case 3: case 7:
   if (fstatfs(h->fd, &fs) < 0) return os_error();
   blocksize = (u32)fs.f_bsize;
   if (!blocksize || blocksize > 0x7fffffffUL) return ST_NOT_SUPPORTED;
   units = (u32)fs.f_blocks; avail = fs.f_bavail > 0 ? (u32)fs.f_bavail : 0UL;
   put64(p, units, (units >> 16) >> 16);
   put64(p + 8, avail, (avail >> 16) >> 16);
   if (cls == 3) {
    put32(p + 16, 1UL); put32(p + 20, blocksize); n = 24;
   } else {
    put64(p + 16, avail, (avail >> 16) >> 16); put32(p + 24, 1UL);
    put32(p + 28, blocksize); n = 32;
   }
   break;
  case 4: put32(p, 7UL); put32(p + 4, 0x10UL); n = 8; break;
  case 5:
   put32(p, cfg.writable ? 2UL : 0x00080002UL); /* preserves filename case */
   put32(p + 4, MAXNAMLEN > NAME_LIMIT ? NAME_LIMIT : MAXNAMLEN);
   n = utf16_encode(p + 12, "2.11BSD", 32); put32(p + 8, (u32)n); n += 12;
   break;
  default: return ST_NOT_SUPPORTED;
  }
 } else return ST_NOT_SUPPORTED;
 if (output < n) return ST_TOO_SMALL;
 memset(response + 64, 0, 8); put16(response + 64, 9);
 put16(response + 66, 72); put32(response + 68, (u32)n);
 response_len = 72 + n;
 return ST_OK;
}

u32
fs_dispatch(command)
unsigned command;
{
 u32 status, restored;
 if (state_enter()) return ST_DENIED;
 if (cfg.writable && (status = state_sweep()) != ST_OK) {
  state_leave(); return status;
 }
 switch (command) {
 case 5: status = create_file(); break;
 case 6: status = close_file(); break;
 case 7: status = flush_file(); break;
 case 8: status = read_file(); break;
 case 9: status = write_file(); break;
 case 14: status = query_directory(); break;
 case 16: status = query_info(); break;
 case 17: status = set_info(); break;
 default: status = ST_NOT_SUPPORTED; break;
 }
 if (command != 8 || status != ST_OK) {
  restored = fs_read_complete();
  if (!status) status = restored;
 }
 state_leave(); return status;
}
