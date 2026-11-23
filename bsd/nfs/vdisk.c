/*
 * Virtual UFS disk: produce 2.11BSD filesystem blocks from an NFS export.
 * See vd.h for the block layout.
 */
#include "vd.h"

struct nfsmount vd_mnt;
int vd_readonly = 1;
int vd_debug;
int vd_actimeo = 5;
int vd_ctlfd = -1;		/* seconds attributes and directories stay fresh */

#define DIRSLACK	DIRBLKSIZ	/* empty space after each directory */
#define NDSLOT		64		/* cached directory images */
#define DSLOTMAX	(128L * 1024)	/* largest directory image (bytes) */
#define KWSECS		8		/* kernel-written image kept at least this long */
#define RCHUNK		NFS_IOSIZE	/* read cache granule */

static char tmpdir[64];
static int unitno;

/* ---- helpers ---- */

u32
vd_now()
{
	return (u32)time((time_t *)0);
}

void
vd_recfh(r, fh)
	struct irec *r;
	struct nfsfh *fh;
{
	fh->len = r->fhlen;
	memcpy((char *)fh->data, (char *)r->fh, r->fhlen);
}

/* Refresh the attributes of ino if they are older than the cache time. */
struct irec *
vd_freshrec(ino, force)
	unsigned ino;
	int force;
{
	struct irec *r;
	struct nfsfh fh;
	struct nfsattr a;
	int e;

	r = itab_get(ino);
	if (r == 0 || (r->flags & IR_LOCAL))
		return r;
	if (!force && (r->flags & IR_ATTR) &&
	    vd_now() - r->attime < (u32)vd_actimeo)
		return r;
	vd_recfh(r, &fh);
	e = nfs_getattr(&vd_mnt, &fh, &a);
	r = itab_get(ino);		/* cache slot may have moved */
	if (e == 0) {
		itab_setattr(r, &a);
		itab_put(ino, r);
	} else if (rpc_verbose)
		syslog(LOG_DEBUG, "getattr ino %u: %s", ino, nfs_strerror(e));
	return r;
}

static struct rcache {
	unsigned ino;
	u32 off;
	unsigned len;
	u32 when;
	u8 data[RCHUNK];
} rc;

/* ---- directory images ---- */

static struct dslot {
	unsigned ino;
	u32 size;		/* bytes, multiple of DIRBLKSIZ */
	u32 built;		/* time built */
	u32 mtime;		/* directory mtime when built */
	unsigned age;
	int kern;		/* holds blocks written by the kernel */
	u32 kwhen;		/* time of the kernel's last write */
} dslot[NDSLOT];
static unsigned dage;
static int dfd = -1;		/* all images: slot i at i * DSLOTMAX */

#define DBASE(ds)	((long)((ds) - dslot) * DSLOTMAX)

/*
 * On a read-write mount the kernel acts on what it read from a directory:
 * namei() scans a block for free space, may sleep (allocating the inode),
 * re-reads the block in direnter() and expects the same layout ("wdir:
 * compact" panics otherwise).  So the image of a directory is never
 * replaced or evicted while the kernel has that directory's inode in core.
 * Callers first drop the kernel's clean cached blocks and unreferenced
 * inodes (NFSDIOCINVAL), so only directories really in use remain.
 * Returns 1 if ino is in core; *cntp gets its reference count.
 */
static int
dincore(ino, cntp)
	unsigned ino;
	int *cntp;
{
	struct nfsd_owner o;

	memset((char *)&o, 0, sizeof(o));
	o.no_blkno = 0;
	o.no_ino = (ino_t)ino;
	if (vd_ctlfd < 0 || ioctl(vd_ctlfd, NFSDIOCOWNER, (char *)&o) < 0 ||
	    o.no_ino == 0)
		return 0;
	if (cntp)
		*cntp = o.no_count;
	return 1;
}

