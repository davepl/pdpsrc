/* NFS version 3 and MOUNT version 3 procedures (RFC 1813). */
#include "nfs.h"

#define MOUNT_PROG 100005UL
#define MOUNT_VERS 3UL
#define NFS_PROG 100003UL
#define NFS_VERS 3UL

#define P_NULL 0UL
#define P_GETATTR 1UL
#define P_SETATTR 2UL
#define P_LOOKUP 3UL
#define P_READLINK 5UL
#define P_READ 6UL
#define P_WRITE 7UL
#define P_CREATE 8UL
#define P_MKDIR 9UL
#define P_SYMLINK 10UL
#define P_MKNOD 11UL
#define P_REMOVE 12UL
#define P_RMDIR 13UL
#define P_RENAME 14UL
#define P_LINK 15UL
#define P_READDIR 16UL
#define P_READDIRPLUS 17UL
#define P_FSSTAT 18UL

u32
u64small(v)
	struct u64 *v;
{
	if (v->hi != 0 || (v->lo & 0x80000000UL))
		return M32;
	return v->lo;
}

int
nfs_errno(s)
	u32 s;
{
	switch ((int)(s > 30000UL ? 30000 : s)) {
	case 0: return 0;
	case 1: return EPERM;
	case 2: return ENOENT;
	case 5: return EIO;
	case 6: return ENXIO;
	case 13: return EACCES;
	case 17: return EEXIST;
	case 18: return EXDEV;
	case 19: return ENODEV;
	case 20: return ENOTDIR;
	case 21: return EISDIR;
	case 22: return EINVAL;
	case 27: return EFBIG;
	case 28: return ENOSPC;
	case 30: return EROFS;
	case 31: return EMLINK;
	case 63: return ENAMETOOLONG;
	case 66: return ENOTEMPTY;
	case 69: return EDQUOT;
	case 70: return ESTALE;
	case 71: return EREMOTE;
	case 10004: return EOPNOTSUPP;	/* NOTSUPP */
	case 10008: return EAGAIN;	/* JUKEBOX */
	case 10001: return ESTALE;	/* BADHANDLE */
	default: return EIO;
	}
}

char *
nfs_strerror(e)
	int e;
{
	static char buf[32];

	switch (e) {
	case 0: return "success";
	case EPERM: return "not owner";
	case ENOENT: return "no such file or directory";
	case EIO: return "I/O or protocol error";
	case EACCES: return "permission denied";
	case EEXIST: return "file exists";
	case ENOTDIR: return "not a directory";
	case EISDIR: return "is a directory";
	case EINVAL: return "invalid argument";
	case EFBIG: return "file too large";
	case ENOSPC: return "no space on server";
	case EROFS: return "read-only file system";
	case ENAMETOOLONG: return "name too long";
	case ENOTEMPTY: return "directory not empty";
	case ESTALE: return "stale file handle";
	case ETIMEDOUT: return "server not responding";
	case ECONNREFUSED: return "connection refused";
	case ECONNRESET: return "connection reset";
	case EHOSTUNREACH: return "unknown or unreachable host";
	case EPROTONOSUPPORT: return "RPC program/version unavailable";
	case EOPNOTSUPP: return "operation not supported";
	case EMSGSIZE: return "reply too large";
	case E2BIG: return "request too large";
	}
	sprintf(buf, "error %d", e);
	return buf;
}

int
nfs_mode(a)
	struct nfsattr *a;
{
	int m;

	m = (int)(a->mode & 07777);
	switch ((int)a->type) {
	case NF3REG: m |= S_IFREG; break;
	case NF3DIR: m |= S_IFDIR; break;
	case NF3BLK: m |= S_IFBLK; break;
	case NF3CHR: m |= S_IFCHR; break;
	case NF3LNK: m |= S_IFLNK; break;
#ifdef S_IFSOCK
	case NF3SOCK: m |= S_IFSOCK; break;
#endif
#ifdef S_IFIFO
	case NF3FIFO: m |= S_IFIFO; break;
#endif
	default: m |= S_IFREG; break;
	}
	return m;
}

