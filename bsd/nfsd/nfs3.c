/* The NFS version 3 procedures. */
#include "nfsd.h"
#include <syslog.h>
#ifdef PDP11
#include <sys/mount.h>
#else
#include <sys/statvfs.h>
#endif

#define SA_MODE 1
#define SA_UID 2
#define SA_GID 4
#define SA_SIZE 8
#define SA_ATIME 16
#define SA_MTIME 32
#define SA_ATIME_NOW 64
#define SA_MTIME_NOW 128

struct sattr {
	int mask;
	u32 mode, uid, gid, atime, mtime;
	struct u64 size;
};

#define ZERO ((u32)0)
#define ONE ((u32)1)
#define ISDIR(sp) (((sp)->st_mode & S_IFMT) == S_IFDIR)
#define ISREG(sp) (((sp)->st_mode & S_IFMT) == S_IFREG)
#define RDONLY (!(cex->flags & EX_RW))

static struct stat st, st2, st3;
static char path[PATHMAX + NAMEMAX + 2], path2[PATHMAX + NAMEMAX + 2];
static char name[NAMEMAX + 2], name2[NAMEMAX + 2];
static struct export *cex;	/* export of the handle last decoded */
static u8 *afh;			/* that handle, in the request */
static unsigned afhlen;
static u8 cfh[FHMAX];
static int rmdirlies;		/* see p_remove */
u8 wverf[8];

u32
nfserr(e)
	int e;
{
	switch (e) {
	case EPERM:	return NFS3ERR_PERM;
	case ENOENT:	return NFS3ERR_NOENT;
	case EACCES:	return NFS3ERR_ACCES;
	case EEXIST:	return NFS3ERR_EXIST;
	case EXDEV:	return NFS3ERR_XDEV;
	case ENOTDIR:	return NFS3ERR_NOTDIR;
	case EISDIR:	return NFS3ERR_ISDIR;
	case EINVAL:	return NFS3ERR_INVAL;
	case EFBIG:	return NFS3ERR_FBIG;
	case ENOSPC:	return NFS3ERR_NOSPC;
	case EROFS:	return NFS3ERR_ROFS;
	case EMLINK:	return NFS3ERR_MLINK;
	case ENAMETOOLONG: return NFS3ERR_NAMETOOLONG;
	case ENOTEMPTY:	return NFS3ERR_NOTEMPTY;
#ifdef EDQUOT
	case EDQUOT:	return NFS3ERR_DQUOT;
#endif
#ifdef ESTALE
	case ESTALE:	return NFS3ERR_STALE;
#endif
	}
	return NFS3ERR_IO;
}

static void
putattr(r, sp)
	register struct xbuf *r;
	register struct stat *sp;
{
	u32 t;

	switch (sp->st_mode & S_IFMT) {
	case S_IFDIR:	t = 2; break;
	case S_IFBLK:	t = 3; break;
	case S_IFCHR:	t = 4; break;
	case S_IFLNK:	t = 5; break;
#ifdef S_IFSOCK
	case S_IFSOCK:	t = 6; break;
#endif
#ifdef S_IFIFO
	case S_IFIFO:	t = 7; break;
#endif
	default:	t = 1;
	}
	xput32(r, t);
	xput32(r, (u32)(sp->st_mode & 07777));
	xput32(r, (u32)(unsigned)sp->st_nlink);
	xput32(r, (u32)(unsigned)sp->st_uid);
	xput32(r, (u32)(unsigned)sp->st_gid);
	xput32(r, ZERO);
	xput32(r, (u32)sp->st_size);
	xput32(r, ZERO);
#ifdef PDP11
	xput32(r, (u32)sp->st_blocks << 10);	/* counted in kilobytes */
#else
	xput32(r, (u32)sp->st_blocks << 9);
#endif
	if (t == 3 || t == 4) {
		xput32(r, (u32)(((unsigned)sp->st_rdev >> 8) & 0377));
		xput32(r, (u32)((unsigned)sp->st_rdev & 0377));
	} else {
		xput32(r, ZERO);
		xput32(r, ZERO);
	}
	xput32(r, ZERO);
	xput32(r, (u32)DEV16(sp));
	xput32(r, ZERO);
	xput32(r, (u32)sp->st_ino);
	xput32(r, (u32)sp->st_atime);
	xput32(r, ZERO);
	xput32(r, (u32)sp->st_mtime);
	xput32(r, ZERO);
	xput32(r, (u32)sp->st_ctime);
	xput32(r, ZERO);
}

static void
postattr(r, sp)
	struct xbuf *r;
	struct stat *sp;
{
	if (sp == 0) {
		xput32(r, ZERO);
		return;
	}
	xput32(r, ONE);
	putattr(r, sp);
}

/* wcc_data: what the object looked like before and after. */
static void
wcc(r, pre, post)
	struct xbuf *r;
	struct stat *pre, *post;
{
	if (pre == 0)
		xput32(r, ZERO);
	else {
		xput32(r, ONE);
		xput32(r, ZERO);
		xput32(r, (u32)pre->st_size);
		xput32(r, (u32)pre->st_mtime);
		xput32(r, ZERO);
		xput32(r, (u32)pre->st_ctime);
		xput32(r, ZERO);
	}
	postattr(r, post);
}