/*
 * Can the image in ds be thrown away?  level 0: not in core at all;
 * level 1: in core but unreferenced (only waiting for its inode write).
 * An image the kernel wrote recently is always kept: the kernel may
 * still hold a delayed write of it (itrunc() zeroes the freed tail of a
 * directory block with bdwrite()), which INVAL cannot drop.  The sync
 * helper pushes delayed writes every two seconds.
 */
static int
dcandrop(ds, level)
	struct dslot *ds;
	int level;
{
	unsigned ino;
	int cnt;

	ino = ds->ino;
	if (ino == 0 || vd_readonly)
		return 1;
	if (ds->kern && vd_now() - ds->kwhen < (u32)KWSECS)
		return 0;
	cnt = 0;
	if (!dincore(ino, &cnt))
		return 1;
	return level > 0 && cnt == 0;
}

/* Forget the image in ds; its size must then come from a rebuild. */
static void
dforget(ds)
	struct dslot *ds;
{
	struct irec *r;

	if (ds->ino && (r = itab_get(ds->ino)) != 0 &&
	    (r->flags & IR_DSIZE)) {
		r->flags &= ~IR_DSIZE;
		itab_put(ds->ino, r);
	}
	ds->ino = 0;
}

/*
 * Slot for the image of ino: its current slot, an empty one, or the
 * least recently used one that may be dropped.  0 if none (errno set).
 */
static struct dslot *
dslotfor(ino)
	unsigned ino;
{
	struct dslot *ds, *best;
	char path[96];
	int i, level;

	best = 0;
	for (i = 0; i < NDSLOT; i++) {
		ds = &dslot[i];
		if (ds->ino == ino)
			return ds;
		if (ds->ino == 0 && (best == 0 || ds->age < best->age))
			best = ds;
	}
	if (best == 0 && !vd_readonly && vd_ctlfd >= 0)
		ioctl(vd_ctlfd, NFSDIOCINVAL, (char *)0);
	for (level = 0; best == 0 && level < 2; level++)
		for (i = 0; i < NDSLOT; i++) {
			ds = &dslot[i];
			if ((best == 0 || ds->age < best->age) &&
			    dcandrop(ds, level))
				best = ds;
		}
	if (best == 0) {
		syslog(LOG_WARNING, "all %d directory images in use", NDSLOT);
		errno = ENFILE;
		return (struct dslot *)0;
	}
	ds = best;
	if (vd_debug && ds->ino)
		syslog(LOG_DEBUG, "dir %u: image dropped for %u", ds->ino, ino);
	dforget(ds);
	if (dfd < 0) {
		sprintf(path, "%s/.nfsdir%d", tmpdir, unitno);
		dfd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
		unlink(path);
		if (dfd < 0)
			return (struct dslot *)0;
	}
	return ds;
}

struct dbuild {
	struct dslot *ds;
	unsigned dirino;
	char chunk[DIRBLKSIZ];
	unsigned used;		/* bytes used in chunk */
	unsigned last;		/* offset of last entry in chunk */
	int err;
};

static void
dflush(b)
	struct dbuild *b;
{
	struct direct *d;

	if (b->used == 0)
		return;
	/* The last entry absorbs the rest of the chunk. */
	d = (struct direct *)(b->chunk + b->last);
	d->d_reclen += DIRBLKSIZ - b->used;
	if (b->ds->size + DIRBLKSIZ > DSLOTMAX) {
		b->err = EFBIG;
		b->used = 0;
		return;
	}
	if (lseek(dfd, (off_t)(DBASE(b->ds) + (long)b->ds->size), 0) < 0 ||
	    write(dfd, b->chunk, DIRBLKSIZ) != DIRBLKSIZ)
		b->err = EIO;
	b->ds->size += DIRBLKSIZ;
	b->used = 0;
}

