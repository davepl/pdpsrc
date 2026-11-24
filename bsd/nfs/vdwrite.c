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

#define NSPOOL		512	/* blocks awaiting their file or its size */
#define NPEND		8	/* names awaiting a matching removal */
#define NDW		16	/* inodes with data written */
#define MAXENT		96	/* directory entries per block */

#define PM_MAPPED	1
#define PM_SPOOLED	2
#define PM_CHAIN	4
#define PM_ORPHAN	8		/* spooled with no known owner; may be stale */
#define PM_KEPT		16		/* indirect block: the spool has the kernel's copy */

struct pmap {			/* pool block map entry */
	unsigned short ino, role, flags;
	unsigned short gen;		/* generation of ino when mapped */
};

static int kinfd = -1, poolfd = -1, spoolfd = -1, indfd = -1, ctl = -1;
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

static u8 oldblk[DEV_BSIZE];

/*
 * The kernel's view of a file must not change under it.  While it writes
 * a file, blocks it has not flushed yet exist only in its buffer cache,
 * under the block numbers in its inode and indirect blocks.  If it reads
 * one of those back from us (its copy was flushed and then invalidated or
 * reused) and gets a map rebuilt from the server instead, the same file
 * block has two addresses: one buffer is read stale and the other later
 * overwrites newer data.  So an indirect block the kernel wrote is always
 * read back as written (in the spool for a pool block, PM_KEPT; in the
 * table below for an (ino, sub) block), and an inode is handed back with
 * the size and block map the kernel last wrote while the file is in use.
 */
#define NIK		16	/* (ino, sub) indirect blocks kept */
#define IKSECS		120
static struct ikeep {
	unsigned ino, sub, gen;
	u32 when;
} ik[NIK];

#define NRW		64	/* files written recently */
#define RWSECS		60
static struct rwrec {
	unsigned ino;
	u32 when;
} rw[NRW];

static void
rwmark(ino)
	unsigned ino;
{
	struct rwrec *rp, *old;
	int i;

	old = &rw[0];
	for (i = 0; i < NRW; i++) {
		rp = &rw[i];
		if (rp->ino == ino)
			break;
		if (rp->ino == 0 || rp->when < old->when)
			old = rp;
	}
	if (i == NRW)
		rp = old;
	rp->ino = ino;
	rp->when = vd_now();
}

static int
rwrecent(ino)
	unsigned ino;
{
	int i;

	for (i = 0; i < NRW; i++)
		if (rw[i].ino == ino)
			return vd_now() - rw[i].when <= RWSECS;
	return 0;
}

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
	    (e = openwork(dir, unit, "spool", &spoolfd)) != 0 ||
	    (e = openwork(dir, unit, "ind", &indfd)) != 0)
		return e;
	memset((char *)ik, 0, sizeof(ik));
	memset((char *)rw, 0, sizeof(rw));
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
	pm.flags &= ~PM_ORPHAN;
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
	if ((pm.flags & PM_MAPPED) == 0 || pm.ino != ino || pm.role != role ||
	    pm.gen != itab_gen(ino))
		pm.flags &= ~PM_KEPT;
	pm.ino = (unsigned short)ino;
	pm.role = (unsigned short)role;
	pm.gen = (unsigned short)itab_gen(ino);
	pm.flags |= PM_MAPPED;
	pm.flags &= ~PM_CHAIN;
	pput(idx, &pm);
	if ((pm.flags & (PM_SPOOLED|PM_ORPHAN)) == (PM_SPOOLED|PM_ORPHAN)) {
		struct irec *r;
		struct dinode sh;
		unsigned mode;

		/*
		 * The spooled copy may be the last write of a removed file.
		 * That is harmless in a regular file, whose own block follows
		 * if it has not come yet, but must never be parsed as a
		 * directory or become a symlink target.
		 */
		r = itab_get(ino);
		mode = r ? (r->mode & IFMT) : 0;
		if (r && (r->flags & IR_LOCAL)) {
			shadow(ino, &sh, 0);
			mode = sh.di_mode & IFMT;
		}
		if (mode != IFREG) {
			pm.flags &= ~(PM_SPOOLED|PM_ORPHAN);
			pput(idx, &pm);
			return;
		}
	}
	if (pm.flags & PM_SPOOLED) {
		u8 buf[DEV_BSIZE];

		fileat(spoolfd, idx * (long)DEV_BSIZE, (char *)buf, DEV_BSIZE, 0);
		if (blockwrite(ino, role, buf, idx) != EAGAIN) {
			pget(idx, &pm);
			pm.flags &= ~(PM_SPOOLED|PM_ORPHAN);
			pput(idx, &pm);
			unspool(idx);
		}
	}
}

