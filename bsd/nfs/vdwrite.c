/*
 * Read-write support for the virtual UFS disk.
 *
 * The kernel only ever writes blocks.  Its intent is recovered from them:
 *  - inode blocks: new inodes (a mode appears), freed inodes (mode 0),
 *    chmod/chown/utimes/truncate, and the block map of each inode;
 *  - directory blocks: names added, removed or redirected, which become
 *    CREATE, MKDIR, SYMLINK, REMOVE, RMDIR, RENAME and LINK calls;
 *  - data blocks: WRITE calls, clamped to the size the kernel holds.
 *
 * New data lands in "pool" blocks handed out through the superblock's
 * free list.  The owner of a pool block is found from the kernel's
 * in-core inodes (NFSDIOCOWNER), from inode blocks, or from indirect
 * blocks.  A block whose owner is not yet known is spooled locally.
 *
 * 2.11BSD writes a new inode synchronously before entering its name, and
 * directory writes are always synchronous, so names and inodes arrive in
 * a usable order.  Delayed writes are pushed by a helper that runs sync.
 */
#include "vd.h"

#define NSPOOL		256	/* blocks awaiting an owner */
#define NPEND		8	/* names awaiting a matching removal */
#define NDW		16	/* inodes with data written */
#define MAXENT		96	/* directory entries per block */

#define PM_MAPPED	1
#define PM_SPOOLED	2
#define PM_CHAIN	4

struct pmap {			/* pool block map entry */
	unsigned short ino, role, flags;
	unsigned short gen;		/* generation of ino when mapped */
};

static int kinfd = -1, poolfd = -1, spoolfd = -1, ctl = -1;
static long nextpool = 1;	/* next pool index to hand out */
static long chainidx;		/* outstanding free-list chain block */
static long spooled[NSPOOL];
static int nspooled;

static struct pend {
	unsigned dir, ino, oldino;
	int replace;
	u32 when;
	char name[MAXNAMLEN + 1];
} pend[NPEND];

static unsigned dw[NDW];	/* data written since last inode write */
static unsigned sizeok;		/* inode whose shadow was just written */

/*
 * Blocks written while the kernel's inode was out of core, clamped to a
 * size that may be stale.  Their data stays in the spool file; the next
 * write of their inode rewrites them with the new size.
 */
#define NREDO		64
#define REDOSECS	60
static struct redo {
	long idx;
	unsigned ino, lbn;
	u32 when;
} redo[NREDO];
static u8 oldblk[DEV_BSIZE];

static long
poolidx(bn)
	daddr_t bn;
{
	if (bn <= VD_POOLBASE || bn >= VD_POOLEND)
		return -1L;
	return (long)(bn - VD_POOLBASE);
}

static daddr_t
poolbn(idx)
	long idx;
{
	return VD_POOLBASE + (daddr_t)idx;
}

static int
fileat(fd, off, p, n, wr)
	int fd;
	long off;
	char *p;
	int n, wr;
{
	int r;

	if (lseek(fd, (off_t)off, 0) < 0)
		return -1;
	if (wr)
		return write(fd, p, n) == n ? 0 : -1;
	r = read(fd, p, n);
	if (r < 0)
		return -1;
	if (r < n)
		memset(p + r, 0, n - r);
	return 0;
}

static void
pget(idx, pm)
	long idx;
	struct pmap *pm;
{
	if (fileat(poolfd, idx * (long)sizeof(*pm), (char *)pm,
	    (int)sizeof(*pm), 0) < 0)
		memset((char *)pm, 0, sizeof(*pm));
}

static void
pput(idx, pm)
	long idx;
	struct pmap *pm;
{
	fileat(poolfd, idx * (long)sizeof(*pm), (char *)pm, (int)sizeof(*pm), 1);
}

static void
shadow(ino, dp, wr)
	unsigned ino;
	struct dinode *dp;
	int wr;
{
	if (fileat(kinfd, (long)ino * (long)sizeof(*dp), (char *)dp,
	    (int)sizeof(*dp), wr) < 0 && !wr)
		memset((char *)dp, 0, sizeof(*dp));
}

/* Size of directory ino as the kernel last had it, or -1. */
long
vw_ksize(ino)
	unsigned ino;
{
	struct dinode sh;

	if (kinfd < 0)
		return -1L;
	shadow(ino, &sh, 0);
	if ((sh.di_mode & IFMT) != IFDIR || sh.di_size <= 0)
		return -1L;
	return (long)sh.di_size;
}

static int
openwork(dir, unit, name, fdp)
	char *dir, *name;
	int unit, *fdp;
{
	char path[96];

	sprintf(path, "%s/.nfs%s%d", dir, name, unit);
	unlink(path);
	*fdp = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	unlink(path);
	return *fdp < 0 ? errno : 0;
}