static void
putfh(x, fh)
	struct xbuf *x;
	struct nfsfh *fh;
{
	xputopaque(x, fh->data, fh->len);
}

static void
getfh(x, fh)
	struct xbuf *x;
	struct nfsfh *fh;
{
	fh->len = xgetopaque(x, fh->data, NFS_FHMAX);
}

static void
getfattr(x, a)
	struct xbuf *x;
	struct nfsattr *a;
{
	a->type = xget32(x);
	a->mode = xget32(x);
	a->nlink = xget32(x);
	a->uid = xget32(x);
	a->gid = xget32(x);
	xget64(x, &a->size);
	xget64(x, &a->used);
	a->rdev1 = xget32(x);
	a->rdev2 = xget32(x);
	xget64(x, &a->fsid);
	xget64(x, &a->fileid);
	a->atime = xget32(x);
	a->atime_ns = xget32(x);
	a->mtime = xget32(x);
	a->mtime_ns = xget32(x);
	a->ctime = xget32(x);
	a->ctime_ns = xget32(x);
}

/* post_op_attr; returns 1 if attributes were present */
static int
getpostop(x, a)
	struct xbuf *x;
	struct nfsattr *a;
{
	struct nfsattr junk;

	if (xget32(x) == 0)
		return 0;
	getfattr(x, a ? a : &junk);
	return 1;
}

static void
getwcc(x, a)
	struct xbuf *x;
	struct nfsattr *a;
{
	if (xget32(x))
		xskip(x, 24);
	if (!getpostop(x, a) && a)
		a->type = 0;
}

static void
putsattr(x, s)
	struct xbuf *x;
	struct nfssattr *s;
{
	int m;

	m = s ? s->mask : 0;
	if (m & SA_MODE) {
		xput32(x, 1UL);
		xput32(x, s->mode & 07777);
	} else
		xput32(x, 0UL);
	if (m & SA_UID) {
		xput32(x, 1UL);
		xput32(x, s->uid);
	} else
		xput32(x, 0UL);
	if (m & SA_GID) {
		xput32(x, 1UL);
		xput32(x, s->gid);
	} else
		xput32(x, 0UL);
	if (m & SA_SIZE) {
		xput32(x, 1UL);
		xput64(x, &s->size);
	} else
		xput32(x, 0UL);
	if (m & SA_ATIME) {
		xput32(x, 2UL);
		xput32(x, s->atime);
		xput32(x, 0UL);
	} else
		xput32(x, (m & SA_ATIME_NOW) ? 1UL : 0UL);
	if (m & SA_MTIME) {
		xput32(x, 2UL);
		xput32(x, s->mtime);
		xput32(x, 0UL);
	} else
		xput32(x, (m & SA_MTIME_NOW) ? 1UL : 0UL);
}

static void
putdirop(x, dir, name)
	struct xbuf *x;
	struct nfsfh *dir;
	char *name;
{
	putfh(x, dir);
	xputstr(x, name);
}

/* Common call: status already decoded; returns errno. */
static int
call(m, x, r)
	struct nfsmount *m;
	struct xbuf *x, *r;
{
	int e;
	u32 st;

	if ((e = rpc_call(&m->nfs, x, r)) != 0)
		return e;
	st = xget32(r);
	if (r->err)
		return EPROTO_RPC;
	return nfs_errno(st);
}