/* ---- data, symlinks, indirect blocks ---- */

/*
 * Write file block lbn to the server, clamped to the file size the kernel
 * has.  If the kernel no longer has the inode in core, the shadow may lag
 * behind: the new size can still be in a delayed write of the inode block.
 * A pool block (idx) that reaches past the shadow's size is then written
 * clamped and EAGAIN is returned, so that the caller also spools it; it is
 * replayed in full when that inode block arrives (inowrite: replay).
 */
/*
 * Write-behind.  The kernel hands over one 1 KB block per request, and a
 * round trip to the server costs the same whatever its size (on a slow
 * or emulated machine, tens of milliseconds), so adjacent blocks of one
 * file are gathered and sent as one WRITE.  Everything else the daemon
 * does that could see the difference flushes first: any read, any inode,
 * directory, indirect or symlink block, and going idle.
 */
static struct {
	unsigned ino, gen;
	u32 off;
	unsigned len;			/* 0: empty */
	u8 data[NFS_IOSIZE];
} wb;

void
vw_flush()
{
	struct irec *r;
	struct nfsfh fh;
	struct nfsattr a;
	unsigned done, pos, ino;
	int e;

	if (wb.len == 0)
		return;
	ino = wb.ino;
	r = itab_get(ino);
	if (r == 0 || (r->flags & IR_LOCAL) || wb.gen != itab_gen(ino)) {
		wb.len = 0;		/* the file is gone */
		return;
	}
	vd_recfh(r, &fh);
	a.type = 0;
	for (pos = 0; pos < wb.len; pos += done) {
		a.type = 0;
		e = nfs_write(&vd_mnt, &fh, wb.off + pos, wb.len - pos,
		    wb.data + pos, &done, &a);
		if (e == 0 && done == 0)
			e = EIO;
		if (e) {
			syslog(LOG_ERR, "write ino %u: %s", ino, nfs_strerror(e));
			break;
		}
	}
	wb.len = 0;
	r = itab_get(ino);
	if (r && a.type) {
		itab_setattr(r, &a);
		itab_put(ino, r);
	}
	vd_rcinval(ino);
}

/* Anything waiting to be sent? */
int
vw_dirty()
{
	return wb.len != 0;
}