/* A failure reply: the status and n absent optional parts. */
static int
fail(r, p0, s, n)
	struct xbuf *r;
	unsigned p0;
	u32 s;
	int n;
{
	r->len = p0;
	r->err = 0;
	xput32(r, s);
	while (n-- > 0)
		xput32(r, ZERO);
	return RPC_SUCCESS;
}

/* A failure reply that still carries the object's attributes. */
static int
failattr(r, p0, s, sp)
	struct xbuf *r;
	unsigned p0;
	u32 s;
	struct stat *sp;
{
	r->len = p0;
	r->err = 0;
	xput32(r, s);
	postattr(r, sp);
	return RPC_SUCCESS;
}

/*
 * Decode a file handle argument, check that this client may use its
 * export, take on the caller's identity and find the object.
 */
static struct fhent *
argfh(a, sp, sv)
	struct xbuf *a;
	struct stat *sp;
	u32 *sv;
{
	struct fhent *e;

	afh = xgetref(a, &afhlen);
	*sv = NFS3ERR_BADHANDLE;
	if (a->err || afhlen < FHHDR || afh[0] != FHMAGIC ||
	    afh[1] >= nexports)
		return (struct fhent *)0;
	cex = &exports[afh[1]];
	if (!ex_allowed(cex)) {
		*sv = NFS3ERR_ACCES;
		return (struct fhent *)0;
	}
	cred_set(cex);
	e = fh_get(afh, afhlen, sp);
	if (e == 0)
		*sv = fh_err;
	return e;
}

/* A file name argument; returns 0 or the NFS status that rejects it. */
static u32
argname(a, buf)
	struct xbuf *a;
	char *buf;
{
	int n;

	n = xgetstr(a, buf, NAMEMAX + 1);
	if (n < 0)
		return NFS3ERR_NAMETOOLONG;
	if (n == 0 || strlen(buf) != n || strchr(buf, '/') != 0)
		return NFS3ERR_INVAL;
	return NFS3_OK;
}

static int
isdot(s)
	char *s;
{
	return s[0] == '.' && (s[1] == 0 || (s[1] == '.' && s[2] == 0));
}

static void
getsattr(a, sa)
	struct xbuf *a;
	struct sattr *sa;
{
	u32 how;

	sa->mask = 0;
	if (xget32(a)) {
		sa->mode = xget32(a);
		sa->mask |= SA_MODE;
	}
	if (xget32(a)) {
		sa->uid = xget32(a);
		sa->mask |= SA_UID;
	}
	if (xget32(a)) {
		sa->gid = xget32(a);
		sa->mask |= SA_GID;
	}
	if (xget32(a)) {
		xget64(a, &sa->size);
		sa->mask |= SA_SIZE;
	}
	how = xget32(a);
	if (how == 1)
		sa->mask |= SA_ATIME_NOW;
	else if (how == 2) {
		sa->atime = xget32(a);
		(void)xget32(a);
		sa->mask |= SA_ATIME;
	}
	how = xget32(a);
	if (how == 1)
		sa->mask |= SA_MTIME_NOW;
	else if (how == 2) {
		sa->mtime = xget32(a);
		(void)xget32(a);
		sa->mask |= SA_MTIME;
	}
}

/* Apply the attributes in sa to the object at p, now described by sp. */
static u32
setattrs(p, sa, sp)
	char *p;
	register struct sattr *sa;
	struct stat *sp;
{
	struct timeval tv[2];
	time_t now;
	int u, g, r, err;

	if ((sp->st_mode & S_IFMT) == S_IFLNK)
		return NFS3_OK;
	if (sa->mask & SA_SIZE) {
		if (ISDIR(sp))
			return NFS3ERR_ISDIR;
		if (sa->size.hi || (sa->size.lo & 0x80000000UL))
			return NFS3ERR_FBIG;
		if ((long)sa->size.lo != (long)sp->st_size) {
			r = truncate(p, (off_t)sa->size.lo);
			if (r < 0 && errno == EACCES && cred_uid() != 0 &&
			    (unsigned)sp->st_uid == cred_uid()) {
				cred_root();
				r = truncate(p, (off_t)sa->size.lo);
				err = errno;
				cred_user();
				errno = err;
			}
			if (r < 0)
				return nfserr(errno);
		}
	}
	if (sa->mask & SA_MODE)
		if (chmod(p, (int)(sa->mode & 07777)) < 0)
			return nfserr(errno);
	if ((sa->mask & (SA_UID | SA_GID)) && !(cex->flags & EX_MAPALL)) {
		u = g = -1;
		if ((sa->mask & SA_UID) &&
		    (unsigned)sa->uid != (unsigned)sp->st_uid)
			u = (int)sa->uid;
		if ((sa->mask & SA_GID) &&
		    (unsigned)sa->gid != (unsigned)sp->st_gid)
			g = (int)sa->gid;
		if ((u != -1 || g != -1) && chown(p, u, g) < 0)
			return nfserr(errno);
	}
	if (sa->mask & (SA_ATIME | SA_MTIME | SA_ATIME_NOW | SA_MTIME_NOW)) {
		(void)time(&now);
		tv[0].tv_sec = sp->st_atime;
		tv[1].tv_sec = sp->st_mtime;
		tv[0].tv_usec = tv[1].tv_usec = 0;
		if (sa->mask & SA_ATIME)
			tv[0].tv_sec = (long)sa->atime;
		else if (sa->mask & SA_ATIME_NOW)
			tv[0].tv_sec = now;
		if (sa->mask & SA_MTIME)
			tv[1].tv_sec = (long)sa->mtime;
		else if (sa->mask & SA_MTIME_NOW)
			tv[1].tv_sec = now;
		if (utimes(p, tv) < 0)
			return nfserr(errno);
	}
	return NFS3_OK;
}