int
vw_init(dir, unit, fd)
	char *dir;
	int unit, fd;
{
	int e;

	ctl = fd;
	if ((e = openwork(dir, unit, "kin", &kinfd)) != 0 ||
	    (e = openwork(dir, unit, "pool", &poolfd)) != 0 ||
	    (e = openwork(dir, unit, "spool", &spoolfd)) != 0)
		return e;
	nextpool = 1;
	chainidx = 0;
	nspooled = 0;
	memset((char *)pend, 0, sizeof(pend));
	memset((char *)dw, 0, sizeof(dw));
	return 0;
}

/* ---- free list ---- */

static long
newpool()
{
	if (poolbn(nextpool) <= 0 || poolbn(nextpool) >= VD_POOLEND) {
		syslog(LOG_ERR, "pool of new blocks exhausted; remount");
		return 0L;
	}
	return nextpool++;
}

static void
fill(nfree, list)
	short *nfree;
	daddr_t *list;
{
	struct pmap pm;
	int i;
	long idx;

	idx = newpool();
	chainidx = idx;
	if (idx) {
		memset((char *)&pm, 0, sizeof(pm));
		pm.flags = PM_CHAIN;
		pput(idx, &pm);
	}
	list[0] = idx ? poolbn(idx) : 0;
	for (i = 1; i < NICFREE; i++) {
		idx = newpool();
		list[i] = idx ? poolbn(idx) : 0;
	}
	*nfree = NICFREE;
}

void
vw_freelist(fs)
	struct fs *fs;
{
	fill(&fs->fs_nfree, fs->fs_free);
}

/* ---- in-core inode query ---- */

static long
kernsize(ino)
	unsigned ino;
{
	struct nfsd_owner o;

	memset((char *)&o, 0, sizeof(o));
	o.no_blkno = 0;
	o.no_ino = (ino_t)ino;
	if (ioctl(ctl, NFSDIOCOWNER, (char *)&o) < 0 || o.no_ino == 0)
		return -1L;
	return (long)o.no_size;
}

static void
datamark(ino)
	unsigned ino;
{
	int i;

	for (i = 0; i < NDW; i++)
		if (dw[i] == ino)
			return;
	for (i = 0; i < NDW; i++)
		if (dw[i] == 0) {
			dw[i] = ino;
			return;
		}
	dw[ino % NDW] = ino;
}

static int
dataclear(ino)
	unsigned ino;
{
	int i, was;

	was = 0;
	for (i = 0; i < NDW; i++)
		if (dw[i] == ino) {
			dw[i] = 0;
			was = 1;
		}
	return was;
}

/* ---- spool ---- */

/*
 * Keep a block locally.  Blocks of a known file that is not on the server
 * yet are also listed, for replay() once it is created; a block whose owner
 * is unknown is found again through its map entry when its inode or
 * indirect block names it.
 */
static void
spool(idx, buf)
	long idx;
	u8 *buf;
{
	struct pmap pm;
	int i;

	fileat(spoolfd, idx * (long)DEV_BSIZE, (char *)buf, DEV_BSIZE, 1);
	pget(idx, &pm);
	pm.flags |= PM_SPOOLED;
	pput(idx, &pm);
	if ((pm.flags & PM_MAPPED) == 0)
		return;
	for (i = 0; i < nspooled; i++)
		if (spooled[i] == idx)
			return;
	if (nspooled < NSPOOL)
		spooled[nspooled++] = idx;
	else
		syslog(LOG_ERR, "spool full: data of block %ld may be lost",
		    (long)poolbn(idx));
}

static void
unspool(idx)
	long idx;
{
	int i;

	for (i = 0; i < nspooled; i++)
		if (spooled[i] == idx) {
			spooled[i] = spooled[--nspooled];
			return;
		}
}

static int blockwrite();

/* Record the owner of a pool block; replay it if it was spooled. */
static void
mapset(idx, ino, role)
	long idx;
	unsigned ino, role;
{
	struct pmap pm;

	if (idx <= 0)
		return;
	pget(idx, &pm);
	if ((pm.flags & PM_MAPPED) && pm.ino == ino && pm.role == role &&
	    pm.gen == itab_gen(ino) && (pm.flags & PM_SPOOLED) == 0)
		return;
	pm.ino = (unsigned short)ino;
	pm.role = (unsigned short)role;
	pm.gen = (unsigned short)itab_gen(ino);
	pm.flags |= PM_MAPPED;
	pm.flags &= ~PM_CHAIN;
	pput(idx, &pm);
	if (pm.flags & PM_SPOOLED) {
		u8 buf[DEV_BSIZE];

		fileat(spoolfd, idx * (long)DEV_BSIZE, (char *)buf, DEV_BSIZE, 0);
		if (blockwrite(ino, role, buf, idx) != EAGAIN) {
			pget(idx, &pm);
			pm.flags &= ~PM_SPOOLED;
			pput(idx, &pm);
			unspool(idx);
		}
	}
}

/* ---- data, symlinks, indirect blocks ---- */