static int
datawrite(ino, lbn, buf, idx)
	unsigned ino, lbn;
	u8 *buf;
	long idx;
{
	struct irec *r;
	struct dinode sh;
	long size;
	u32 off;
	unsigned n;
	int keep;

	r = itab_get(ino);
	if (r == 0)
		return 0;		/* file is gone */
	if (r->flags & IR_LOCAL)
		return EAGAIN;		/* not created on the server yet */
	size = kernsize(ino);
	off = (u32)lbn * DEV_BSIZE;
	keep = 0;
	if (size < 0) {
		shadow(ino, &sh, 0);
		size = sh.di_mode ? sh.di_size : (long)r->size;
		if (idx > 0 && ino != sizeok && (long)off + DEV_BSIZE > size) {
			if (vd_debug)
				syslog(LOG_DEBUG, "ino %u lbn %u: kept for "
				    "inode (size %ld)", ino, lbn, size);
			keep = EAGAIN;
		}
	}
	if ((long)off >= size)
		return keep;		/* beyond the kernel's end of file */
	n = DEV_BSIZE;
	if (size - (long)off < (long)DEV_BSIZE)
		n = (unsigned)(size - (long)off);
	if (wb.len && (wb.ino != ino || wb.gen != itab_gen(ino) ||
	    wb.off + wb.len != off || wb.len + n > NFS_IOSIZE))
		vw_flush();
	if (wb.len == 0) {
		wb.ino = ino;
		wb.gen = itab_gen(ino);
		wb.off = off;
	}
	memcpy((char *)wb.data + wb.len, (char *)buf, n);
	wb.len += n;
	if (wb.len == NFS_IOSIZE || n < DEV_BSIZE)
		vw_flush();		/* full, or the end of the file */
	datamark(ino);
	rwmark(ino);
	return keep;
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
indwrite(ino, sub, buf, idx)
	unsigned ino, sub;
	u8 *buf;
	long idx;
{
	struct pmap pm;
	struct ikeep *kp, *old;
	daddr_t *ap;
	u32 base;
	unsigned j, k;
	int i;

	/* Keep the kernel's copy: it is what a later read must return. */
	if (idx > 0) {
		fileat(spoolfd, idx * (long)DEV_BSIZE, (char *)buf, DEV_BSIZE, 1);
		pget(idx, &pm);
		pm.flags |= PM_KEPT;
		pput(idx, &pm);
	} else {
		old = &ik[0];
		for (i = 0; i < NIK; i++) {
			kp = &ik[i];
			if (kp->ino == ino && kp->sub == sub)
				break;
			if (kp->ino == 0 || kp->when < old->when)
				old = kp;
		}
		if (i == NIK)
			kp = old;
		kp->ino = ino;
		kp->sub = sub;
		kp->gen = itab_gen(ino);
		kp->when = vd_now();
		fileat(indfd, (long)(kp - ik) * (long)DEV_BSIZE, (char *)buf,
		    DEV_BSIZE, 1);
	}
	rwmark(ino);
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

/* The kernel's copy of indirect block (ino, sub), if it wrote one lately. */
int
vw_indread(ino, sub, buf)
	unsigned ino, sub;
	u8 *buf;
{
	struct ikeep *kp;
	int i;

	for (i = 0; i < NIK; i++) {
		kp = &ik[i];
		if (kp->ino != ino || kp->sub != sub)
			continue;
		if (kp->gen != itab_gen(ino) || vd_now() - kp->when > IKSECS) {
			kp->ino = 0;
			return 0;
		}
		return fileat(indfd, (long)i * (long)DEV_BSIZE, (char *)buf,
		    DEV_BSIZE, 0) >= 0;
	}
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

/*
 * Replay blocks of ino that were spooled before it reached the server, or
 * before the kernel's size for it did.  Entries leave the list as they are
 * replayed (the last one moves into their place), so step only past those
 * that stay.
 */
static void
replay(ino)
	unsigned ino;
{
	struct pmap pm;
	long idx;
	int i;

	for (i = 0; i < nspooled; ) {
		idx = spooled[i];
		pget(idx, &pm);
		if ((pm.flags & PM_MAPPED) && pm.ino == ino &&
		    pm.gen != itab_gen(ino)) {
			/* data of an earlier file with this number */
			pm.flags = 0;
			pput(idx, &pm);
			unspool(idx);
		} else if ((pm.flags & PM_MAPPED) && pm.ino == ino &&
		    (pm.flags & PM_SPOOLED)) {
			pm.flags &= ~PM_MAPPED;
			pput(idx, &pm);
			mapset(idx, ino, pm.role);
		}
		if (i < nspooled && spooled[i] == idx)
			i++;
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

/*
 * A file removed while a process still has it open (ld and many others
 * create a scratch file and unlink it at once) must stay usable until the
 * last close.  On the server it is renamed out of the way instead, and
 * removed when the kernel frees the inode.
 */
#define NSILLY		16
static struct silly {
	unsigned ino, dir;
	char name[20];
} silly[NSILLY];
static unsigned sillyseq;

/* Reference count of the kernel's in-core inode, -1 if not in core. */
static int
kerncount(ino)
	unsigned ino;
{
	struct nfsd_owner o;

	memset((char *)&o, 0, sizeof(o));
	o.no_blkno = 0;
	o.no_ino = (ino_t)ino;
	if (ioctl(ctl, NFSDIOCOWNER, (char *)&o) < 0 || o.no_ino == 0)
		return -1;
	return (int)o.no_count;
}

/* The kernel is done with ino: remove what was kept for it. */
static void
sillydone(ino)
	unsigned ino;
{
	struct irec *r;
	struct nfsfh dfh;
	unsigned dir;
	int i, e;

	for (i = 0; i < NSILLY; i++) {
		if (silly[i].ino != ino)
			continue;
		dir = silly[i].dir;
		silly[i].ino = 0;
		r = itab_get(dir);
		if (r == 0)
			continue;
		vd_recfh(r, &dfh);
		e = nfs_remove(&vd_mnt, &dfh, silly[i].name);
		if (e && e != ENOENT)
			syslog(LOG_ERR, "remove %s: %s", silly[i].name,
			    nfs_strerror(e));
		if (vd_debug)
			syslog(LOG_DEBUG, "ino %u closed: %s removed", ino,
			    silly[i].name);
	}
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
	/*
	 * unlink() itself holds one reference while it writes the
	 * directory; any more means the file is open somewhere.
	 */
	if (r && (r->mode & IFMT) != IFDIR && r->nlink <= 1 &&
	    kerncount(ino) >= 2) {
		for (i = 0; i < NSILLY && silly[i].ino; i++)
			;
		if (i < NSILLY) {
			sprintf(silly[i].name, ".nfs%u.%u", ino, ++sillyseq);
			vw_flush();
			e = nfs_rename(&vd_mnt, dfh, name, dfh, silly[i].name);
			if (e == 0) {
				silly[i].ino = ino;
				silly[i].dir = dir;
				if (vd_debug)
					syslog(LOG_DEBUG, "ino %u open: %s kept "
					    "as %s", ino, name, silly[i].name);
				return;
			}
		} else
			syslog(LOG_WARNING, "%s removed while open; too many "
			    "such files to keep it", name);
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
	if (sub >= VD_MAXLBN) {
		vw_flush();
		return indwrite(ino, sub, buf, idx);
	}
	switch (mode) {
	case IFDIR:
		vw_flush();
		return dirwrite(ino, sub, buf);
	case IFLNK:
		vw_flush();
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

/* The kernel's copy of indirect block bn into buf, if we have it. */
static int
keptind(bn, ino, sub, buf)
	daddr_t bn;
	unsigned ino, sub;
	u8 *buf;
{
	struct pmap pm;
	long idx;

	if (bn == 0)
		return 0;
	idx = poolidx(bn);
	if (idx <= 0)
		return vw_indread(ino, sub, buf);
	pget(idx, &pm);
	if ((pm.flags & PM_MAPPED) == 0 || (pm.flags & PM_KEPT) == 0 ||
	    pm.ino != ino || pm.role != sub || pm.gen != itab_gen(ino))
		return 0;
	return fileat(spoolfd, idx * (long)DEV_BSIZE, (char *)buf,
	    DEV_BSIZE, 0) >= 0;
}

/*
 * Does the file a map entry names still have block bn there, as far as
 * the kernel has told us?  The kernel reuses freed blocks at once, and a
 * write for the new owner can come while that file's inode is out of core
 * and before the inode or indirect block that names the block.  "No" is
 * always safe: the block is kept until its owner is named.
 */
static int
mapvalid(pm, bn)
	struct pmap *pm;
	daddr_t bn;
{
	static u8 ib[DEV_BSIZE];
	struct dinode sh;
	daddr_t *ap;
	u32 lbn, k;

	if (itab_get(pm->ino) == 0 || pm->gen != itab_gen(pm->ino))
		return 0;
	shadow(pm->ino, &sh, 0);
	if (sh.di_mode == 0)
		return 0;
	ap = (daddr_t *)ib;
	if (pm->role == VD_IND1)
		return sh.di_addr[NDADDR] == bn;
	if (pm->role == VD_IND2)
		return sh.di_addr[NDADDR + 1] == bn;
	if (pm->role > VD_MAXLBN) {		/* single under the double */
		if (sh.di_addr[NDADDR + 1] == 0)
			return 0;
		if (!keptind(sh.di_addr[NDADDR + 1], pm->ino, VD_IND2, ib))
			return 1;
		return ap[VD_IND2 - 1 - pm->role] == bn;
	}
	lbn = pm->role;
	if (lbn < NDADDR)
		return sh.di_addr[lbn] == bn;
	if (lbn < NDADDR + NINDIR) {
		if (sh.di_addr[NDADDR] == 0)
			return 0;
		if (!keptind(sh.di_addr[NDADDR], pm->ino, VD_IND1, ib))
			return 1;
		return ap[lbn - NDADDR] == bn;
	}
	k = (lbn - NDADDR - NINDIR) / NINDIR;
	if (!keptind(sh.di_addr[NDADDR + 1], pm->ino, VD_IND2, ib))
		return 1;
	if (!keptind(ap[k], pm->ino, (unsigned)(VD_IND2 - 1 - k), ib))
		return 1;
	return ap[(lbn - NDADDR - NINDIR) % NINDIR] == bn;
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
	/*
	 * Ask the kernel first.  The map entry may be left from a removed
	 * file: the kernel reuses freed blocks at once (compiler temporary
	 * files), and the data of the new file can come before the inode
	 * block that names the block.
	 */
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
		if ((pm.flags & PM_MAPPED) == 0 || pm.ino != o.no_ino ||
		    pm.role != role || pm.gen != itab_gen(o.no_ino)) {
			if ((pm.flags & PM_MAPPED) && (pm.flags & PM_SPOOLED))
				unspool(idx);
			pm.ino = o.no_ino;
			pm.role = (unsigned short)role;
			pm.gen = (unsigned short)itab_gen(o.no_ino);
			pm.flags = PM_MAPPED;
			pput(idx, &pm);
		}
	} else if ((pm.flags & PM_MAPPED) == 0) {
		spool(idx, buf);	/* owner learned later */
		return 0;
	} else if (!mapvalid(&pm, bn)) {
		/*
		 * The mapped file no longer has the block and no inode in
		 * core has it: either a late write of that file, or the first
		 * write of a new one whose inode block is still to come.
		 * Keep it.
		 */
		if (vd_debug)
			syslog(LOG_DEBUG, "block %ld: no longer in ino %u, kept",
			    (long)bn, pm.ino);
		if (pm.flags & PM_SPOOLED)
			unspool(idx);
		pm.flags = 0;
		pput(idx, &pm);
		spool(idx, buf);
		pget(idx, &pm);
		pm.flags |= PM_ORPHAN;
		pput(idx, &pm);
		return 0;
	}
	if (blockwrite(pm.ino, pm.role, buf, idx) == EAGAIN)
		return 0;
	if (pm.flags & PM_SPOOLED) {
		pm.flags &= ~(PM_SPOOLED|PM_ORPHAN);
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
	long ks;
	int i, e, wrote;

	wrote = 0;
	nd = (struct dinode *)buf;
	ino = (unsigned)((bn - 2) * INOPB + 1);
	for (i = 0; i < INOPB; i++, ino++, nd++) {
		shadow(ino, &sh, 0);
		if (memcmp((char *)&sh, (char *)nd, sizeof(sh)) == 0)
			continue;
		ks = kernsize(ino);
		r = itab_get(ino);
		if (r == 0) {
			/* any free number may be reused, pool or not */
			if (nd->di_mode != 0 && nd->di_nlink > 0)
				itab_newlocal(ino, nd->di_mode, nd->di_uid,
				    nd->di_gid, nd->di_nlink);
		} else if (nd->di_mode == 0) {
			sillydone(ino);
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
			/*
			 * Not while the kernel holds the inode with another
			 * size: this copy is then an old one, written back
			 * with a neighbour's inode, and setting its size
			 * would cut off data written since.
			 */
			if ((nd->di_mode & IFMT) == IFREG && sh.di_mode &&
			    nd->di_size != sh.di_size &&
			    (u32)nd->di_size != r->size &&
			    (ks < 0 || ks == nd->di_size)) {
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
				e = nfs_setattr(&vd_mnt, &fh, &sa, &a);
				if (e && (sa.mask & (SA_UID | SA_GID)) &&
				    (sa.mask & ~(SA_UID | SA_GID))) {
					/*
					 * The server refuses the change of
					 * owner (we are not root there); do
					 * not lose the mode or size with it.
					 */
					sa.mask &= ~(SA_UID | SA_GID);
					a.type = 0;
					e = nfs_setattr(&vd_mnt, &fh, &sa, &a);
				}
				if (e != 0)
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
		if ((nd->di_mode & IFMT) == IFREG)
			rwmark(ino);
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
		}
		sizeok = 0;
		/*
		 * A file the kernel wrote carries the kernel's modification
		 * time, as on a local disk, not the server's time of the
		 * last WRITE: the two clocks differ, and ld compares an
		 * archive's time with the one ranlib stored inside it.
		 * Last, so that no WRITE of ours moves it again.
		 */
		if ((nd->di_mode & IFMT) == IFREG && nd->di_nlink > 0 &&
		    nd->di_mtime != 0 && (ks < 0 || ks == nd->di_size)) {
			vw_flush();
			r = itab_get(ino);
			if (r && (r->flags & IR_LOCAL) == 0 &&
			    rwrecent(ino) && (u32)nd->di_mtime != r->mtime) {
				sa.mask = SA_MTIME;
				sa.mtime = (u32)nd->di_mtime;
				vd_recfh(r, &fh);
				a.type = 0;
				if (nfs_setattr(&vd_mnt, &fh, &sa, &a) == 0) {
					r = itab_get(ino);
					if (r && a.type) {
						itab_setattr(r, &a);
						itab_put(ino, r);
					}
				}
			}
		}
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
		vw_flush();
		inowrite(bn, buf);
		vw_flush();
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
	if ((pm.flags & PM_SPOOLED) ||
	    ((pm.flags & PM_MAPPED) && (pm.flags & PM_KEPT) &&
	    pm.role >= VD_MAXLBN && pm.gen == itab_gen(pm.ino)))
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
	struct dinode sh;
	struct irec *r;
	unsigned ino;
	int i, k, pool;

	dp = (struct dinode *)buf;
	ino = (unsigned)((bn - 2) * INOPB + 1);
	for (i = 0; i < INOPB; i++, ino++, dp++) {
		shadow(ino, &sh, 0);
		/*
		 * A regular file the kernel wrote: keep its block map, and
		 * while it is in use its size too (the server may not have
		 * all of it yet).  See the comment at ik[].
		 */
		if (sh.di_mode != 0 && (sh.di_mode & IFMT) == IFREG &&
		    (dp->di_mode & IFMT) == IFREG && sh.di_nlink > 0) {
			pool = 0;
			for (k = 0; k < NDADDR + 2; k++)
				if (poolidx(sh.di_addr[k]) > 0)
					pool = 1;
			r = itab_get(ino);
			if (r && (r->flags & IR_LOCAL))
				*dp = sh;	/* exists only here so far */
			else if (pool && (rwrecent(ino) ||
			    sh.di_size == dp->di_size)) {
				dp->di_size = sh.di_size;
				for (k = 0; k < NDADDR + NIADDR; k++)
					dp->di_addr[k] = sh.di_addr[k];
			}
		}
		/*
		 * An inode the kernel holds keeps the copy it has, not
		 * this one: leave its shadow alone.  (The inode being
		 * loaded is in the table too, but its shadow is either
		 * empty or what it got last time.)
		 */
		if (kernsize(ino) >= 0 && sh.di_mode != 0)
			continue;
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

	vw_flush();
	/* A kept file whose inode left the kernel without a freeing write. */
	for (i = 0; i < NSILLY; i++)
		if (silly[i].ino && kerncount(silly[i].ino) < 0)
			sillydone(silly[i].ino);
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