static void
dadd(b, ino, name)
	struct dbuild *b;
	unsigned ino;
	char *name;
{
	struct direct de, *d;
	unsigned len, rec;

	len = strlen(name);
	if (len > MAXNAMLEN)
		return;
	de.d_namlen = len;
	rec = DIRSIZ(&de);
	if (b->used + rec > DIRBLKSIZ)
		dflush(b);
	d = (struct direct *)(b->chunk + b->used);
	memset((char *)d, 0, rec);
	d->d_ino = (ino_t)ino;
	d->d_reclen = rec;
	d->d_namlen = len;
	memcpy(d->d_name, name, len);
	b->last = b->used;
	b->used += rec;
}

static int
dentry(arg, name, fileid, a, fh)
	void *arg;
	char *name;
	struct u64 *fileid;
	struct nfsattr *a;
	struct nfsfh *fh;
{
	struct dbuild *b;
	unsigned ino;

	b = (struct dbuild *)arg;
	if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
		return 0;
	if (strlen(name) > MAXNAMLEN || strchr(name, '/'))
		return 0;
	ino = itab_enter(fileid, fh, a, b->dirino);
	if (ino)
		dadd(b, ino, name);
	return 0;
}

/* Build (or rebuild) the image of directory ino; returns its slot. */
static struct dslot *
dbuild(ino, ep)
	unsigned ino;
	int *ep;
{
	static struct dbuild b;
	struct dslot *ds;
	struct irec *r;
	struct nfsfh fh;
	struct nfsattr da;
	unsigned parent;
	int e;

	if ((ds = dslotfor(ino)) == 0) {
		*ep = errno ? errno : EIO;
		return (struct dslot *)0;
	}
	r = itab_get(ino);
	if (r == 0) {
		*ep = ESTALE;
		return (struct dslot *)0;
	}
	vd_recfh(r, &fh);
	parent = r->parent ? r->parent : ROOTINO;
	ds->ino = 0;
	ds->size = 0;
	ds->kern = 0;
	b.ds = ds;
	b.dirino = ino;
	b.used = 0;
	b.err = 0;
	dadd(&b, ino, ".");
	dadd(&b, parent, "..");
	e = nfs_readdirplus(&vd_mnt, &fh, dentry, (void *)&b, &da);
	if (e == 0) {
		dflush(&b);
		e = b.err;
	}
	if (e) {
		*ep = e;
		return (struct dslot *)0;
	}
	ds->ino = ino;
	ds->built = vd_now();
	ds->age = ++dage;
	r = itab_get(ino);
	if (da.type == NF3DIR) {
		itab_setattr(r, &da);
		ds->mtime = da.mtime;
	} else
		ds->mtime = r->mtime;
	r->dsize = ds->size;
	r->dmtime = ds->mtime;
	r->flags |= IR_DSIZE;
	itab_put(ino, r);
	return ds;
}

/* Find a current image of directory ino, building one if needed. */
static struct dslot *
dimage(ino, revalidate, ep)
	unsigned ino;
	int revalidate;
	int *ep;
{
	struct dslot *ds;
	struct irec *r;
	int i;

	for (i = 0; i < NDSLOT; i++) {
		ds = &dslot[i];
		if (ds->ino != ino)
			continue;
		/* Read-write: images change only through the kernel's own
		 * writes; remote changes are picked up by drefresh(). */
		if (!revalidate || !vd_readonly ||
		    vd_now() - ds->built < (u32)vd_actimeo) {
			ds->age = ++dage;
			return ds;
		}
		r = vd_freshrec(ino, 1);
		if (r == 0 || r->mtime == ds->mtime) {
			ds->built = vd_now();
			ds->age = ++dage;
			return ds;
		}
		if (!vd_readonly && vd_ctlfd >= 0)
			ioctl(vd_ctlfd, NFSDIOCINVAL, (char *)0);
		break;
	}
	return dbuild(ino, ep);
}

/* Directory size to report in the inode. */
static u32
dirsize(ino, r)
	unsigned ino;
	struct irec *r;
{
	struct dslot *ds;
	long ks;
	int e, i;