static void
redoadd(idx, ino, lbn, buf)
	long idx;
	unsigned ino, lbn;
	u8 *buf;
{
	struct redo *rp, *old;
	u32 now;
	int i;

	now = vd_now();
	old = &redo[0];
	for (i = 0; i < NREDO; i++) {
		rp = &redo[i];
		if (rp->idx == idx)
			break;
		if (rp->when < old->when)
			old = rp;
	}
	if (i == NREDO)
		rp = old;
	fileat(spoolfd, idx * (long)DEV_BSIZE, (char *)buf, DEV_BSIZE, 1);
	rp->idx = idx;
	rp->ino = ino;
	rp->lbn = lbn;
	rp->when = now;
}

static int datawrite();

/* The inode of ino was just written: rewrite its clamped blocks. */
static void
redorun(ino)
	unsigned ino;
{
	struct redo *rp;
	struct pmap pm;
	u8 buf[DEV_BSIZE];
	u32 now;
	int i;

	now = vd_now();
	for (i = 0; i < NREDO; i++) {
		rp = &redo[i];
		if (rp->idx == 0)
			continue;
		if (now - rp->when > REDOSECS) {
			rp->idx = 0;
			continue;
		}
		if (rp->ino != ino)
			continue;
		pget(rp->idx, &pm);
		if ((pm.flags & PM_MAPPED) && pm.ino == ino && pm.role == rp->lbn &&
		    pm.gen == itab_gen(ino) &&
		    fileat(spoolfd, rp->idx * (long)DEV_BSIZE, (char *)buf,
		    DEV_BSIZE, 0) >= 0) {
			if (vd_debug)
				syslog(LOG_DEBUG, "ino %u lbn %u: rewritten",
				    ino, rp->lbn);
			(void)datawrite(ino, rp->lbn, buf, 0L);
		}
		rp->idx = 0;
	}
}

/*
 * Write file block lbn to the server, clamped to the file size the kernel
 * has.  If the kernel no longer has the inode in core, the shadow may lag
 * behind: the new size can still be in a delayed write of the inode block.
 * A pool block (idx) that reaches past the shadow's size is then written
 * clamped and also kept, to be rewritten when that inode block arrives.
 */
static int
datawrite(ino, lbn, buf, idx)
	unsigned ino, lbn;
	u8 *buf;
	long idx;
{
	struct irec *r;
	struct nfsfh fh;
	struct nfsattr a;
	struct dinode sh;
	long size;
	u32 off;
	unsigned n, done, pos;
	int e;

	r = itab_get(ino);
	if (r == 0)
		return 0;		/* file is gone */
	if (r->flags & IR_LOCAL)
		return EAGAIN;		/* not created on the server yet */
	size = kernsize(ino);
	off = (u32)lbn * DEV_BSIZE;
	if (size < 0) {
		shadow(ino, &sh, 0);
		size = sh.di_mode ? sh.di_size : (long)r->size;
		if (idx > 0 && ino != sizeok && (long)off + DEV_BSIZE > size) {
			if (vd_debug)
				syslog(LOG_DEBUG, "ino %u lbn %u: kept for "
				    "inode (size %ld)", ino, lbn, size);
			redoadd(idx, ino, lbn, buf);
		}
	}
	if ((long)off >= size)
		return 0;		/* beyond the kernel's end of file */
	n = DEV_BSIZE;
	if (size - (long)off < (long)DEV_BSIZE)
		n = (unsigned)(size - (long)off);
	vd_recfh(r, &fh);
	for (pos = 0; pos < n; pos += done) {
		a.type = 0;
		e = nfs_write(&vd_mnt, &fh, off + pos, n - pos, buf + pos, &done, &a);
		if (e == 0 && done == 0)
			e = EIO;
		if (e) {
			syslog(LOG_ERR, "write ino %u: %s", ino, nfs_strerror(e));
			return e;
		}
	}
	r = itab_get(ino);
	if (r && a.type) {
		itab_setattr(r, &a);
		itab_put(ino, r);
	}
	vd_rcinval(ino);
	datamark(ino);
	return 0;
}

static void bindnew();

static int
linkwrite(ino, buf)
	unsigned ino;
	u8 *buf;
{
	struct irec *r;
	struct nfsfh dfh, fh;
	struct nfsattr a;
	char target[DEV_BSIZE + 1];
	long size;
	int i, e;

	r = itab_get(ino);
	if (r == 0 || (r->flags & IR_LOCAL) == 0)
		return 0;		/* existing links are never rewritten */
	for (i = 0; i < NPEND; i++)
		if (pend[i].ino == ino && pend[i].dir && pend[i].replace == -1)
			break;
	if (i == NPEND)
		return EAGAIN;		/* name not known yet */
	size = kernsize(ino);
	if (size < 0 || size > DEV_BSIZE)
		size = strlen((char *)buf) < DEV_BSIZE ? strlen((char *)buf) : 0;
	memcpy(target, (char *)buf, (int)size);
	target[size] = 0;
	r = itab_get(pend[i].dir);
	if (r == 0)
		return 0;
	vd_recfh(r, &dfh);
	e = nfs_symlink(&vd_mnt, &dfh, pend[i].name, target, &fh, &a);
	if (e) {
		syslog(LOG_ERR, "symlink %s: %s", pend[i].name, nfs_strerror(e));
		pend[i].dir = 0;
		return 0;
	}
	bindnew(ino, &fh, &a, pend[i].dir);
	pend[i].dir = 0;
	return 0;
}

