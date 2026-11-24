/*
 * File handles: turning a handle back into a path, the small cache that
 * makes that cheap, the few files kept open, and whose identity the
 * process currently has.
 */
#include "nfsd.h"

static struct fhent fhtab[NFH];
u32 fh_err;
int fh_nopen;
static u32 fhclock;
static char pbuf[PATHMAX + NAMEMAX + 2];

#define get16(p) (((unsigned)(p)[0] << 8) | (unsigned)(p)[1])

void
fh_init()
{
	register struct fhent *e;

	for (e = fhtab; e < &fhtab[NFH]; e++) {
		e->len = 0;
		e->fd = -1;
	}
}

/* dst = dir "/" name; -1 if the result would not fit in a path. */
int
pjoin(dst, dir, name)
	char *dst, *dir, *name;
{
	unsigned n, m;

	n = strlen(dir);
	if (n == 1 && dir[0] == '/')
		n = 0;
	m = strlen(name);
	if (n + 1 + m >= PATHMAX)
		return -1;
	if (dst != dir)
		memcpy(dst, dir, n);
	dst[n] = '/';
	memcpy(dst + n + 1, name, m + 1);
	return 0;
}

static void
fdclose(e)
	register struct fhent *e;
{
	if (e->fd >= 0) {
		(void)close(e->fd);
		e->fd = -1;
		fh_nopen--;
	}
}

static void
drop(e)
	register struct fhent *e;
{
	fdclose(e);
	if (e->len)
		free(e->path);
	e->len = 0;
}

static struct fhent *
find(fh, len)
	register u8 *fh;
	register unsigned len;
{
	register struct fhent *e;

	for (e = fhtab; e < &fhtab[NFH]; e++)
		if (e->len == len && e->fh[len - 1] == fh[len - 1] &&
		    memcmp((char *)e->fh, (char *)fh, len) == 0)
			return e;
	return (struct fhent *)0;
}

/* Remember that this handle is this path. */
struct fhent *
fh_put(fh, len, path)
	u8 *fh;
	unsigned len;
	char *path;
{
	register struct fhent *e, *old;
	char *p;

	p = malloc(strlen(path) + 1);
	if (p == 0)
		return (struct fhent *)0;
	strcpy(p, path);
	e = find(fh, len);
	if (e) {
		free(e->path);
		e->path = p;
		e->age = ++fhclock;
		return e;
	}
	old = fhtab;
	for (e = fhtab; e < &fhtab[NFH]; e++) {
		if (e->len == 0) {
			old = e;
			break;
		}
		if (e->age < old->age)
			old = e;
	}
	e = old;
	drop(e);
	e->len = len;
	memcpy((char *)e->fh, (char *)fh, len);
	e->path = p;
	e->fd = -1;
	e->off = -1;
	e->age = ++fhclock;
	return e;
}

unsigned
fh_root(ex, out)
	int ex;
	u8 *out;
{
	out[0] = FHMAGIC;
	out[1] = ex;
	out[2] = 0;
	out[3] = DIRTYPE;
	out[4] = exports[ex].rino >> 8;
	out[5] = exports[ex].rino;
	out[6] = exports[ex].rdev >> 8;
	out[7] = exports[ex].rdev;
	return FHHDR;
}

/* Handle of an entry of directory dir; 0 if it would be too deep. */
unsigned
fh_child(dir, dlen, st, out)
	u8 *dir, *out;
	unsigned dlen;
	struct stat *st;
{
	unsigned ino;

	if (dlen + 2 > FHMAX)
		return 0;
	memcpy((char *)out, (char *)dir, dlen);
	ino = INO16(st);
	out[2]++;
	out[3] = FTYPE(st->st_mode);
	out[dlen] = ino >> 8;
	out[dlen + 1] = ino;
	return dlen + 2;
}

/* Handle of the directory holding fh (the export's own, at the top). */
unsigned
fh_parent(fh, len, out)
	u8 *fh, *out;
	unsigned len;
{
	if (fh[2] == 0) {
		memcpy((char *)out, (char *)fh, len);
		return len;
	}
	memcpy((char *)out, (char *)fh, len - 2);
	out[2]--;
	out[3] = DIRTYPE;
	return len - 2;
}

/*
 * Append to path the name of its entry with inode number ino.  A mount
 * point's entry carries the covered directory's number, so when nothing
 * matches the root inode number the entries are examined one by one.
 */