int
nfs_mount(m, host, path)
	struct nfsmount *m;
	char *host, *path;
{
	struct rpcconn mc;
	struct xbuf x, r;
	unsigned port, nport;
	u32 st;
	int e, resv;

	memset((char *)m, 0, sizeof(*m));
	m->nfs.fd = -1;
	strncpy(m->host, host, sizeof(m->host) - 1);
	strncpy(m->path, path, sizeof(m->path) - 1);
	if ((e = rpc_resolve(host, &m->addr)) != 0)
		return e;
	resv = geteuid() == 0;
	if ((e = pmap_getport(m->addr, MOUNT_PROG, MOUNT_VERS, &port)) != 0)
		return e;
	if ((e = rpc_open(&mc, m->addr, port, MOUNT_PROG, MOUNT_VERS, resv)) != 0)
		return e;
	rpc_begin(&mc, &x, 1UL);	/* MOUNTPROC3_MNT */
	xputstr(&x, path);
	e = rpc_call(&mc, &x, &r);
	rpc_close(&mc);
	if (e)
		return e;
	st = xget32(&r);
	if (st != 0)
		return nfs_errno(st);
	getfh(&r, &m->root);
	if (r.err)
		return EPROTO_RPC;
	if (pmap_getport(m->addr, NFS_PROG, NFS_VERS, &nport) != 0)
		nport = 2049;
	if ((e = rpc_open(&m->nfs, m->addr, nport, NFS_PROG, NFS_VERS, resv)) != 0)
		return e;
	return nfs_null(m);
}

int
nfs_unmount(m)
	struct nfsmount *m;
{
	struct rpcconn mc;
	struct xbuf x, r;
	unsigned port;
	int e;

	rpc_close(&m->nfs);
	if ((e = pmap_getport(m->addr, MOUNT_PROG, MOUNT_VERS, &port)) != 0)
		return e;
	if ((e = rpc_open(&mc, m->addr, port, MOUNT_PROG, MOUNT_VERS,
	    geteuid() == 0)) != 0)
		return e;
	rpc_begin(&mc, &x, 3UL);	/* MOUNTPROC3_UMNT */
	xputstr(&x, m->path);
	e = rpc_call(&mc, &x, &r);
	rpc_close(&mc);
	return e;
}

int
nfs_null(m)
	struct nfsmount *m;
{
	struct xbuf x, r;

	rpc_begin(&m->nfs, &x, P_NULL);
	return rpc_call(&m->nfs, &x, &r);
}

int
nfs_getattr(m, fh, a)
	struct nfsmount *m;
	struct nfsfh *fh;
	struct nfsattr *a;
{
	struct xbuf x, r;
	int e;

	rpc_begin(&m->nfs, &x, P_GETATTR);
	putfh(&x, fh);
	if ((e = call(m, &x, &r)) != 0)
		return e;
	getfattr(&r, a);
	return r.err ? EPROTO_RPC : 0;
}

int
nfs_setattr(m, fh, s, a)
	struct nfsmount *m;
	struct nfsfh *fh;
	struct nfssattr *s;
	struct nfsattr *a;
{
	struct xbuf x, r;
	int e;

	rpc_begin(&m->nfs, &x, P_SETATTR);
	putfh(&x, fh);
	putsattr(&x, s);
	xput32(&x, 0UL);		/* no guard */
	e = call(m, &x, &r);
	if (e == 0 && a) {
		getwcc(&r, a);
		if (r.err)
			a->type = 0;	/* reply cut short: attributes unknown */
	}
	return e;
}

int
nfs_lookup(m, dir, name, fh, a)
	struct nfsmount *m;
	struct nfsfh *dir;
	char *name;
	struct nfsfh *fh;
	struct nfsattr *a;
{
	struct xbuf x, r;
	struct nfsattr tmp;
	int e;

	rpc_begin(&m->nfs, &x, P_LOOKUP);
	putdirop(&x, dir, name);
	if ((e = call(m, &x, &r)) != 0)
		return e;
	getfh(&r, fh);
	if (!getpostop(&r, a ? a : &tmp)) {
		if (r.err)
			return EPROTO_RPC;
		return a ? nfs_getattr(m, fh, a) : 0;
	}
	return r.err ? EPROTO_RPC : 0;
}