static int
indwrite(ino, sub, buf)
	unsigned ino, sub;
	u8 *buf;
{
	daddr_t *ap;
	u32 base;
	unsigned j, k;

	ap = (daddr_t *)buf;
	if (sub == VD_IND2) {
		for (k = 0; k < NINDIR; k++)
			if (poolidx(ap[k]) > 0 && VD_IND2 - 1 - k > VD_MAXLBN)
				mapset(poolidx(ap[k]), ino, VD_IND2 - 1 - k);
		return 0;
	}
	if (sub == VD_IND1)
		base = NDADDR;
	else
		base = NDADDR + NINDIR + (u32)(VD_IND2 - 1 - sub) * NINDIR;
	for (j = 0; j < NINDIR; j++)
		if (poolidx(ap[j]) > 0 && base + j < VD_MAXLBN)
			mapset(poolidx(ap[j]), ino, (unsigned)(base + j));
	return 0;
}

/* ---- directories ---- */

struct ent {
	unsigned off, ino, namlen;
	char *name;
};

static int
parse(blk, ev)
	u8 *blk;
	struct ent *ev;
{
	struct direct *d;
	unsigned c, off;
	int n;

	n = 0;
	for (c = 0; c < DEV_BSIZE; c += DIRBLKSIZ) {
		for (off = c; off < c + DIRBLKSIZ; ) {
			d = (struct direct *)(blk + off);
			if (d->d_reclen < 8 || off + d->d_reclen > c + DIRBLKSIZ ||
			    d->d_namlen > MAXNAMLEN)
				break;
			if (d->d_ino != 0 && n < MAXENT) {
				ev[n].off = off;
				ev[n].ino = d->d_ino;
				ev[n].namlen = d->d_namlen;
				ev[n].name = d->d_name;
				n++;
			}
			off += d->d_reclen;
		}
	}
	return n;
}

static int
findname(ev, n, name, len)
	struct ent *ev;
	int n;
	char *name;
	unsigned len;
{
	int i;

	for (i = 0; i < n; i++)
		if (ev[i].namlen == len && memcmp(ev[i].name, name, len) == 0)
			return i;
	return -1;
}

static int
dotname(e)
	struct ent *e;
{
	return e->name[0] == '.' && (e->namlen == 1 ||
	    (e->namlen == 2 && e->name[1] == '.'));
}

/* Replay blocks of ino that were spooled before it reached the server. */
static void
replay(ino)
	unsigned ino;
{
	struct pmap pm;
	long list[NSPOOL];
	int i, n;

	n = nspooled;
	for (i = 0; i < n; i++)
		list[i] = spooled[i];
	for (i = 0; i < n; i++) {
		pget(list[i], &pm);
		if ((pm.flags & PM_MAPPED) && pm.ino == ino &&
		    pm.gen != itab_gen(ino)) {
			/* data of an earlier file with this number */
			pm.flags = 0;
			pput(list[i], &pm);
			unspool(list[i]);
			continue;
		}
		if ((pm.flags & PM_MAPPED) && pm.ino == ino &&
		    (pm.flags & PM_SPOOLED)) {
			pm.flags &= ~PM_MAPPED;
			pput(list[i], &pm);
			mapset(list[i], ino, pm.role);
		}
	}
}

static void
bindnew(ino, fh, a, dir)
	unsigned ino, dir;
	struct nfsfh *fh;
	struct nfsattr *a;
{
	struct irec *r, nr;
	struct dinode sh;
	struct nfssattr sa;

	r = itab_get(ino);
	if (r == 0)
		return;
	nr = *r;
	itab_bind(ino, &a->fileid, fh, a, dir);
	if ((nr.mode & IFMT) == IFDIR || (a->type == NF3DIR))
		vd_dirtouch(ino, a->mtime);
	/* Give the file the owner the kernel chose, if the server lets us. */
	shadow(ino, &sh, 0);
	if (sh.di_mode && (a->uid != (u32)sh.di_uid || a->gid != (u32)sh.di_gid)) {
		sa.mask = SA_UID | SA_GID;
		sa.uid = (u32)sh.di_uid;
		sa.gid = (u32)sh.di_gid;
		if (nfs_setattr(&vd_mnt, fh, &sa, a) == 0) {
			r = itab_get(ino);
			if (r) {
				itab_setattr(r, a);
				itab_put(ino, r);
			}
		}
	}
	replay(ino);
}

static void
dirmtime(dir)
	unsigned dir;
{
	struct irec *r;

	r = vd_freshrec(dir, 1);
	if (r)
		vd_dirtouch(dir, r->mtime);
}

/*
 * After a name of ino went away on the server, forget ino if the file
 * itself is gone.  The kernel may hand the inode number out again before
 * it writes the freed inode, so the record must not outlive the file.
 */