static int
dirscan(path, ino)
	char *path;
	unsigned ino;
{
	struct stat sb;
	struct dent d;
	unsigned n;
	int r, pass, found;
	int pdev;

	if (lstat(path, &sb) < 0 || (sb.st_mode & S_IFMT) != S_IFDIR)
		return -1;
	pdev = sb.st_dev;
	n = strlen(path);
	if (n >= PATHMAX)
		return -1;
	if (n == 1 && path[0] == '/')
		n = 0;
	found = 0;
	for (pass = 0; pass < 2 && !found; pass++) {
		if (pass == 1 && ino != 2)
			break;
		path[n] = 0;
		if (dir_start(n ? path : "/", -1, 0L) < 0)
			return -1;
		while ((r = dir_read(&d)) > 0) {
			if (d.name[0] == '.' && (d.namlen == 1 ||
			    (d.namlen == 2 && d.name[1] == '.')))
				continue;
			if (d.namlen > NAMEMAX)
				continue;
			if (pass == 0) {
				if (((unsigned)d.ino & 0xffffU) != ino)
					continue;
				path[n] = '/';
				memcpy(path + n + 1, d.name, d.namlen);
				path[n + 1 + d.namlen] = 0;
				found = 1;
				break;
			}
			path[n] = '/';
			memcpy(path + n + 1, d.name, d.namlen);
			path[n + 1 + d.namlen] = 0;
			if (lstat(path, &sb) == 0 && INO16(&sb) == 2 &&
			    sb.st_dev != pdev) {
				found = 1;
				break;
			}
		}
		dir_end();
	}
	if (!found) {
		path[n] = 0;
		return -1;
	}
	return strlen(path) < PATHMAX ? 0 : -1;
}

/*
 * The cache entry for a handle, with the object's attributes in *st.
 * On failure returns 0 and leaves the NFS status in fh_err.
 */
struct fhent *
fh_get(fh, len, st)
	u8 *fh;
	unsigned len;
	struct stat *st;
{
	register struct fhent *e;
	struct export *ex;
	unsigned depth, want, i, j, k;
	u8 tmp[FHMAX];
	int r;

	fh_err = NFS3ERR_BADHANDLE;
	if (len < FHHDR || len > FHMAX || fh[0] != FHMAGIC ||
	    fh[1] >= nexports)
		return (struct fhent *)0;
	depth = fh[2];
	if (depth > MAXDEPTH || len != FHHDR + 2 * depth)
		return (struct fhent *)0;
	ex = &exports[fh[1]];
	fh_err = NFS3ERR_STALE;
	if (get16(fh + 4) != ex->rino || get16(fh + 6) != ex->rdev)
		return (struct fhent *)0;
	want = depth ? get16(fh + len - 2) : ex->rino;

	e = find(fh, len);
	if (e) {
		r = e->fd >= 0 ? fstat(e->fd, st) : lstat(e->path, st);
		if (r < 0 && errno == EACCES) {
			fh_err = NFS3ERR_ACCES;
			return (struct fhent *)0;
		}
		if (r == 0 && INO16(st) == want &&
		    FTYPE(st->st_mode) == fh[3]) {
			e->age = ++fhclock;
			return e;
		}
		drop(e);
	}

	/*
	 * Not cached, or no longer true: rebuild the path, starting from
	 * the nearest directory above it whose path is still known.  This
	 * is done as root; the caller's access is judged on the result.
	 */
	cred_root();
	memcpy((char *)tmp, (char *)fh, len);
	tmp[3] = DIRTYPE;
	strcpy(pbuf, ex->path);
	k = 0;
	for (j = depth ? depth - 1 : 0; j > 0; j--) {
		tmp[2] = j;
		e = find(tmp, FHHDR + 2 * j);
		if (e == 0)
			continue;
		if (lstat(e->path, st) == 0 &&
		    INO16(st) == get16(tmp + FHHDR + 2 * j - 2) &&
		    (st->st_mode & S_IFMT) == S_IFDIR) {
			strcpy(pbuf, e->path);
			k = j;
			break;
		}
		drop(e);
	}
	for (i = k; i < depth; i++)
		if (dirscan(pbuf, get16(fh + FHHDR + 2 * i)) < 0)
			break;
	if (i < depth || lstat(pbuf, st) < 0 || INO16(st) != want ||
	    FTYPE(st->st_mode) != fh[3]) {
		cred_user();
		return (struct fhent *)0;
	}
	cred_user();
	e = fh_put(fh, len, pbuf);
	if (e == 0)
		fh_err = NFS3ERR_SERVERFAULT;
	return e;
}

static void
fdroom(keep)
	struct fhent *keep;
{
	register struct fhent *e, *old;

	while (fh_nopen >= MAXFD) {
		old = 0;
		for (e = fhtab; e < &fhtab[NFH]; e++)
			if (e->len && e->fd >= 0 && e != keep &&
			    (old == 0 || e->age < old->age))
				old = e;
		if (old == 0)
			break;
		fdclose(old);
	}
}

/* Hand an already open descriptor to an entry (after CREATE). */
void
fh_setfd(e, fd, fmode)
	register struct fhent *e;
	int fd, fmode;
{
	fdclose(e);
	fdroom(e);
	e->fd = fd;
	e->fmode = fmode;
	e->off = 0;
	fh_nopen++;
}

/*
 * A descriptor for the entry's file, open for reading or for writing.
 * As on other NFS servers the owner may always read and write his own
 * file, whatever its mode: a client checks the mode when it opens the
 * file, and expects I/O on a file it created read-only to keep working.
 */