static int
p_getattr(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	u32 s;

	e = argfh(a, &st, &s);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, r->len, s, 0);
	xput32(r, NFS3_OK);
	putattr(r, &st);
	return RPC_SUCCESS;
}

static int
p_setattr(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	struct sattr sa;
	u32 s, guard, gsec;
	unsigned p0;

	p0 = r->len;
	e = argfh(a, &st, &s);
	getsattr(a, &sa);
	guard = xget32(a);
	gsec = 0;
	if (guard) {
		gsec = xget32(a);
		(void)xget32(a);
	}
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 2);
	if (RDONLY)
		return fail(r, p0, NFS3ERR_ROFS, 2);
	if (guard && ((u32)st.st_ctime & M32) != gsec)
		return fail(r, p0, NFS3ERR_NOT_SYNC, 2);
	s = setattrs(e->path, &sa, &st);
	if (lstat(e->path, &st2) < 0)
		return fail(r, p0, s ? s : nfserr(errno), 2);
	xput32(r, s);
	wcc(r, &st, &st2);
	return RPC_SUCCESS;
}

static int
p_lookup(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	u32 s, ns;
	unsigned p0, clen;

	p0 = r->len;
	e = argfh(a, &st, &s);
	ns = argname(a, name);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 1);
	if (ns)
		return failattr(r, p0, ns, &st);
	if (!ISDIR(&st))
		return failattr(r, p0, NFS3ERR_NOTDIR, &st);
	if (name[0] == '.' && name[1] == 0) {
		clen = afhlen;
		memcpy((char *)cfh, (char *)afh, clen);
		st2 = st;
	} else if (isdot(name)) {
		clen = fh_parent(afh, afhlen, cfh);
		if (fh_get(cfh, clen, &st2) == 0)
			return failattr(r, p0, fh_err, &st);
	} else {
		if (pjoin(path, e->path, name) < 0)
			return failattr(r, p0, NFS3ERR_NAMETOOLONG, &st);
		if (lstat(path, &st2) < 0)
			return failattr(r, p0, nfserr(errno), &st);
		clen = fh_child(afh, afhlen, &st2, cfh);
		if (clen == 0)
			return failattr(r, p0, NFS3ERR_NAMETOOLONG, &st);
		if (fh_put(cfh, clen, path) == 0)
			return failattr(r, p0, NFS3ERR_SERVERFAULT, &st);
	}
	xput32(r, NFS3_OK);
	xputopaque(r, cfh, clen);
	postattr(r, &st2);
	postattr(r, &st);
	return RPC_SUCCESS;
}

static int
p_access(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	u32 s, want, have;
	unsigned bits, p0;

	p0 = r->len;
	e = argfh(a, &st, &s);
	want = xget32(a);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 1);
	if (cred_uid() == 0)
		bits = 7;
	else if ((unsigned)st.st_uid == cred_uid())
		bits = (st.st_mode >> 6) & 7;
	else if (cred_ingroup((unsigned)st.st_gid))
		bits = (st.st_mode >> 3) & 7;
	else
		bits = st.st_mode & 7;
	have = 0;
	if (bits & 4)
		have |= 1;			/* READ */
	if (ISDIR(&st)) {
		if (bits & 1)
			have |= 2;		/* LOOKUP */
		if (bits & 2)
			have |= 4 | 8 | 16;	/* MODIFY EXTEND DELETE */
	} else {
		if (bits & 2)
			have |= 4 | 8;
		if (bits & 1)
			have |= 32;		/* EXECUTE */
	}
	if (RDONLY)
		have &= ~(u32)(4 | 8 | 16);
	xput32(r, NFS3_OK);
	postattr(r, &st);
	xput32(r, have & want);
	return RPC_SUCCESS;
}