static void
checkgone(ino)
	unsigned ino;
{
	struct irec *r;
	struct nfsfh fh;
	struct nfsattr a;
	int e;

	r = itab_get(ino);
	if (r == 0 || (r->flags & IR_LOCAL))
		return;
	vd_recfh(r, &fh);
	a.type = 0;
	e = nfs_getattr(&vd_mnt, &fh, &a);
	if (e == ESTALE || e == ENOENT) {
		if (vd_debug)
			syslog(LOG_DEBUG, "ino %u gone", ino);
		itab_drop(ino);
	} else if (e == 0 && a.type) {
		r = itab_get(ino);
		if (r) {
			itab_setattr(r, &a);
			itab_put(ino, r);
		}
	}
}

/* A name appeared in directory dir (replacing oldino's, if not 0). */
static void
added(dir, dfh, name, ino, oldino)
	unsigned dir, ino, oldino;
	struct nfsfh *dfh;
	char *name;
{
	struct irec *r;
	struct dinode sh;
	struct nfssattr sa;
	struct nfsfh fh;
	struct nfsattr a;
	int e, i;

	r = itab_get(ino);
	if (r == 0) {
		syslog(LOG_ERR, "entry %s: unknown inode %u", name, ino);
		return;
	}
	if (vd_debug)
		syslog(LOG_DEBUG, "dir %u: + %s ino %u%s", dir, name, ino,
		    (r->flags & IR_LOCAL) ? " (new)" : "");
	if ((r->flags & IR_LOCAL) == 0) {
		/* An existing file under a new name: rename or link. */
		for (i = 0; i < NPEND && pend[i].dir; i++)
			;
		if (i == NPEND)
			i = 0;
		pend[i].dir = dir;
		pend[i].ino = ino;
		pend[i].oldino = oldino;
		pend[i].replace = 0;
		pend[i].when = vd_now();
		strcpy(pend[i].name, name);
		return;
	}
	shadow(ino, &sh, 0);
	sa.mask = SA_MODE;
	sa.mode = (u32)(sh.di_mode & 07777);
	switch (sh.di_mode & IFMT) {
	case IFREG:
		sa.mask |= SA_SIZE;
		sa.size.hi = sa.size.lo = 0;
		e = nfs_create(&vd_mnt, dfh, name, &sa, 0, &fh, &a);
		break;
	case IFDIR:
		e = nfs_mkdir(&vd_mnt, dfh, name, &sa, &fh, &a);
		if (e == EEXIST)
			e = nfs_lookup(&vd_mnt, dfh, name, &fh, &a);
		break;
	case IFLNK:
		/* Created once the kernel writes the target. */
		for (i = 0; i < NPEND && pend[i].dir; i++)
			;
		if (i == NPEND)
			i = 0;
		pend[i].dir = dir;
		pend[i].ino = ino;
		pend[i].replace = -1;
		pend[i].when = vd_now();
		strcpy(pend[i].name, name);
		replay(ino);
		return;
	default:
		syslog(LOG_ERR, "%s: special files are not supported over NFS",
		    name);
		return;
	}
	if (e) {
		syslog(LOG_ERR, "create %s: %s", name, nfs_strerror(e));
		return;
	}
	bindnew(ino, &fh, &a, dir);
}

/* A name disappeared from directory dir. */
static void
removed(dir, dfh, name, ino)
	unsigned dir, ino;
	struct nfsfh *dfh;
	char *name;
{
	struct irec *r;
	struct nfsfh tfh;
	int i, e;

	if (vd_debug)
		syslog(LOG_DEBUG, "dir %u: - %s ino %u", dir, name, ino);
	for (i = 0; i < NPEND; i++) {
		if (pend[i].dir == 0 || pend[i].ino != ino || pend[i].replace < 0)
			continue;
		/* The second half of a rename. */
		r = itab_get(pend[i].dir);
		if (r == 0)
			break;
		vd_recfh(r, &tfh);
		e = nfs_rename(&vd_mnt, dfh, name, &tfh, pend[i].name);
		if (e)
			syslog(LOG_ERR, "rename %s to %s: %s", name, pend[i].name,
			    nfs_strerror(e));
		r = itab_get(ino);
		if (r && (r->mode & IFMT) == IFDIR) {
			r->parent = (unsigned short)pend[i].dir;
			itab_put(ino, r);
		}
		if (pend[i].dir != dir)
			dirmtime(pend[i].dir);
		pend[i].dir = 0;
		if (e == 0 && pend[i].oldino && pend[i].oldino != ino)
			checkgone(pend[i].oldino);
		return;
	}
	r = itab_get(ino);
	if (r && (r->flags & IR_LOCAL)) {
		for (i = 0; i < NPEND; i++)
			if (pend[i].ino == ino)
				pend[i].dir = 0;
		return;			/* never reached the server */
	}
	if (r && (r->mode & IFMT) == IFDIR)
		e = nfs_rmdir(&vd_mnt, dfh, name);
	else
		e = nfs_remove(&vd_mnt, dfh, name);
	if (e && e != ENOENT)
		syslog(LOG_ERR, "remove %s: %s", name, nfs_strerror(e));
	else
		checkgone(ino);
}

