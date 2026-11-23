/*
 * Inode table: maps virtual inode numbers to NFS file handles and cached
 * attributes.  Records live in a sparse file indexed by inode number; a
 * second file is an open-addressed hash from NFS fileid to inode number.
 * A small write-through cache keeps recently used records in memory.
 */
#include "vd.h"

#define HSLOTS		65536L		/* hash slots (2 bytes each) */
#define NCACHE		24

static int recfd = -1, hashfd = -1;
static char recpath[80], hashpath[80];
unsigned itab_next = ROOTINO;	/* the export root is entered first */

static struct icache {
	unsigned ino;
	unsigned age;
	struct irec r;
} cache[NCACHE];
static unsigned clock_age;

int
itab_open(dir, unit)
	char *dir;
	int unit;
{
	sprintf(recpath, "%s/.nfsino%d", dir, unit);
	sprintf(hashpath, "%s/.nfshash%d", dir, unit);
	unlink(recpath);
	unlink(hashpath);
	recfd = open(recpath, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (recfd < 0)
		return errno;
	hashfd = open(hashpath, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (hashfd < 0)
		return errno;
	/* Keep the files private; nothing else needs a name for them. */
	unlink(recpath);
	unlink(hashpath);
	memset((char *)cache, 0, sizeof(cache));
	itab_next = ROOTINO;
	return 0;
}

void
itab_close()
{
	if (recfd >= 0)
		close(recfd);
	if (hashfd >= 0)
		close(hashfd);
	recfd = hashfd = -1;
}

static int
readat(fd, off, p, n)
	int fd;
	long off;
	char *p;
	int n;
{
	int r;

	if (lseek(fd, (off_t)off, 0) < 0)
		return -1;
	r = read(fd, p, n);
	if (r < n) {
		if (r < 0)
			return -1;
		memset(p + r, 0, n - r);	/* sparse / beyond end */
	}
	return 0;
}

static int
writeat(fd, off, p, n)
	int fd;
	long off;
	char *p;
	int n;
{
	if (lseek(fd, (off_t)off, 0) < 0)
		return -1;
	return write(fd, p, n) == n ? 0 : -1;
}

static struct icache *
slot(ino)
	unsigned ino;
{
	struct icache *c, *old;
	int i;

	old = &cache[0];
	for (i = 0; i < NCACHE; i++) {
		c = &cache[i];
		if (c->ino == ino && ino != 0) {
			c->age = ++clock_age;
			return c;
		}
		if (c->age < old->age)
			old = c;
	}
	old->ino = ino;
	old->age = ++clock_age;
	if (readat(recfd, (long)ino * (long)sizeof(struct irec),
	    (char *)&old->r, (int)sizeof(struct irec)) < 0)
		memset((char *)&old->r, 0, sizeof(struct irec));
	return old;
}

/* Generation of ino, whether or not it is in use. */
unsigned
itab_gen(ino)
	unsigned ino;
{
	if (ino == 0 || ino > VD_MAXINO)
		return 0;
	return slot(ino)->r.gen;
}

struct irec *
itab_get(ino)
	unsigned ino;
{
	struct icache *c;

	if (ino == 0 || ino > VD_MAXINO)
		return (struct irec *)0;
	c = slot(ino);
	if ((c->r.flags & IR_USED) == 0)
		return (struct irec *)0;
	return &c->r;
}

int
itab_put(ino, r)
	unsigned ino;
	struct irec *r;
{
	struct icache *c;

	c = slot(ino);
	if (&c->r != r)
		c->r = *r;
	return writeat(recfd, (long)ino * (long)sizeof(struct irec),
	    (char *)&c->r, (int)sizeof(struct irec));
}

static long
hashof(fid)
	struct u64 *fid;
{
	u32 h;

	h = fid->lo ^ (fid->lo >> 16) ^ fid->hi ^ (fid->hi >> 16) ^ (fid->lo >> 5);
	return (long)(h & 0xffffUL);
}

static unsigned short
hget(s)
	long s;
{
	unsigned char b[2];

	if (readat(hashfd, s * 2L, (char *)b, 2) < 0)
		return 0;
	return (unsigned short)(b[0] | (b[1] << 8));
}

static void
hset(s, ino)
	long s;
	unsigned ino;
{
	unsigned char b[2];

	b[0] = (unsigned char)ino;
	b[1] = (unsigned char)(ino >> 8);
	writeat(hashfd, s * 2L, (char *)b, 2);
}

unsigned
itab_find(fid)
	struct u64 *fid;
{
	long s, n;
	unsigned ino;
	struct irec *r;

	s = hashof(fid);
	for (n = 0; n < HSLOTS; n++) {
		ino = hget(s);
		if (ino == 0)
			return 0;
		r = itab_get(ino);
		if (r && (r->flags & IR_LOCAL) == 0 &&
		    r->fid_lo == fid->lo && r->fid_hi == fid->hi)
			return ino;
		s = (s + 1) & (HSLOTS - 1);	/* dropped records are tombstones */
	}
	return 0;
}

void
itab_setattr(r, a)
	struct irec *r;
	struct nfsattr *a;
{
	u32 sz;

	r->mode = (unsigned short)nfs_mode(a);
	switch ((int)a->type) {
	case NF3REG:
	case NF3DIR:
	case NF3LNK:
		break;
	default:
		/* Devices, sockets and FIFOs are not exported to the PDP. */
		r->mode = (unsigned short)(IFREG | (a->mode & 0777));
		break;
	}
	r->nlink = a->nlink > 65535UL ? 65535 : (unsigned short)a->nlink;
	r->uid = a->uid > 65535UL ? 65534 : (unsigned short)a->uid;
	r->gid = a->gid > 65535UL ? 65534 : (unsigned short)a->gid;
	sz = u64small(&a->size);
	if (sz == M32 || sz > (u32)VD_MAXLBN * DEV_BSIZE)
		sz = (u32)VD_MAXLBN * DEV_BSIZE;
	if ((r->mode & IFMT) == IFREG && a->type != NF3REG)
		sz = 0;
	r->size = sz;
	r->mtime = a->mtime;
	r->atime = a->atime;
	r->ctime = a->ctime;
	r->attime = (u32)time((time_t *)0);
	r->flags |= IR_ATTR;
}

void
itab_fh(r, fh)
	struct irec *r;
	struct nfsfh *fh;
{
	r->fhlen = (unsigned char)fh->len;
	memcpy((char *)r->fh, (char *)fh->data, fh->len);
}

/* Find or allocate the inode for fid, refreshing handle and attributes. */
unsigned
itab_enter(fid, fh, a, parent)
	struct u64 *fid;
	struct nfsfh *fh;
	struct nfsattr *a;
	unsigned parent;
{
	struct irec *r, nr;
	unsigned ino;
	long s, n;

	ino = itab_find(fid);
	if (ino) {
		r = itab_get(ino);
		if (fh)
			itab_fh(r, fh);
		if (a)
			itab_setattr(r, a);
		if (parent && (r->mode & IFMT) == IFDIR && ino != ROOTINO)
			r->parent = (unsigned short)parent;
		if (fh || a || parent)
			itab_put(ino, r);
		return ino;
	}
	if (fh == 0)
		return 0;		/* cannot use a file without a handle */
	if (VD_ISPOOL(itab_next) || itab_next >= VD_PINO) {
		syslog(LOG_ERR, "inode table full; ignoring further files");
		return 0;
	}
	ino = itab_next++;
	memset((char *)&nr, 0, sizeof(nr));
	nr.fid_hi = fid->hi;
	nr.fid_lo = fid->lo;
	nr.flags = IR_USED;
	nr.parent = (unsigned short)(parent ? parent : ino);
	itab_fh(&nr, fh);
	if (a)
		itab_setattr(&nr, a);
	itab_put(ino, &nr);
	s = hashof(fid);
	for (n = 0; n < HSLOTS; n++) {
		if (hget(s) == 0) {
			hset(s, ino);
			break;
		}
		s = (s + 1) & (HSLOTS - 1);
	}
	return ino;
}

static void
hinsert(fid, ino)
	struct u64 *fid;
	unsigned ino;
{
	long s, n;

	s = hashof(fid);
	for (n = 0; n < HSLOTS; n++) {
		if (hget(s) == 0) {
			hset(s, ino);
			return;
		}
		s = (s + 1) & (HSLOTS - 1);
	}
}

/* An inode the kernel allocated; it exists only here until named. */
void
itab_newlocal(ino, mode, uid, gid, nlink)
	unsigned ino, mode, uid, gid, nlink;
{
	struct irec nr;
	unsigned gen;

	gen = itab_gen(ino) + 1;
	memset((char *)&nr, 0, sizeof(nr));
	nr.gen = (unsigned short)gen;
	nr.flags = IR_USED | IR_LOCAL | IR_NOALIAS;
	nr.mode = (unsigned short)mode;
	nr.uid = (unsigned short)uid;
	nr.gid = (unsigned short)gid;
	nr.nlink = (unsigned short)nlink;
	nr.attime = (u32)time((time_t *)0);
	itab_put(ino, &nr);
}

/* A kernel-allocated inode now exists on the server. */
void
itab_bind(ino, fid, fh, a, parent)
	unsigned ino, parent;
	struct u64 *fid;
	struct nfsfh *fh;
	struct nfsattr *a;
{
	struct irec *r;

	r = itab_get(ino);
	if (r == 0)
		return;
	r->fid_hi = fid->hi;
	r->fid_lo = fid->lo;
	r->flags &= ~IR_LOCAL;
	r->parent = (unsigned short)parent;
	itab_fh(r, fh);
	itab_setattr(r, a);
	itab_put(ino, r);
	hinsert(fid, ino);
}

void
itab_drop(ino)
	unsigned ino;
{
	struct irec *r;

	r = itab_get(ino);
	if (r == 0)
		return;
	r->flags = 0;
	itab_put(ino, r);
}