static int
p_readlink(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	u32 s;
	unsigned p0;
	int n;

	p0 = r->len;
	e = argfh(a, &st, &s);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 1);
	if ((st.st_mode & S_IFMT) != S_IFLNK)
		return failattr(r, p0, NFS3ERR_INVAL, &st);
	n = readlink(e->path, path, PATHMAX);
	if (n < 0)
		return failattr(r, p0, nfserr(errno), &st);
	xput32(r, NFS3_OK);
	postattr(r, &st);
	xputopaque(r, (u8 *)path, (unsigned)n);
	return RPC_SUCCESS;
}

#define ATTRLEN 84	/* an encoded fattr3 */

static int
p_read(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	struct u64 off;
	u32 s, cnt;
	unsigned p0, room;
	int fd, n, eof;

	p0 = r->len;
	e = argfh(a, &st, &s);
	xget64(a, &off);
	cnt = xget32(a);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 1);
	if (ISDIR(&st))
		return failattr(r, p0, NFS3ERR_ISDIR, &st);
	if (!ISREG(&st))
		return failattr(r, p0, NFS3ERR_INVAL, &st);
	room = r->cap - p0 - (ATTRLEN + 24);
	if (cnt > IOSIZE)
		cnt = IOSIZE;
	if (cnt > room)
		cnt = room;
	n = 0;
	if (off.hi == 0 && !(off.lo & 0x80000000UL) && cnt != 0) {
		fd = fh_fd(e, 0, &st);
		if (fd < 0)
			return failattr(r, p0, nfserr(errno), &st);
		if (e->off != (long)off.lo &&
		    lseek(fd, (off_t)off.lo, 0) < 0) {
			e->off = -1;
			return failattr(r, p0, nfserr(errno), &st);
		}
		/* straight into the reply, after the fields that precede it */
		n = read(fd, (char *)r->base + p0 + ATTRLEN + 20,
		    (unsigned)cnt);
		if (n < 0) {
			e->off = -1;
			return failattr(r, p0, nfserr(errno), &st);
		}
		e->off = (long)off.lo + n;
	}
	eof = off.hi != 0 || (off.lo & 0x80000000UL) ||
	    (long)off.lo + n >= (long)st.st_size;
	xput32(r, NFS3_OK);
	postattr(r, &st);
	xput32(r, (u32)n);
	xput32(r, (u32)eof);
	xput32(r, (u32)n);
	r->len += n;
	while (r->len & 3)
		r->base[r->len++] = 0;
	return RPC_SUCCESS;
}

static int
p_write(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	struct u64 off;
	u32 s, cnt, stable;
	u8 *data;
	unsigned p0, dlen;
	int fd, n;

	p0 = r->len;
	e = argfh(a, &st, &s);
	xget64(a, &off);
	cnt = xget32(a);
	stable = xget32(a);
	data = xgetref(a, &dlen);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 2);
	if (RDONLY)
		return fail(r, p0, NFS3ERR_ROFS, 2);
	if (!ISREG(&st))
		return fail(r, p0, ISDIR(&st) ? NFS3ERR_ISDIR :
		    NFS3ERR_INVAL, 2);
	if (cnt < dlen)
		dlen = (unsigned)cnt;
	if (off.hi || (off.lo & 0x80000000UL) ||
	    ((off.lo + dlen) & 0x80000000UL))
		return fail(r, p0, NFS3ERR_FBIG, 2);
	fd = fh_fd(e, 1, &st);
	if (fd < 0)
		return fail(r, p0, nfserr(errno), 2);
	if (e->off != (long)off.lo && lseek(fd, (off_t)off.lo, 0) < 0) {
		e->off = -1;
		return fail(r, p0, nfserr(errno), 2);
	}
	n = write(fd, (char *)data, dlen);
	if (n < 0) {
		e->off = -1;
		return fail(r, p0, nfserr(errno), 2);
	}
	e->off = (long)off.lo + n;
	if (stable)
		(void)fsync(fd);
	if (fstat(fd, &st2) < 0)
		return fail(r, p0, nfserr(errno), 2);
	xput32(r, NFS3_OK);
	wcc(r, &st, &st2);
	xput32(r, (u32)n);
	xput32(r, stable ? (u32)2 : ZERO);	/* FILE_SYNC or UNSTABLE */
	xputfixed(r, wverf, 8);
	return RPC_SUCCESS;
}

/* The reply shared by CREATE, MKDIR and SYMLINK once path exists. */
static int
created(r, p0, e, fd)
	struct xbuf *r;
	unsigned p0;
	struct fhent *e;
	int fd;
{
	struct fhent *c;
	unsigned clen;

	if (lstat(path, &st2) < 0) {
		if (fd >= 0)
			(void)close(fd);
		return fail(r, p0, nfserr(errno), 2);
	}
	clen = fh_child(afh, afhlen, &st2, cfh);
	c = clen ? fh_put(cfh, clen, path) : (struct fhent *)0;
	if (fd >= 0) {
		/* keep it open: the data usually follows at once */
		if (c)
			fh_setfd(c, fd, 3);
		else
			(void)close(fd);
	}
	xput32(r, NFS3_OK);
	if (clen) {
		xput32(r, ONE);
		xputopaque(r, cfh, clen);
	} else
		xput32(r, ZERO);
	postattr(r, &st2);
	wcc(r, &st, lstat(e->path, &st3) < 0 ? (struct stat *)0 : &st3);
	return RPC_SUCCESS;
}