int
nfs_readlink(m, fh, buf, max)
	struct nfsmount *m;
	struct nfsfh *fh;
	char *buf;
	unsigned max;
{
	struct xbuf x, r;
	int e;

	rpc_begin(&m->nfs, &x, P_READLINK);
	putfh(&x, fh);
	if ((e = call(m, &x, &r)) != 0)
		return e;
	getpostop(&r, (struct nfsattr *)0);
	if (xgetstr(&r, buf, max) < 0)
		return ENAMETOOLONG;
	return 0;
}

int
nfs_read(m, fh, off, count, buf, got, eof, a)
	struct nfsmount *m;
	struct nfsfh *fh;
	u32 off;
	unsigned count;
	u8 *buf;
	unsigned *got;
	int *eof;
	struct nfsattr *a;
{
	struct xbuf x, r;
	struct u64 o;
	u32 n;
	u8 *p;
	unsigned len, total;
	int e, have;

	if (count > NFS_IOSIZE)
		count = NFS_IOSIZE;
	/*
	 * A server may return fewer bytes than asked without being at the
	 * end of the file; callers take a short count to mean the end.
	 */
	total = 0;
	*eof = 0;
	while (total < count && !*eof) {
		rpc_begin(&m->nfs, &x, P_READ);
		putfh(&x, fh);
		o.hi = 0;
		o.lo = off + total;
		xput64(&x, &o);
		xput32(&x, (u32)(count - total));
		if ((e = call(m, &x, &r)) != 0)
			return e;
		have = getpostop(&r, a);
		if (!have && a)
			a->type = 0;
		n = xget32(&r);
		*eof = xget32(&r) != 0;
		p = xgetref(&r, &len);
		if (r.err || p == 0 || (u32)len != n || len > count - total)
			return EPROTO_RPC;
		if (len == 0)
			break;
		memcpy(buf + total, p, len);
		total += len;
	}
	*got = total;
	return 0;
}

int
nfs_write(m, fh, off, count, buf, done, a)
	struct nfsmount *m;
	struct nfsfh *fh;
	u32 off;
	unsigned count;
	u8 *buf;
	unsigned *done;
	struct nfsattr *a;
{
	struct xbuf x, r;
	struct u64 o;
	u32 n;
	int e;

	if (count > NFS_IOSIZE)
		count = NFS_IOSIZE;
	rpc_begin(&m->nfs, &x, P_WRITE);
	putfh(&x, fh);
	o.hi = 0;
	o.lo = off;
	xput64(&x, &o);
	xput32(&x, (u32)count);
	xput32(&x, 2UL);		/* FILE_SYNC: no COMMIT bookkeeping */
	xputopaque(&x, buf, count);
	if ((e = call(m, &x, &r)) != 0)
		return e;
	getwcc(&r, a);
	n = xget32(&r);
	if (r.err || n > (u32)count)
		return EPROTO_RPC;
	*done = (unsigned)n;
	return 0;
}

/* Decode diropres3 (CREATE/MKDIR/SYMLINK/MKNOD); fetch fh by LOOKUP if
 * the server omitted it. */
static int
diropres(m, r, dir, name, fh, a)
	struct nfsmount *m;
	struct xbuf *r;
	struct nfsfh *dir;
	char *name;
	struct nfsfh *fh;
	struct nfsattr *a;
{
	struct nfsfh tfh;
	struct nfsattr ta;
	int hasfh, hasattr;

	if (fh == 0)
		fh = &tfh;
	if (a == 0)
		a = &ta;
	hasfh = xget32(r) != 0;
	if (hasfh)
		getfh(r, fh);
	hasattr = getpostop(r, a);
	if (r->err)
		return EPROTO_RPC;
	if (!hasfh)
		return nfs_lookup(m, dir, name, fh, a);
	if (!hasattr)
		return nfs_getattr(m, fh, a);
	return 0;
}

int
nfs_create(m, dir, name, s, exclusive, fh, a)
	struct nfsmount *m;
	struct nfsfh *dir;
	char *name;
	struct nfssattr *s;
	int exclusive;
	struct nfsfh *fh;
	struct nfsattr *a;
{
	struct xbuf x, r;
	int e;