int
fh_fd(e, wr, st)
	register struct fhent *e;
	int wr;
	struct stat *st;
{
	int fd, m, need, err;

	need = wr ? 2 : 1;
	if (e->fd >= 0 && (e->fmode & need))
		return e->fd;
	fdclose(e);
	fdroom(e);
	m = wr ? 3 : 1;
	fd = open(e->path, wr ? O_RDWR : O_RDONLY, 0);
	if (fd < 0 && wr && errno == EACCES) {
		m = 2;
		fd = open(e->path, O_WRONLY, 0);
	}
	if (fd < 0 && errno == EACCES && st && cred_uid() != 0 &&
	    (unsigned)st->st_uid == cred_uid()) {
		cred_root();
		m = wr ? 3 : 1;
		fd = open(e->path, wr ? O_RDWR : O_RDONLY, 0);
		err = errno;
		cred_user();
		errno = err;
	}
	if (fd < 0)
		return -1;
	e->fd = fd;
	e->fmode = m;
	e->off = 0;
	fh_nopen++;
	return fd;
}

void
fh_closeall()
{
	register struct fhent *e;

	if (fh_nopen == 0)
		return;
	for (e = fhtab; e < &fhtab[NFH]; e++)
		if (e->len)
			fdclose(e);
}

/* The object at path is gone: forget every handle cached for it. */
void
fh_droppath(path)
	char *path;
{
	register struct fhent *e;

	for (e = fhtab; e < &fhtab[NFH]; e++)
		if (e->len && strcmp(e->path, path) == 0)
			drop(e);
}

/*
 * from was renamed to to.  Handles cached for it, and for anything
 * below it, stay good: only their paths change.
 */
void
fh_rename(from, to)
	char *from, *to;
{
	register struct fhent *e;
	unsigned n;
	char *p;

	n = strlen(from);
	for (e = fhtab; e < &fhtab[NFH]; e++) {
		if (e->len == 0 || strncmp(e->path, from, n) != 0)
			continue;
		if (e->path[n] != 0 && e->path[n] != '/')
			continue;
		p = malloc(strlen(to) + strlen(e->path + n) + 1);
		if (p == 0 || strlen(to) + strlen(e->path + n) >= PATHMAX) {
			if (p)
				free(p);
			drop(e);
			continue;
		}
		strcpy(p, to);
		strcat(p, e->path + n);
		free(e->path);
		e->path = p;
	}
}

/*
 * Identity.  The server stays root in its real uid and takes on the
 * caller's effective uid and groups, so the kernel does the permission
 * checks and new files get the right owner.
 */
static int isroot;
static unsigned cuid, cgid;
static int cng;
static unsigned cgrp[16];

void
cred_init()
{
	isroot = geteuid() == 0;
	cuid = cgid = 0;
	cng = -1;
}

void
cred_become(uid, gid, ng, grp)
	unsigned uid, gid;
	int ng;
	u32 *grp;
{
	gid_t g[17];
	int i, same;

	if (ng > 15)
		ng = 15;
	same = uid == cuid && gid == cgid && ng == cng;
	for (i = 0; same && i < ng; i++)
		if ((unsigned)grp[i] != cgrp[i])
			same = 0;
	if (same)
		return;
	/* descriptors were opened with the old identity's rights */
	fh_closeall();
	cuid = uid;
	cgid = gid;
	cng = ng;
	g[0] = gid;
	for (i = 0; i < ng; i++) {
		cgrp[i] = (unsigned)grp[i];
		g[i + 1] = cgrp[i];
	}
	if (!isroot)
		return;
	(void)seteuid(0);
	(void)setgroups(ng + 1, g);
	(void)setegid(gid);
	if (uid)
		(void)seteuid(uid);
}

void
cred_set(ex)
	struct export *ex;
{
	if (ex->flags & EX_MAPALL)
		cred_become(ex->mapuid, ex->mapgid, 0, (u32 *)0);
	else if (rq.uid > 65535UL || rq.gid > 65535UL ||
	    (rq.uid == 0 && !(ex->flags & EX_ROOT)))
		cred_become(NOBODY, NOBODY, 0, (u32 *)0);
	else
		cred_become((unsigned)rq.uid, (unsigned)rq.gid, rq.ngrp,
		    rq.grp);
}

void
cred_root()
{
	if (isroot && cuid)
		(void)seteuid(0);
}

void
cred_user()
{
	if (isroot && cuid)
		(void)seteuid(cuid);
}

unsigned
cred_uid()
{
	return cuid;
}

unsigned
cred_gid()
{
	return cgid;
}

int
cred_ingroup(gid)
	unsigned gid;
{
	int i;

	if (gid == cgid)
		return 1;
	for (i = 0; i < cng; i++)
		if (cgrp[i] == gid)
			return 1;
	return 0;
}