static int
p_create(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	struct sattr sa;
	struct timeval tv[2];
	u8 verf[8];
	u32 s, ns, how;
	unsigned p0;
	int fd, mode, exists;

	p0 = r->len;
	e = argfh(a, &st, &s);
	ns = argname(a, name);
	how = xget32(a);
	sa.mask = 0;
	if (how == 2)
		xgetfixed(a, verf, 8);
	else
		getsattr(a, &sa);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 2);
	if (RDONLY)
		return fail(r, p0, NFS3ERR_ROFS, 2);
	if (ns)
		return fail(r, p0, ns, 2);
	if (!ISDIR(&st))
		return fail(r, p0, NFS3ERR_NOTDIR, 2);
	if (isdot(name))
		return fail(r, p0, NFS3ERR_EXIST, 2);
	if (afhlen + 2 > FHMAX || pjoin(path, e->path, name) < 0)
		return fail(r, p0, NFS3ERR_NAMETOOLONG, 2);
	exists = lstat(path, &st2) == 0;
	mode = (sa.mask & SA_MODE) ? (int)(sa.mode & 07777) : 0644;
	fd = -1;
	if (how == 2) {
		/*
		 * Exclusive: the client's verifier is kept in the new
		 * file's times, so a repeated request can be told from
		 * a file that was already there.
		 */
		tv[0].tv_sec = (long)(getbe32(verf) & 0x7fffffffUL);
		tv[1].tv_sec = (long)(getbe32(verf + 4) & 0x7fffffffUL);
		tv[0].tv_usec = tv[1].tv_usec = 0;
		if (exists) {
			if (st2.st_atime != tv[0].tv_sec ||
			    st2.st_mtime != tv[1].tv_sec)
				return fail(r, p0, NFS3ERR_EXIST, 2);
		} else {
			fd = open(path, O_RDWR | O_CREAT | O_EXCL, mode);
			if (fd < 0)
				return fail(r, p0, nfserr(errno), 2);
			(void)utimes(path, tv);
		}
	} else if (exists) {
		if (how == 1)
			return fail(r, p0, NFS3ERR_EXIST, 2);
		if (ISDIR(&st2))
			return fail(r, p0, NFS3ERR_ISDIR, 2);
		/* unchecked, and it is there: only the size applies */
		sa.mask &= SA_SIZE;
		s = setattrs(path, &sa, &st2);
		if (s)
			return fail(r, p0, s, 2);
	} else {
		fd = open(path, O_RDWR | O_CREAT | O_EXCL, mode);
		if (fd < 0)
			return fail(r, p0, nfserr(errno), 2);
		if (cred_uid() == 0 && (sa.mask & (SA_UID | SA_GID)))
			(void)chown(path,
			    (sa.mask & SA_UID) ? (int)sa.uid : -1,
			    (sa.mask & SA_GID) ? (int)sa.gid : -1);
	}
	return created(r, p0, e, fd);
}

static int
p_mkdir(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	struct sattr sa;
	u32 s, ns;
	unsigned p0;

	p0 = r->len;
	e = argfh(a, &st, &s);
	ns = argname(a, name);
	getsattr(a, &sa);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 2);
	if (RDONLY)
		return fail(r, p0, NFS3ERR_ROFS, 2);
	if (ns)
		return fail(r, p0, ns, 2);
	if (!ISDIR(&st))
		return fail(r, p0, NFS3ERR_NOTDIR, 2);
	if (isdot(name))
		return fail(r, p0, NFS3ERR_EXIST, 2);
	if (afhlen + 2 > FHMAX || pjoin(path, e->path, name) < 0)
		return fail(r, p0, NFS3ERR_NAMETOOLONG, 2);
	if (mkdir(path, (sa.mask & SA_MODE) ? (int)(sa.mode & 07777) :
	    0755) < 0)
		return fail(r, p0, nfserr(errno), 2);
	return created(r, p0, e, -1);
}

static int
p_symlink(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	struct sattr sa;
	u32 s, ns;
	unsigned p0;
	int n;

	p0 = r->len;
	e = argfh(a, &st, &s);
	ns = argname(a, name);
	getsattr(a, &sa);
	n = xgetstr(a, path2, PATHMAX);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 2);
	if (RDONLY)
		return fail(r, p0, NFS3ERR_ROFS, 2);
	if (ns)
		return fail(r, p0, ns, 2);
	if (n < 0)
		return fail(r, p0, NFS3ERR_NAMETOOLONG, 2);
	if (!ISDIR(&st))
		return fail(r, p0, NFS3ERR_NOTDIR, 2);
	if (isdot(name))
		return fail(r, p0, NFS3ERR_EXIST, 2);
	if (afhlen + 2 > FHMAX || pjoin(path, e->path, name) < 0)
		return fail(r, p0, NFS3ERR_NAMETOOLONG, 2);
	if (symlink(path2, path) < 0)
		return fail(r, p0, nfserr(errno), 2);
	return created(r, p0, e, -1);
}