	rpc_begin(&m->nfs, &x, P_CREATE);
	putdirop(&x, dir, name);
	xput32(&x, exclusive ? 1UL : 0UL);	/* GUARDED or UNCHECKED */
	putsattr(&x, s);
	if ((e = call(m, &x, &r)) != 0)
		return e;
	return diropres(m, &r, dir, name, fh, a);
}

int
nfs_mkdir(m, dir, name, s, fh, a)
	struct nfsmount *m;
	struct nfsfh *dir;
	char *name;
	struct nfssattr *s;
	struct nfsfh *fh;
	struct nfsattr *a;
{
	struct xbuf x, r;
	int e;

	rpc_begin(&m->nfs, &x, P_MKDIR);
	putdirop(&x, dir, name);
	putsattr(&x, s);
	if ((e = call(m, &x, &r)) != 0)
		return e;
	return diropres(m, &r, dir, name, fh, a);
}

int
nfs_symlink(m, dir, name, target, fh, a)
	struct nfsmount *m;
	struct nfsfh *dir;
	char *name, *target;
	struct nfsfh *fh;
	struct nfsattr *a;
{
	struct xbuf x, r;
	int e;

	rpc_begin(&m->nfs, &x, P_SYMLINK);
	putdirop(&x, dir, name);
	putsattr(&x, (struct nfssattr *)0);
	xputstr(&x, target);
	if ((e = call(m, &x, &r)) != 0)
		return e;
	return diropres(m, &r, dir, name, fh, a);
}

int
nfs_mknod(m, dir, name, type, s, maj, min, fh, a)
	struct nfsmount *m;
	struct nfsfh *dir;
	char *name;
	int type;
	struct nfssattr *s;
	u32 maj, min;
	struct nfsfh *fh;
	struct nfsattr *a;
{
	struct xbuf x, r;
	int e;

	rpc_begin(&m->nfs, &x, P_MKNOD);
	putdirop(&x, dir, name);
	xput32(&x, (u32)type);
	putsattr(&x, s);
	if (type == NF3CHR || type == NF3BLK) {
		xput32(&x, maj);
		xput32(&x, min);
	}
	if ((e = call(m, &x, &r)) != 0)
		return e;
	return diropres(m, &r, dir, name, fh, a);
}

static int
dirop(m, proc, dir, name)
	struct nfsmount *m;
	u32 proc;
	struct nfsfh *dir;
	char *name;
{
	struct xbuf x, r;

	rpc_begin(&m->nfs, &x, proc);
	putdirop(&x, dir, name);
	return call(m, &x, &r);
}

int
nfs_remove(m, dir, name)
	struct nfsmount *m;
	struct nfsfh *dir;
	char *name;
{
	return dirop(m, P_REMOVE, dir, name);
}

int
nfs_rmdir(m, dir, name)
	struct nfsmount *m;
	struct nfsfh *dir;
	char *name;
{
	return dirop(m, P_RMDIR, dir, name);
}

int
nfs_rename(m, fdir, fname, tdir, tname)
	struct nfsmount *m;
	struct nfsfh *fdir, *tdir;
	char *fname, *tname;
{
	struct xbuf x, r;

	rpc_begin(&m->nfs, &x, P_RENAME);
	putdirop(&x, fdir, fname);
	putdirop(&x, tdir, tname);
	return call(m, &x, &r);
}

int
nfs_link(m, fh, dir, name)
	struct nfsmount *m;
	struct nfsfh *fh, *dir;
	char *name;
{
	struct xbuf x, r;

	rpc_begin(&m->nfs, &x, P_LINK);
	putfh(&x, fh);
	putdirop(&x, dir, name);
	return call(m, &x, &r);
}

static u8 dirbuf[NFS_BUFSIZE];