	for (i = 0; i < NDSLOT; i++) {
		if (dslot[i].ino != ino)
			continue;
		/*
		 * The kernel's cached copy of a block it wrote has zeroed
		 * chunks past the size it had, so report exactly that size.
		 */
		if (!vd_readonly && dslot[i].kern && (ks = vw_ksize(ino)) >= 0)
			return (u32)ks;
		if (dslot[i].mtime == r->mtime || !vd_readonly)
			return dslot[i].size + DIRSLACK;
	}
	if ((r->flags & IR_DSIZE) && r->dmtime == r->mtime)
		return r->dsize + DIRSLACK;
	ds = dbuild(ino, &e);
	if (ds == 0)
		return DIRBLKSIZ * 2;
	return ds->size + DIRSLACK;
}

static int
dread(ino, lbn, buf)
	unsigned ino;
	unsigned lbn;
	u8 *buf;
{
	struct dslot *ds;
	struct direct *d;
	u32 off;
	int e, n, i;

	e = 0;
	ds = dimage(ino, lbn == 0, &e);
	if (ds == 0)
		return e ? e : EIO;
	off = (u32)lbn * DEV_BSIZE;
	n = 0;
	if (off < ds->size) {
		n = ds->size - off < DEV_BSIZE ? (int)(ds->size - off) : DEV_BSIZE;
		if (lseek(dfd, (off_t)(DBASE(ds) + (long)off), 0) < 0 ||
		    (n = read(dfd, (char *)buf, n)) < 0)
			n = 0;
		n &= ~(DIRBLKSIZ - 1);
	}
	/* Pad with empty directory chunks. */
	for (i = n; i < DEV_BSIZE; i += DIRBLKSIZ) {
		d = (struct direct *)(buf + i);
		memset((char *)d, 0, DIRBLKSIZ);
		d->d_ino = 0;
		d->d_reclen = DIRBLKSIZ;
	}
	return 0;
}

/*
 * Image access for directory writes: old contents of block lbn, and
 * replacement of that block with what the kernel wrote.  The image of a
 * directory created by the kernel starts from its first written block.
 */
int
vd_dirblock(ino, lbn, buf)
	unsigned ino, lbn;
	u8 *buf;
{
	return dread(ino, lbn, buf);
}

int
vd_dirupdate(ino, lbn, buf, mtime)
	unsigned ino, lbn;
	u8 *buf;
	u32 mtime;
{
	struct dslot *ds;
	struct irec *r;
	u32 off, end;
	int e;

	if ((ds = dslotfor(ino)) == 0)
		return errno ? errno : EIO;
	if (ds->ino != ino) {
		/* new directory */
		ds->ino = ino;
		ds->size = 0;
		r = itab_get(ino);
		ds->mtime = r ? r->mtime : 0;
	}
	off = (u32)lbn * DEV_BSIZE;
	end = off + DEV_BSIZE;
	if (end > (u32)DSLOTMAX) {
		syslog(LOG_ERR, "dir %u: larger than %ld bytes", ino, DSLOTMAX);
		return EFBIG;
	}
	if (ds->size < off) {
		/* blocks the image never had: empty chunks */
		u8 eb[DIRBLKSIZ];
		u32 o;

		memset((char *)eb, 0, DIRBLKSIZ);
		((struct direct *)eb)->d_reclen = DIRBLKSIZ;
		for (o = ds->size; o < off; o += DIRBLKSIZ) {
			lseek(dfd, (off_t)(DBASE(ds) + (long)o), 0);
			write(dfd, (char *)eb, DIRBLKSIZ);
		}
	}
	if (lseek(dfd, (off_t)(DBASE(ds) + (long)off), 0) < 0 ||
	    write(dfd, (char *)buf, DEV_BSIZE) != DEV_BSIZE)
		return EIO;
	if (end > ds->size)
		ds->size = end;
	ds->kern = 1;
	ds->kwhen = vd_now();
	ds->built = ds->kwhen;
	ds->age = ++dage;
	if (mtime)
		ds->mtime = mtime;
	e = 0;
	return e;
}