static int
p_remove(a, r, dir)
	struct xbuf *a, *r;
	int dir;
{
	struct fhent *e;
	u32 s, ns;
	unsigned p0;
	int n;

	p0 = r->len;
	e = argfh(a, &st, &s);
	ns = argname(a, name);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 2);
	if (RDONLY)
		return fail(r, p0, NFS3ERR_ROFS, 2);
	if (ns)
		return fail(r, p0, ns, 2);
	if (!ISDIR(&st))
		return fail(r, p0, NFS3ERR_NOTDIR, 2);
	if (isdot(name))
		return fail(r, p0, NFS3ERR_INVAL, 2);
	if (pjoin(path, e->path, name) < 0)
		return fail(r, p0, NFS3ERR_NAMETOOLONG, 2);
	if (lstat(path, &st2) < 0)
		return fail(r, p0, nfserr(errno), 2);
	/* unlink(2) would let root unlink a directory */
	if (!dir && ISDIR(&st2))
		return fail(r, p0, NFS3ERR_ISDIR, 2);
	if (dir && !ISDIR(&st2))
		return fail(r, p0, NFS3ERR_NOTDIR, 2);
	fh_droppath(path);
	n = dir ? rmdir(path) : unlink(path);
	/*
	 * On 2.11BSD rmdir(2) of a directory that is not empty has been
	 * seen to return 0, about once in 200 calls, having done nothing.
	 * So believe a successful rmdir only when the directory is gone.
	 */
	if (dir && n == 0 && lstat(path, &st3) == 0 &&
	    st3.st_ino == st2.st_ino) {
		if (++rmdirlies <= 3)
			syslog(LOG_NOTICE,
			    "rmdir %s returned 0 but did nothing", path);
		n = rmdir(path);
		if (n == 0 && lstat(path, &st3) == 0 &&
		    st3.st_ino == st2.st_ino) {
			n = -1;
			errno = ENOTEMPTY;
		}
	}
	if (debug > 1)
		fprintf(stderr, "%s %s = %d\n", dir ? "rmdir" : "unlink", path,
		    n);
	if (n < 0)
		return fail(r, p0, nfserr(errno), 2);
	xput32(r, NFS3_OK);
	wcc(r, &st, lstat(e->path, &st3) < 0 ? (struct stat *)0 : &st3);
	return RPC_SUCCESS;
}

static int
p_rename(a, r)
	struct xbuf *a, *r;
{
	static char d1[PATHMAX], d2[PATHMAX];
	static struct stat sta;
	struct fhent *e;
	u32 s, s2, ns, ns2;
	unsigned p0;

	p0 = r->len;
	d1[0] = d2[0] = 0;
	e = argfh(a, &st, &s);
	ns = argname(a, name);
	if (e)
		strcpy(d1, e->path);
	e = argfh(a, &sta, &s2);
	ns2 = argname(a, name2);
	if (e)
		strcpy(d2, e->path);
	if (a->err)
		return RPC_GARBAGE;
	if (d1[0] == 0)
		return fail(r, p0, s, 4);
	if (d2[0] == 0)
		return fail(r, p0, s2, 4);
	if (RDONLY)
		return fail(r, p0, NFS3ERR_ROFS, 4);
	if (ns || ns2)
		return fail(r, p0, ns ? ns : ns2, 4);
	if (!ISDIR(&st) || !ISDIR(&sta))
		return fail(r, p0, NFS3ERR_NOTDIR, 4);
	if (isdot(name) || isdot(name2))
		return fail(r, p0, NFS3ERR_INVAL, 4);
	if (pjoin(path, d1, name) < 0 || pjoin(path2, d2, name2) < 0)
		return fail(r, p0, NFS3ERR_NAMETOOLONG, 4);
	if (rename(path, path2) < 0)
		return fail(r, p0, nfserr(errno), 4);
	fh_droppath(path2);
	fh_rename(path, path2);
	xput32(r, NFS3_OK);
	wcc(r, &st, lstat(d1, &st2) < 0 ? (struct stat *)0 : &st2);
	wcc(r, &sta, lstat(d2, &st3) < 0 ? (struct stat *)0 : &st3);
	return RPC_SUCCESS;
}

static int
p_link(a, r)
	struct xbuf *a, *r;
{
	static struct stat sta;
	struct fhent *e, *d;
	u32 s, s2, ns;
	unsigned p0;