int
nfs_readdir(m, dir, fn, arg)
	struct nfsmount *m;
	struct nfsfh *dir;
	nfs_dirfn fn;
	void *arg;
{
	struct xbuf x, r;
	struct u64 cookie, fileid;
	u8 verf[8];
	char name[NFS_NAMEMAX + 1];
	int e, eof, n;

	cookie.hi = cookie.lo = 0;
	memset((char *)verf, 0, sizeof(verf));
	for (;;) {
		rpc_begin(&m->nfs, &x, P_READDIR);
		putfh(&x, dir);
		xput64(&x, &cookie);
		xputfixed(&x, verf, 8);
		xput32(&x, (u32)NFS_IOSIZE);
		if ((e = call(m, &x, &r)) != 0)
			return e;
		/* The callback may issue RPCs, which reuse rpc_rbuf. */
		memcpy(dirbuf, r.base, r.len);
		r.base = dirbuf;
		getpostop(&r, (struct nfsattr *)0);
		xgetfixed(&r, verf, 8);
		n = 0;
		while (xget32(&r) != 0 && !r.err) {
			xget64(&r, &fileid);
			if (xgetstr(&r, name, sizeof(name)) < 0)
				return EPROTO_RPC;
			xget64(&r, &cookie);
			if (r.err)
				return EPROTO_RPC;
			n++;
			if ((*fn)(arg, name, &fileid, &cookie))
				return 0;
		}
		eof = xget32(&r) != 0;
		if (r.err)
			return EPROTO_RPC;
		if (eof || n == 0)
			return 0;
	}
}

/*
 * READDIRPLUS: callback receives attributes and handle when the server
 * supplied them (NULL otherwise).  The callback must not issue RPCs.
 * dirattr, if not NULL, receives the directory's post-operation
 * attributes (type 0 if absent).
 */
int
nfs_readdirplus(m, dir, fn, arg, dirattr)
	struct nfsmount *m;
	struct nfsfh *dir;
	nfs_plusfn fn;
	void *arg;
	struct nfsattr *dirattr;
{
	struct xbuf x, r;
	struct u64 cookie, fileid;
	struct nfsattr a;
	struct nfsfh fh;
	u8 verf[8];
	char name[NFS_NAMEMAX + 1];
	int e, eof, n, hasa, hasfh, first;

	cookie.hi = cookie.lo = 0;
	memset((char *)verf, 0, sizeof(verf));
	if (dirattr)
		dirattr->type = 0;
	first = 1;
	for (;;) {
		rpc_begin(&m->nfs, &x, P_READDIRPLUS);
		putfh(&x, dir);
		xput64(&x, &cookie);
		xputfixed(&x, verf, 8);
		xput32(&x, (u32)(NFS_IOSIZE / 2));	/* dircount */
		xput32(&x, (u32)NFS_IOSIZE);		/* maxcount */
		if ((e = call(m, &x, &r)) != 0)
			return e;
		if (getpostop(&r, &a) && first && dirattr)
			*dirattr = a;
		first = 0;
		xgetfixed(&r, verf, 8);
		n = 0;
		while (xget32(&r) != 0 && !r.err) {
			xget64(&r, &fileid);
			if (xgetstr(&r, name, sizeof(name)) < 0)
				return EPROTO_RPC;
			xget64(&r, &cookie);
			hasa = getpostop(&r, &a);
			hasfh = xget32(&r) != 0;
			if (hasfh)
				getfh(&r, &fh);
			if (r.err)
				return EPROTO_RPC;
			n++;
			if ((*fn)(arg, name, &fileid, hasa ? &a : (struct nfsattr *)0,
			    hasfh ? &fh : (struct nfsfh *)0))
				return 0;
		}
		eof = xget32(&r) != 0;
		if (r.err)
			return EPROTO_RPC;
		if (eof || n == 0)
			return 0;
	}
}

int
nfs_fsstat(m, fh, s)
	struct nfsmount *m;
	struct nfsfh *fh;
	struct nfsfsstat *s;
{
	struct xbuf x, r;
	int e;