/*
 * Make every chunk of a written directory block a valid record chain.
 * When direnter() shrinks a directory, itrunc() zeroes the rest of the
 * last block and writes it: those chunks are past the kernel's size, but
 * we report a larger size later, so they must read back as empty chunks.
 */
static void
dirfix(blk)
	u8 *blk;
{
	struct direct *d, *prev;
	unsigned c, off;

	for (c = 0; c < DEV_BSIZE; c += DIRBLKSIZ) {
		prev = 0;
		for (off = c; off < c + DIRBLKSIZ; off += d->d_reclen) {
			d = (struct direct *)(blk + off);
			if (d->d_reclen >= 8 && (d->d_reclen & 3) == 0 &&
			    off + d->d_reclen <= c + DIRBLKSIZ &&
			    d->d_namlen <= MAXNAMLEN &&
			    (d->d_ino == 0 || DIRSIZ(d) <= d->d_reclen))
				{ prev = d; continue; }
			if (prev)
				prev->d_reclen = (c + DIRBLKSIZ) - ((u8 *)prev - blk);
			else {
				memset((char *)d, 0, DIRBLKSIZ);
				d->d_reclen = DIRBLKSIZ;
			}
			break;
		}
	}
}

static int
dirwrite(dir, lbn, buf)
	unsigned dir, lbn;
	u8 *buf;
{
	static struct ent ov[MAXENT], nv[MAXENT];
	struct irec *r;
	struct nfsfh dfh;
	struct direct *d;
	char name[MAXNAMLEN + 1];
	int no, nn, i, j, ops, local;

	dirfix(buf);
	r = itab_get(dir);
	if (r == 0)
		return 0;
	local = (r->flags & IR_LOCAL) != 0;
	vd_recfh(r, &dfh);
	if (vd_dirblock(dir, lbn, oldblk) != 0 || local)
		memset((char *)oldblk, 0, DEV_BSIZE);
	no = local ? 0 : parse(oldblk, ov);
	nn = parse(buf, nv);
	ops = 0;
	if (!local) {
		/* names added or redirected */
		for (j = 0; j < nn; j++) {
			if (dotname(&nv[j]))
				continue;
			memcpy(name, nv[j].name, nv[j].namlen);
			name[nv[j].namlen] = 0;
			i = findname(ov, no, nv[j].name, nv[j].namlen);
			if (i < 0) {
				added(dir, &dfh, name, nv[j].ino, 0);
				ops++;
			} else if (ov[i].ino != nv[j].ino) {
				/* rename onto an existing name */
				added(dir, &dfh, name, nv[j].ino, ov[i].ino);
				ops++;
			}
		}
		/* names removed: the old record must still be in place */
		for (i = 0; i < no; i++) {
			if (dotname(&ov[i]))
				continue;
			if (findname(nv, nn, ov[i].name, ov[i].namlen) >= 0)
				continue;
			d = (struct direct *)(buf + ov[i].off);
			if (d->d_namlen != ov[i].namlen ||
			    memcmp(d->d_name, ov[i].name, ov[i].namlen) != 0) {
				syslog(LOG_WARNING, "dir %u: layout changed, %.*s kept",
				    dir, (int)ov[i].namlen, ov[i].name);
				continue;
			}
			memcpy(name, ov[i].name, ov[i].namlen);
			name[ov[i].namlen] = 0;
			removed(dir, &dfh, name, ov[i].ino);
			ops++;
		}
	}
	vd_dirupdate(dir, lbn, buf, (u32)0);
	if (ops)
		dirmtime(dir);
	return 0;
}

/* ---- dispatch ---- */

static int
blockwrite(ino, sub, buf, idx)
	unsigned ino, sub;
	u8 *buf;
	long idx;
{
	struct irec *r;
	struct dinode sh;
	unsigned mode;
	int e;

	r = itab_get(ino);
	if (r == 0)
		return 0;
	mode = r->mode & IFMT;
	if (r->flags & IR_LOCAL) {
		shadow(ino, &sh, 0);
		mode = sh.di_mode & IFMT;
	}
	if (sub >= VD_MAXLBN)
		return indwrite(ino, sub, buf);
	switch (mode) {
	case IFDIR:
		return dirwrite(ino, sub, buf);
	case IFLNK:
		e = linkwrite(ino, buf);
		break;
	default:
		e = datawrite(ino, sub, buf, idx);
		break;
	}
	if (e == EAGAIN && idx > 0)
		spool(idx, buf);
	return e == EAGAIN ? EAGAIN : 0;
}