	p0 = r->len;
	path2[0] = 0;
	e = argfh(a, &st, &s);
	if (e)
		strcpy(path2, e->path);
	d = argfh(a, &sta, &s2);
	ns = argname(a, name);
	if (a->err)
		return RPC_GARBAGE;
	if (path2[0] == 0)
		return fail(r, p0, s, 3);
	if (d == 0)
		return fail(r, p0, s2, 3);
	if (RDONLY)
		return fail(r, p0, NFS3ERR_ROFS, 3);
	if (ns)
		return fail(r, p0, ns, 3);
	if (ISDIR(&st))
		return fail(r, p0, NFS3ERR_ISDIR, 3);
	if (!ISDIR(&sta))
		return fail(r, p0, NFS3ERR_NOTDIR, 3);
	if (pjoin(path, d->path, name) < 0)
		return fail(r, p0, NFS3ERR_NAMETOOLONG, 3);
	if (link(path2, path) < 0)
		return fail(r, p0, nfserr(errno), 3);
	xput32(r, NFS3_OK);
	postattr(r, lstat(path2, &st2) < 0 ? (struct stat *)0 : &st2);
	wcc(r, &sta, lstat(d->path, &st3) < 0 ? (struct stat *)0 : &st3);
	return RPC_SUCCESS;
}

/* READDIR, and READDIRPLUS when plus is set. */
static int
p_readdir(a, r, plus)
	struct xbuf *a, *r;
	int plus;
{
	struct fhent *e;
	struct dent d;
	struct u64 cookie;
	u8 verf[8];
	u32 s, cnt;
	unsigned p0, limit, need, clen, nent;
	int fd, k, eof, hasattr;

	p0 = r->len;
	e = argfh(a, &st, &s);
	xget64(a, &cookie);
	xgetfixed(a, verf, 8);
	if (plus)
		(void)xget32(a);	/* dircount */
	cnt = xget32(a);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 1);
	if (!ISDIR(&st))
		return failattr(r, p0, NFS3ERR_NOTDIR, &st);
	if (cookie.hi || (cookie.lo & 0x80000000UL))
		return failattr(r, p0, NFS3ERR_BAD_COOKIE, &st);
	fd = fh_fd(e, 0, &st);
	if (fd < 0)
		return failattr(r, p0, nfserr(errno), &st);
	e->off = -1;
	if (dir_start(e->path, fd, (long)cookie.lo) < 0)
		return failattr(r, p0, nfserr(errno), &st);
	limit = r->cap - p0;
	if (cnt < limit)
		limit = (unsigned)cnt;
	limit += p0;
	xput32(r, NFS3_OK);
	postattr(r, &st);
	memset((char *)verf, 0, 8);
	xputfixed(r, verf, 8);
	eof = 0;
	nent = 0;
	for (;;) {
		k = dir_read(&d);
		if (k <= 0) {
			eof = k == 0;
			break;
		}
		if (d.namlen > NAMEMAX)
			continue;
		need = 24 + ((d.namlen + 3) & ~3);
		clen = 0;
		hasattr = 0;
		if (plus) {
			need += 8;
			/* attributes and a handle, except for . and .. */
			if (!isdot(d.name) &&
			    pjoin(path, e->path, d.name) == 0 &&
			    lstat(path, &st2) == 0) {
				clen = fh_child(afh, afhlen, &st2, cfh);
				hasattr = 1;
				need += ATTRLEN;
				if (clen)
					need += 4 + ((clen + 3) & ~3);
			}
		}
		if (r->len + need + 8 > limit)
			break;
		xput32(r, ONE);
		xput32(r, ZERO);
		xput32(r, d.ino);
		xputopaque(r, (u8 *)d.name, d.namlen);
		xput32(r, ZERO);
		xput32(r, (u32)d.next);
		if (plus) {
			if (hasattr)
				postattr(r, &st2);
			else
				xput32(r, ZERO);
			if (clen) {
				xput32(r, ONE);
				xputopaque(r, cfh, clen);
			} else
				xput32(r, ZERO);
		}
		nent++;
	}
	dir_end();
	if (k < 0 && nent == 0)
		return failattr(r, p0, NFS3ERR_IO, &st);
	if (nent == 0 && !eof)
		return failattr(r, p0, NFS3ERR_TOOSMALL, &st);
	xput32(r, ZERO);
	xput32(r, (u32)eof);
	return RPC_SUCCESS;
}

/* v = n * m, where m is a power of two */
static void
scale(v, n, m)
	struct u64 *v;
	u32 n, m;
{
	v->lo = n & M32;
	v->hi = ((n >> 16) >> 16) & M32;
	while (m > 1) {
		v->hi = ((v->hi << 1) | ((v->lo >> 31) & 1)) & M32;
		v->lo = (v->lo << 1) & M32;
		m >>= 1;
	}
}

static int
p_fsstat(a, r)
	struct xbuf *a, *r;
{
#ifdef PDP11
	struct statfs sf;
#else
	struct statvfs sf;
#endif
	struct fhent *e;
	struct u64 v;
	u32 s, bs;
	unsigned p0;

	p0 = r->len;
	e = argfh(a, &st, &s);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 1);
#ifdef PDP11
	if (statfs(e->path, &sf) < 0)
		return failattr(r, p0, nfserr(errno), &st);
	bs = (u32)sf.f_bsize;