/* Directory changed by us on the server: keep our image, note mtime. */
void
vd_dirtouch(ino, mtime)
	unsigned ino;
	u32 mtime;
{
	int i;

	for (i = 0; i < NDSLOT; i++)
		if (dslot[i].ino == ino) {
			dslot[i].mtime = mtime;
			dslot[i].built = vd_now();
		}
}

void
vd_rcinval(ino)
	unsigned ino;
{
	if (rc.ino == ino)
		rc.ino = 0;
}

/* ---- file data ---- */


static int
fread_block(ino, r, lbn, buf)
	unsigned ino;
	struct irec *r;
	unsigned lbn;
	u8 *buf;
{
	struct nfsfh fh;
	u32 off, base;
	unsigned got, n;
	int e, eof;

	off = (u32)lbn * DEV_BSIZE;
	memset((char *)buf, 0, DEV_BSIZE);
	if (off >= r->size)
		return 0;
	base = off & ~(u32)(RCHUNK - 1);
	if (rc.ino != ino || rc.off != base || vd_now() - rc.when > (u32)vd_actimeo) {
		vd_recfh(r, &fh);
		rc.ino = 0;
		e = nfs_read(&vd_mnt, &fh, base, RCHUNK, rc.data, &got, &eof,
		    (struct nfsattr *)0);
		if (e)
			return e;
		rc.ino = ino;
		rc.off = base;
		rc.len = got;
		rc.when = vd_now();
	}
	if (off - base >= rc.len)
		return 0;
	n = rc.len - (unsigned)(off - base);
	if (n > DEV_BSIZE)
		n = DEV_BSIZE;
	memcpy((char *)buf, (char *)rc.data + (off - base), n);
	return 0;
}

static int
lread(r, buf)
	struct irec *r;
	u8 *buf;
{
	struct nfsfh fh;
	char target[NFS_PATHMAX];
	int e;
	unsigned n;

	memset((char *)buf, 0, DEV_BSIZE);
	vd_recfh(r, &fh);
	if ((e = nfs_readlink(&vd_mnt, &fh, target, sizeof(target))) != 0)
		return e;
	n = strlen(target);
	if (n > DEV_BSIZE - 1)
		n = DEV_BSIZE - 1;
	memcpy((char *)buf, target, n);
	return 0;
}

/* ---- inodes and indirect blocks ---- */

static u32
nblocks(ino, r)
	unsigned ino;
	struct irec *r;
{
	u32 size;

	if ((r->mode & IFMT) == IFDIR)
		size = dirsize(ino, r);
	else
		size = r->size;
	size = (size + DEV_BSIZE - 1) / DEV_BSIZE;
	if (size > VD_MAXLBN)
		size = VD_MAXLBN;
	return size;
}

void
vd_mkdinode(ino, dp)
	unsigned ino;
	struct dinode *dp;
{
	struct irec *r, rr;
	u32 nb, size;
	int i;