static int
poolwrite(bn, buf)
	daddr_t bn;
	u8 *buf;
{
	struct pmap pm;
	struct nfsd_owner o;
	long idx;
	unsigned role;

	idx = poolidx(bn);
	pget(idx, &pm);
	if ((pm.flags & PM_MAPPED) == 0) {
		memset((char *)&o, 0, sizeof(o));
		o.no_blkno = bn;
		if (ioctl(ctl, NFSDIOCOWNER, (char *)&o) == 0 && o.no_ino) {
			if (o.no_slot >= 0)
				role = (unsigned)o.no_slot;
			else if (o.no_slot == -1)
				role = VD_IND1;
			else if (o.no_slot == -2)
				role = VD_IND2;
			else {
				syslog(LOG_ERR, "file too large for NFS disk");
				return EFBIG;
			}
			pm.ino = o.no_ino;
			pm.role = (unsigned short)role;
			pm.gen = (unsigned short)itab_gen(o.no_ino);
			pm.flags = PM_MAPPED;
			pput(idx, &pm);
		} else {
			spool(idx, buf);	/* owner learned later */
			return 0;
		}
	}
	if (pm.gen != itab_gen(pm.ino)) {
		/* a late write of a file that is gone; its number is reused */
		if (vd_debug)
			syslog(LOG_DEBUG, "block %ld: stale, ino %u reused",
			    (long)bn, pm.ino);
		if (pm.flags & PM_SPOOLED)
			unspool(idx);
		pm.flags = 0;
		pput(idx, &pm);
		return 0;
	}
	if (blockwrite(pm.ino, pm.role, buf, idx) == EAGAIN)
		return 0;
	if (pm.flags & PM_SPOOLED) {
		pm.flags &= ~PM_SPOOLED;
		pput(idx, &pm);
		unspool(idx);
	}
	return 0;
}

static void
inowrite(bn, buf)
	daddr_t bn;
	u8 *buf;
{
	struct dinode *nd, sh;
	struct irec *r;
	struct nfssattr sa;
	struct nfsfh fh;
	struct nfsattr a;
	unsigned ino, k;
	int i, e, wrote;

	wrote = 0;
	nd = (struct dinode *)buf;
	ino = (unsigned)((bn - 2) * INOPB + 1);
	for (i = 0; i < INOPB; i++, ino++, nd++) {
		shadow(ino, &sh, 0);
		if (memcmp((char *)&sh, (char *)nd, sizeof(sh)) == 0)
			continue;
		r = itab_get(ino);
		if (r == 0) {
			/* any free number may be reused, pool or not */
			if (nd->di_mode != 0 && nd->di_nlink > 0)
				itab_newlocal(ino, nd->di_mode, nd->di_uid,
				    nd->di_gid, nd->di_nlink);
		} else if (nd->di_mode == 0) {
			itab_drop(ino);		/* freed after its last unlink */
		} else if ((r->flags & IR_LOCAL) == 0 && nd->di_nlink > 0) {
			sa.mask = 0;
			if ((nd->di_mode & 07777) != (sh.di_mode & 07777) &&
			    (nd->di_mode & 07777) != (r->mode & 07777)) {
				sa.mask |= SA_MODE;
				sa.mode = (u32)(nd->di_mode & 07777);
			}
			/*
			 * A field changed if it differs both from what the
			 * kernel last had (the shadow) and from the server.
			 */
			if (sh.di_mode && nd->di_uid != sh.di_uid &&
			    nd->di_uid != r->uid) {
				sa.mask |= SA_UID;
				sa.uid = (u32)nd->di_uid;
			}
			if (sh.di_mode && nd->di_gid != sh.di_gid &&
			    nd->di_gid != r->gid) {
				sa.mask |= SA_GID;
				sa.gid = (u32)nd->di_gid;
			}
			(void)dataclear(ino);
			if ((nd->di_mode & IFMT) == IFREG && sh.di_mode &&
			    nd->di_size != sh.di_size &&
			    (u32)nd->di_size != r->size) {
				sa.mask |= SA_SIZE;
				sa.size.hi = 0;
				sa.size.lo = (u32)nd->di_size;
			}
			/*
			 * Mounted noatime, so only utimes() moves atime:
			 * both times changing means explicit times.
			 */
			if (sh.di_mode && nd->di_mtime != sh.di_mtime &&
			    nd->di_atime != sh.di_atime &&
			    (u32)nd->di_mtime != r->mtime) {
				sa.mask |= SA_MTIME | SA_ATIME;
				sa.mtime = (u32)nd->di_mtime;
				sa.atime = (u32)nd->di_atime;
			}
			if (sa.mask) {
				if (vd_debug)
					syslog(LOG_DEBUG, "setattr ino %u mask %o"
					    " mode %o uid %ld size %ld", ino, sa.mask,
					    (int)sa.mode, (long)sa.uid, (long)sa.size.lo);
				vd_recfh(r, &fh);
				a.type = 0;
				if ((e = nfs_setattr(&vd_mnt, &fh, &sa, &a)) != 0)
					syslog(LOG_WARNING,
					    "setattr ino %u mask %o: %s", ino,
					    sa.mask, nfs_strerror(e));
				r = itab_get(ino);
				if (r && a.type) {
					itab_setattr(r, &a);
					itab_put(ino, r);
				}
				if (sa.mask & SA_SIZE)
					vd_rcinval(ino);
			}
		}
		/*
		 * Record what the kernel now has before replaying any of its
		 * blocks: datawrite() clamps them to this size.
		 */
		shadow(ino, nd, 1);
		sizeok = ino;
		/* The block map names the owners of new blocks. */
		if (nd->di_mode != 0) {
			for (k = 0; k < NDADDR; k++)
				if (poolidx(nd->di_addr[k]) > 0)
					mapset(poolidx(nd->di_addr[k]), ino, k);
			if (poolidx(nd->di_addr[NDADDR]) > 0)
				mapset(poolidx(nd->di_addr[NDADDR]), ino, VD_IND1);
			if (poolidx(nd->di_addr[NDADDR + 1]) > 0)
				mapset(poolidx(nd->di_addr[NDADDR + 1]), ino, VD_IND2);
			replay(ino);	/* blocks spooled for this inode */
			redorun(ino);	/* blocks clamped to the old size */
		}
		sizeok = 0;
	}
}