#else
	if (statvfs(e->path, &sf) < 0)
		return failattr(r, p0, nfserr(errno), &st);
	bs = (u32)sf.f_frsize;
#endif
	xput32(r, NFS3_OK);
	postattr(r, &st);
	scale(&v, (u32)sf.f_blocks, bs);
	xput64(r, &v);
	scale(&v, (u32)sf.f_bfree, bs);
	xput64(r, &v);
	scale(&v, (u32)sf.f_bavail, bs);
	xput64(r, &v);
	xput32(r, ZERO);
	xput32(r, (u32)sf.f_files);
	xput32(r, ZERO);
	xput32(r, (u32)sf.f_ffree);
	xput32(r, ZERO);
	xput32(r, (u32)sf.f_ffree);
	xput32(r, ZERO);		/* invarsec */
	return RPC_SUCCESS;
}

static int
p_fsinfo(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	u32 s;
	unsigned p0;

	p0 = r->len;
	e = argfh(a, &st, &s);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 1);
	xput32(r, NFS3_OK);
	postattr(r, &st);
	xput32(r, (u32)IOSIZE);		/* rtmax */
	xput32(r, (u32)IOSIZE);		/* rtpref */
	xput32(r, (u32)512);		/* rtmult */
	xput32(r, (u32)IOSIZE);		/* wtmax */
	xput32(r, (u32)IOSIZE);		/* wtpref */
	xput32(r, (u32)512);		/* wtmult */
	xput32(r, (u32)4096);		/* dtpref */
	xput32(r, ZERO);		/* largest file: 2 GB - 1 */
	xput32(r, 0x7fffffffUL);
	xput32(r, ONE);			/* times are kept to the second */
	xput32(r, ZERO);
	xput32(r, (u32)0x1b);		/* links, symlinks, homogeneous,
					   times can be set */
	return RPC_SUCCESS;
}

static int
p_pathconf(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	u32 s;
	unsigned p0;

	p0 = r->len;
	e = argfh(a, &st, &s);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 1);
	xput32(r, NFS3_OK);
	postattr(r, &st);
	xput32(r, (u32)32767);		/* linkmax */
	xput32(r, (u32)NAMEMAX);	/* name_max */
	xput32(r, ONE);			/* no_trunc */
	xput32(r, ONE);			/* chown_restricted */
	xput32(r, ZERO);		/* case_insensitive */
	xput32(r, ONE);			/* case_preserving */
	return RPC_SUCCESS;
}

static int
p_commit(a, r)
	struct xbuf *a, *r;
{
	struct fhent *e;
	u32 s;
	unsigned p0;
	int fd;

	p0 = r->len;
	e = argfh(a, &st, &s);
	if (a->err)
		return RPC_GARBAGE;
	if (e == 0)
		return fail(r, p0, s, 2);
	fd = fh_fd(e, 0, &st);
	if (fd < 0 || fsync(fd) < 0)
		return fail(r, p0, nfserr(errno), 2);
	xput32(r, NFS3_OK);
	wcc(r, (struct stat *)0, fstat(fd, &st2) < 0 ? (struct stat *)0 :
	    &st2);
	xputfixed(r, wverf, 8);
	return RPC_SUCCESS;
}

int
nfs_proc(proc, a, r)
	u32 proc;
	struct xbuf *a, *r;
{
	unsigned p0;
	int rv;

	p0 = r->len;
	switch ((int)proc) {
	case 0:	 rv = RPC_SUCCESS; break;
	case 1:	 rv = p_getattr(a, r); break;
	case 2:	 rv = p_setattr(a, r); break;
	case 3:	 rv = p_lookup(a, r); break;
	case 4:	 rv = p_access(a, r); break;
	case 5:	 rv = p_readlink(a, r); break;
	case 6:	 rv = p_read(a, r); break;
	case 7:	 rv = p_write(a, r); break;
	case 8:	 rv = p_create(a, r); break;
	case 9:	 rv = p_mkdir(a, r); break;
	case 10: rv = p_symlink(a, r); break;
	case 11: rv = fail(r, p0, NFS3ERR_NOTSUPP, 2); break;	/* MKNOD */
	case 12: rv = p_remove(a, r, 0); break;
	case 13: rv = p_remove(a, r, 1); break;
	case 14: rv = p_rename(a, r); break;
	case 15: rv = p_link(a, r); break;
	case 16: rv = p_readdir(a, r, 0); break;
	case 17: rv = p_readdir(a, r, 1); break;
	case 18: rv = p_fsstat(a, r); break;
	case 19: rv = p_fsinfo(a, r); break;
	case 20: rv = p_pathconf(a, r); break;
	case 21: rv = p_commit(a, r); break;
	default: return RPC_PROC_UNAVAIL;
	}
	if (debug && proc != 0 && rv == RPC_SUCCESS)
		fprintf(stderr, "nfs proc %d uid %u status %lu\n", (int)proc,
		    cred_uid(), getbe32(r->base + p0));
	return rv;
}