	rpc_begin(&m->nfs, &x, P_FSSTAT);
	putfh(&x, fh);
	if ((e = call(m, &x, &r)) != 0)
		return e;
	getpostop(&r, (struct nfsattr *)0);
	xget64(&r, &s->tbytes);
	xget64(&r, &s->fbytes);
	xget64(&r, &s->abytes);
	xget64(&r, &s->tfiles);
	xget64(&r, &s->ffiles);
	xget64(&r, &s->afiles);
	return r.err ? EPROTO_RPC : 0;
}

/*
 * Resolve a slash-separated path relative to the mount root.  "." and
 * ".." are sent to the server, which handles them in the export.
 * Symlinks are followed (relative ones only within the export) for all
 * but the last component unless follow is set.
 */
int
nfs_walk(m, path, follow, fh, a)
	struct nfsmount *m;
	char *path;
	int follow;
	struct nfsfh *fh;
	struct nfsattr *a;
{
	static char work[NFS_PATHMAX], link[NFS_PATHMAX];
	char comp[NFS_NAMEMAX + 1];
	struct nfsfh cur, next, parent;
	struct nfsattr ca;
	char *p, *q;
	int e, n, depth;

	if (strlen(path) >= sizeof(work))
		return ENAMETOOLONG;
	strcpy(work, path);
	cur = m->root;
	if ((e = nfs_getattr(m, &cur, &ca)) != 0)
		return e;
	depth = 0;
	p = work;
	for (;;) {
		while (*p == '/')
			p++;
		if (*p == 0)
			break;
		q = p;
		while (*q && *q != '/')
			q++;
		n = (int)(q - p);
		if (n > NFS_NAMEMAX)
			return ENAMETOOLONG;
		memcpy(comp, p, n);
		comp[n] = 0;
		p = q;
		if (ca.type != NF3DIR)
			return ENOTDIR;
		if ((e = nfs_lookup(m, &cur, comp, &next, &ca)) != 0)
			return e;
		while (*p == '/')
			p++;
		if (ca.type == NF3LNK && (*p != 0 || follow)) {
			if (++depth > 8)
				return ELOOP;
			if ((e = nfs_readlink(m, &next, link, sizeof(link))) != 0)
				return e;
			if (strlen(link) + strlen(p) + 2 >= sizeof(link))
				return ENAMETOOLONG;
			if (*p) {
				strcat(link, "/");
				strcat(link, p);
			}
			strcpy(work, link);
			p = work;
			if (*p == '/') {
				/* absolute target: interpret within the export */
				cur = m->root;
			}
			parent = cur;
			if ((e = nfs_getattr(m, &parent, &ca)) != 0)
				return e;
			continue;
		}
		cur = next;
	}
	*fh = cur;
	if (a)
		*a = ca;
	return 0;
}

/* Split path into the parent directory handle and final component. */
int
nfs_parent(m, path, dir, leaf)
	struct nfsmount *m;
	char *path;
	struct nfsfh *dir;
	char *leaf;
{
	static char tmp[NFS_PATHMAX];
	struct nfsattr a;
	char *s;
	int e;

	if (strlen(path) >= sizeof(tmp))
		return ENAMETOOLONG;
	strcpy(tmp, path);
	s = tmp + strlen(tmp);
	while (s > tmp && s[-1] == '/')
		*--s = 0;
	s = strrchr(tmp, '/');
	if (s == 0) {
		if (strlen(tmp) > NFS_NAMEMAX)
			return ENAMETOOLONG;
		strcpy(leaf, tmp);
		tmp[0] = 0;
	} else {
		if (strlen(s + 1) > NFS_NAMEMAX)
			return ENAMETOOLONG;
		strcpy(leaf, s + 1);
		*s = 0;
	}
	if (leaf[0] == 0)
		return EINVAL;
	if ((e = nfs_walk(m, tmp, 1, dir, &a)) != 0)
		return e;
	if (a.type != NF3DIR)
		return ENOTDIR;
	return 0;
}