	memset((char *)dp, 0, sizeof(*dp));
	if (!VD_ISPOOL(ino) && ino >= itab_next) {
		/* Not yet given to a server file: never offered as free. */
		dp->di_mode = IFREG;
		dp->di_nlink = 1;
		return;
	}
	r = vd_freshrec(ino, 0);
	if (r == 0) {
		if (!VD_ISPOOL(ino) && ino > ROOTINO) {
			/* Reserved for server files: never offered as free. */
			dp->di_mode = IFREG;
			dp->di_nlink = 1;
		}
		return;
	}
	if (r->flags & IR_NOALIAS) {
		r->flags &= ~IR_NOALIAS;	/* (ino, lbn) blocks now in use */
		itab_put(ino, r);
	}
	rr = *r;		/* dirsize() may reuse the cache slot */
	if ((rr.mode & IFMT) == IFDIR) {
		size = dirsize(ino, &rr);
		r = itab_get(ino);
		if (r)
			rr = *r;
	} else
		size = rr.size;
	dp->di_mode = rr.mode;
	dp->di_nlink = rr.nlink ? rr.nlink : 1;
	dp->di_uid = rr.uid;
	dp->di_gid = rr.gid;
	dp->di_size = (long)size;
	dp->di_atime = (long)rr.atime;
	dp->di_mtime = (long)rr.mtime;
	dp->di_ctime = (long)rr.ctime;
	nb = (size + DEV_BSIZE - 1) / DEV_BSIZE;
	if (nb > VD_MAXLBN)
		nb = VD_MAXLBN;
	for (i = 0; i < NDADDR; i++)
		dp->di_addr[i] = (u32)i < nb ? VD_BN(ino, i) : 0;
	if (nb > NDADDR)
		dp->di_addr[NDADDR] = VD_BN(ino, VD_IND1);
	if (nb > NDADDR + NINDIR)
		dp->di_addr[NDADDR + 1] = VD_BN(ino, VD_IND2);
}

static int
inoblock(bn, buf)
	daddr_t bn;
	u8 *buf;
{
	struct dinode *dp;
	unsigned ino;
	int i;

	dp = (struct dinode *)buf;
	ino = (unsigned)((bn - 2) * INOPB + 1);
	for (i = 0; i < INOPB; i++, ino++, dp++)
		vd_mkdinode(ino, dp);
	if (!vd_readonly)
		vw_inoread(bn, buf);
	return 0;
}

static int
indblock(ino, sub, buf)
	unsigned ino, sub;
	u8 *buf;
{
	struct irec *r;
	daddr_t *ap;
	u32 nb, first;
	unsigned k, j;

	memset((char *)buf, 0, DEV_BSIZE);
	r = itab_get(ino);
	if (r == 0)
		return 0;
	nb = nblocks(ino, r);
	ap = (daddr_t *)buf;
	if (sub == VD_IND1) {
		for (j = 0; j < NINDIR && NDADDR + j < nb; j++)
			ap[j] = VD_BN(ino, NDADDR + j);
		return 0;
	}
	if (sub == VD_IND2) {
		for (k = 0; k < NINDIR; k++) {
			first = NDADDR + NINDIR + (u32)k * NINDIR;
			if (first >= nb || VD_IND2 - 1 - k <= VD_MAXLBN)
				break;
			ap[k] = VD_BN(ino, VD_IND2 - 1 - k);
		}
		return 0;
	}
	/* single indirect k under the double indirect block */
	k = VD_IND2 - 1 - sub;
	first = NDADDR + NINDIR + (u32)k * NINDIR;
	for (j = 0; j < NINDIR && first + j < nb; j++)
		ap[j] = VD_BN(ino, (unsigned)(first + j));
	return 0;
}

static void
superblock(buf)
	u8 *buf;
{
	struct fs *fs;
	struct nfsd_stat st;

	memset((char *)buf, 0, DEV_BSIZE);
	fs = (struct fs *)buf;
	fs->fs_isize = VD_ISIZE;
	fs->fs_fsize = 0x7fffffffL;
	fs->fs_nfree = 0;
	fs->fs_ninode = 0;
	fs->fs_time = (long)vd_now();
	vd_statfs(&st);
	fs->fs_tfree = st.ns_tfree;
	fs->fs_tinode = st.ns_tinode;
	fs->fs_step = 1;
	fs->fs_cyl = 1;
	fs->fs_lasti = VD_POOLINO;
	fs->fs_nbehind = 0;
	fs->fs_flags = 0;
	if (!vd_readonly)
		vw_freelist(fs);
}

void
vd_statfs(s)
	struct nfsd_stat *s;
{
	struct nfsfsstat f;
	u32 kb, files;