int
vw_write(bn, buf)
	daddr_t bn;
	u8 *buf;
{
	struct irec *r;
	unsigned ino;

	if (bn < 2)
		return 0;
	if (bn < VD_ISIZE) {
		inowrite(bn, buf);
		return 0;
	}
	if (VD_ISPOOLBN(bn))
		return poolwrite(bn, buf);
	ino = VD_INO(bn);
	r = itab_get(ino);
	if (r && (r->flags & IR_NOALIAS)) {
		/* late write for an earlier file with this inode number */
		if (vd_debug)
			syslog(LOG_DEBUG, "block %ld: stale, ino %u reused",
			    (long)bn, ino);
		return 0;
	}
	blockwrite(ino, VD_SUB(bn), buf, -1L);
	return 0;
}

/* Kernel reads a pool block: a free-list chain, spooled data or an alias. */
int
vw_poolread(bn, buf)
	daddr_t bn;
	u8 *buf;
{
	struct pmap pm;
	struct fblk *fb;
	long idx;

	idx = poolidx(bn);
	memset((char *)buf, 0, DEV_BSIZE);
	if (idx <= 0)
		return 0;
	if (idx == chainidx) {
		fb = (struct fblk *)buf;
		fill(&fb->df_nfree, fb->df_free);
		return 0;
	}
	pget(idx, &pm);
	if (pm.flags & PM_SPOOLED)
		return fileat(spoolfd, idx * (long)DEV_BSIZE, (char *)buf,
		    DEV_BSIZE, 0) < 0 ? EIO : 0;
	if (pm.flags & PM_MAPPED)
		return vd_read(VD_BN(pm.ino, pm.role), buf);
	return 0;
}

/* Kernel was given these inodes: remember what it holds. */
void
vw_inoread(bn, buf)
	daddr_t bn;
	u8 *buf;
{
	struct dinode *dp;
	unsigned ino;
	int i;

	struct dinode sh;

	dp = (struct dinode *)buf;
	ino = (unsigned)((bn - 2) * INOPB + 1);
	for (i = 0; i < INOPB; i++, ino++, dp++) {
		/*
		 * An inode the kernel holds keeps the copy it has, not
		 * this one: leave its shadow alone.  (The inode being
		 * loaded is in the table too, but its shadow is either
		 * empty or what it got last time.)
		 */
		if (kernsize(ino) >= 0) {
			shadow(ino, &sh, 0);
			if (sh.di_mode != 0)
				continue;
		}
		shadow(ino, dp, 1);
	}
}

/* Nothing arrived for a while: settle names that were not renames. */
void
vw_idle()
{
	struct irec *r, *dr;
	struct nfsfh fh, dfh;
	int i, e;
	long t;

	t = (long)vd_now();
	for (i = 0; i < NPEND; i++) {
		if (pend[i].dir == 0 || t - (long)pend[i].when < 1)
			continue;
		if (pend[i].replace < 0) {
			if (t - (long)pend[i].when > 120) {
				syslog(LOG_ERR, "symlink %s: target never written",
				    pend[i].name);
				pend[i].dir = 0;
			}
			continue;
		}
		r = itab_get(pend[i].ino);
		if (r && (r->mode & IFMT) == IFDIR) {
			/* a directory only moves by rename: wait for it */
			if (t - (long)pend[i].when > 120) {
				syslog(LOG_ERR, "rename of %s never completed",
				    pend[i].name);
				pend[i].dir = 0;
			}
			continue;
		}
		dr = itab_get(pend[i].dir);
		if (r && dr) {
			vd_recfh(itab_get(pend[i].ino), &fh);
			vd_recfh(itab_get(pend[i].dir), &dfh);
			e = nfs_link(&vd_mnt, &fh, &dfh, pend[i].name);
			if (e)
				syslog(LOG_ERR, "link %s: %s", pend[i].name,
				    nfs_strerror(e));
			dirmtime(pend[i].dir);
		}
		pend[i].dir = 0;
	}
}