	s->ns_tfree = 0;
	s->ns_tinode = 0;
	if (nfs_fsstat(&vd_mnt, &vd_mnt.root, &f) != 0)
		return;
	kb = (f.abytes.hi >= 512UL) ? 0x7fffffffUL :
	    ((f.abytes.hi << 22) | (f.abytes.lo >> 10));
	if (kb > 0x7fff0000UL)
		kb = 0x7fff0000UL;
	s->ns_tfree = (daddr_t)kb;
	files = (f.afiles.hi || f.afiles.lo > 30000UL) ? 30000UL : f.afiles.lo;
	if (vd_readonly)
		files = 0;
	s->ns_tinode = (ino_t)files;
}

/* ---- entry points ---- */

int
vd_init(dir, unit)
	char *dir;
	int unit;
{
	struct nfsattr a;
	unsigned ino;
	int e;

	strncpy(tmpdir, dir, sizeof(tmpdir) - 1);
	unitno = unit;
	if ((e = itab_open(dir, unit)) != 0)
		return e;
	if ((e = nfs_getattr(&vd_mnt, &vd_mnt.root, &a)) != 0)
		return e;
	if (a.type != NF3DIR)
		return ENOTDIR;
	ino = itab_enter(&a.fileid, &vd_mnt.root, &a, 0);
	if (ino != ROOTINO)
		return EIO;
	return 0;
}

int
vd_read(bn, buf)
	daddr_t bn;
	u8 *buf;
{
	struct irec *r, rr;
	unsigned ino, sub;

	if (bn < 0)
		return EINVAL;
	if (bn == 0) {
		memset((char *)buf, 0, DEV_BSIZE);
		return 0;
	}
	if (bn == 1) {
		superblock(buf);
		return 0;
	}
	if (bn < VD_ISIZE)
		return inoblock(bn, buf);
	ino = VD_INO(bn);
	sub = VD_SUB(bn);
	if (VD_ISPOOLBN(bn)) {
		if (!vd_readonly)
			return vw_poolread(bn, buf);
		memset((char *)buf, 0, DEV_BSIZE);	/* unused pool block */
		return 0;
	}
	if (sub >= VD_MAXLBN)
		return indblock(ino, sub, buf);
	r = itab_get(ino);
	if (r == 0)
		return ESTALE;
	rr = *r;
	switch (rr.mode & IFMT) {
	case IFDIR:
		return dread(ino, sub, buf);
	case IFLNK:
		if (sub != 0) {
			memset((char *)buf, 0, DEV_BSIZE);
			return 0;
		}
		return lread(&rr, buf);
	default:
		return fread_block(ino, &rr, sub, buf);
	}
}

int
vd_write(bn, buf)
	daddr_t bn;
	u8 *buf;
{
	if (bn == 1)
		return 0;		/* superblock updates are harmless */
	if (vd_readonly)
		return EROFS;
	return vw_write(bn, buf);
}

/*
 * Read-write mounts: notice directories changed on the server.  An image
 * is dropped (and rebuilt on next use) only when its mtime changed and the
 * kernel does not have the directory in core.
 */
static void
drefresh()
{
	static u32 last;
	struct dslot *ds;
	struct irec *r;
	u32 now;
	int i;

	now = vd_now();
	if (now - last < (u32)vd_actimeo)
		return;
	last = now;
	for (i = 0; i < NDSLOT; i++) {
		ds = &dslot[i];
		if (ds->ino == 0 || now - ds->built < (u32)vd_actimeo ||
		    !dcandrop(ds, 0))
			continue;
		r = vd_freshrec(ds->ino, 1);
		if (r == 0 || (r->flags & IR_LOCAL))
			continue;
		if (r->mtime == ds->mtime) {
			ds->built = now;
			continue;
		}
		ioctl(vd_ctlfd, NFSDIOCINVAL, (char *)0);
		if (!dcandrop(ds, 0))
			continue;
		if (vd_debug)
			syslog(LOG_DEBUG, "dir %u: changed on server", ds->ino);
		dforget(ds);
	}
}

void
vd_idle()
{
	if (!vd_readonly) {
		vw_idle();
		drefresh();
	}
}
