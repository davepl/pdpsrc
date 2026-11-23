/*
 * nfsdisk - user-served virtual disk, used for NFS mounts.
 *
 * The block device /dev/nfsN looks like any other disk to the 2.11BSD
 * filesystem code: mount(2), namei, the buffer cache, exec and statfs are
 * unchanged.  Its strategy routine queues each buffer; the daemon that has
 * the control device /dev/nfsdN open reads the queued requests, produces
 * (or consumes) the UFS block contents from an NFS server, and writes back
 * a reply, which completes the buffer.
 *
 * If the daemon goes away every queued and future request fails with
 * ENXIO, so processes using the mount get errors instead of hanging.
 */
#include "param.h"
#include "../machine/seg.h"

#include "systm.h"
#include "user.h"
#include "proc.h"
#include "buf.h"
#include "conf.h"
#include "uio.h"
#include "ioctl.h"
#include "file.h"
#include "inode.h"
#include "fs.h"
#include "mount.h"
#include "nfsdisk.h"

struct nfsdsc {
	short	sc_flags;
	u_short	sc_seq;
	short	sc_nq;			/* buffers queued */
	struct	buf *sc_head, *sc_tail;	/* queued, linked by av_forw */
	struct	buf *sc_active;		/* handed to the daemon */
	struct	proc *sc_selp;		/* daemon selecting */
	struct	proc *sc_procp;		/* daemon, never throttled */
} nfsdsc[NNFSD];

#define	SC_ATTACHED	0x01		/* control device open */
#define	SC_BOPEN	0x02		/* block device open */
#define	SC_CLOSED	0x04		/* unmounted: daemon sees EOF */
#define	SC_RWAIT	0x08		/* daemon sleeping in read */
#define	SC_QWAIT	0x10		/* writer waiting for queue space */

static
nfsdwake(sc)
	register struct nfsdsc *sc;
{
	if (sc->sc_flags & SC_RWAIT) {
		sc->sc_flags &= ~SC_RWAIT;
		wakeup((caddr_t)sc);
	}
	if (sc->sc_selp) {
		selwakeup(sc->sc_selp, (long)0);
		sc->sc_selp = 0;
	}
}

static
nfsdfail(sc, error)
	register struct nfsdsc *sc;
	int error;
{
	register struct buf *bp;
	int s;

	s = splbio();
	while (bp = sc->sc_head) {
		sc->sc_head = bp->av_forw;
		bp->b_error = error;
		bp->b_flags |= B_ERROR;
		biodone(bp);
	}
	sc->sc_tail = 0;
	sc->sc_nq = 0;
	if (sc->sc_flags & SC_QWAIT) {
		sc->sc_flags &= ~SC_QWAIT;
		wakeup((caddr_t)&sc->sc_nq);
	}
	if (bp = sc->sc_active) {
		sc->sc_active = 0;
		bp->b_error = error;
		bp->b_flags |= B_ERROR;
		biodone(bp);
	}
	splx(s);
}

/* ---- block device ---- */

nfsbopen(dev, flag, mode)
	dev_t dev;
	int flag, mode;
{
	register struct nfsdsc *sc;

	if (minor(dev) >= NNFSD)
		return (ENXIO);
	sc = &nfsdsc[minor(dev)];
	if ((sc->sc_flags & SC_ATTACHED) == 0)
		return (ENXIO);
	sc->sc_flags |= SC_BOPEN;
	sc->sc_flags &= ~SC_CLOSED;
	return (0);
}

nfsbclose(dev, flag, mode)
	dev_t dev;
	int flag, mode;
{
	register struct nfsdsc *sc;
	register struct mount *mp;

	if (minor(dev) >= NNFSD)
		return (0);
	for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
		if (mp->m_inodp != NULL && mp->m_dev == dev)
			return (0);		/* still mounted */
	sc = &nfsdsc[minor(dev)];
	sc->sc_flags &= ~SC_BOPEN;
	sc->sc_flags |= SC_CLOSED;
	nfsdwake(sc);
	return (0);
}

nfsbstrategy(bp)
	register struct buf *bp;
{
	register struct nfsdsc *sc;
	int s;

	if (minor(bp->b_dev) >= NNFSD)
		goto bad;
	sc = &nfsdsc[minor(bp->b_dev)];
	if ((sc->sc_flags & SC_ATTACHED) == 0 || (bp->b_flags & B_PHYS) ||
	    bp->b_bcount > MAXBSIZE)
		goto bad;
	bp->av_forw = 0;
	s = splbio();
	/*
	 * Asynchronous writes can be issued much faster than the daemon
	 * completes them.  Make the writer wait rather than letting busy
	 * NFS buffers fill the cache, which the daemon itself needs for
	 * its local files.  Strategy is only reached from process context
	 * on this device.
	 */
	while ((bp->b_flags & (B_READ|B_ASYNC)) == B_ASYNC &&
	    sc->sc_nq >= NFSD_MAXQ && (sc->sc_flags & SC_ATTACHED) &&
	    u.u_procp != sc->sc_procp) {
		sc->sc_flags |= SC_QWAIT;
		sleep((caddr_t)&sc->sc_nq, PRIBIO);
	}
	if ((sc->sc_flags & SC_ATTACHED) == 0) {
		splx(s);
		goto bad;
	}
	sc->sc_nq++;
	if (sc->sc_tail)
		sc->sc_tail->av_forw = bp;
	else
		sc->sc_head = bp;
	sc->sc_tail = bp;
	nfsdwake(sc);
	splx(s);
	return;
bad:
	bp->b_error = ENXIO;
	bp->b_flags |= B_ERROR;
	biodone(bp);
}

/* ---- control device, held by the daemon ---- */

/*
 * The buffer cache keeps b_blkno in 512-byte sectors (fsbtodb); the daemon
 * works in filesystem blocks.  The shift is unsigned: the daemon's block
 * numbers use all 31 bits, so fsbtodb sets the sign bit.
 */
#define	FSBN(bp)	((daddr_t)((u_long)(bp)->b_blkno >> 1))

nfsdopen(dev, flag, mode)
	dev_t dev;
	int flag, mode;
{
	register struct nfsdsc *sc;

	if (minor(dev) >= NNFSD)
		return (ENXIO);
	if (!suser())
		return (u.u_error);
	sc = &nfsdsc[minor(dev)];
	if (sc->sc_flags & (SC_ATTACHED|SC_BOPEN))
		return (EBUSY);
	sc->sc_flags = SC_ATTACHED;
	sc->sc_head = sc->sc_tail = sc->sc_active = 0;
	sc->sc_selp = 0;
	sc->sc_procp = 0;
	sc->sc_nq = 0;
	return (0);
}

nfsdclose(dev, flag, mode)
	dev_t dev;
	int flag, mode;
{
	register struct nfsdsc *sc;

	sc = &nfsdsc[minor(dev)];
	sc->sc_flags &= ~(SC_ATTACHED|SC_RWAIT);
	sc->sc_selp = 0;
	sc->sc_procp = 0;
	nfsdfail(sc, ENXIO);
	return (0);
}

nfsdread(dev, uio, flag)
	dev_t dev;
	register struct uio *uio;
	int flag;
{
	register struct nfsdsc *sc;
	register struct buf *bp;
	struct nfsd_req req;
	int s, error;

	sc = &nfsdsc[minor(dev)];
	sc->sc_procp = u.u_procp;
	if (sc->sc_active)
		return (EBUSY);		/* answer the last request first */
	if (uio->uio_resid < sizeof (req) + MAXBSIZE)
		return (EINVAL);
	s = splbio();
	while (sc->sc_head == 0) {
		if (sc->sc_flags & SC_CLOSED) {
			splx(s);
			return (0);
		}
		if (flag & IO_NDELAY) {
			splx(s);
			return (EWOULDBLOCK);
		}
		sc->sc_flags |= SC_RWAIT;
		sleep((caddr_t)sc, PZERO + 1);
	}
	bp = sc->sc_head;
	if ((sc->sc_head = bp->av_forw) == 0)
		sc->sc_tail = 0;
	sc->sc_active = bp;
	if (--sc->sc_nq < NFSD_MAXQ && (sc->sc_flags & SC_QWAIT)) {
		sc->sc_flags &= ~SC_QWAIT;
		wakeup((caddr_t)&sc->sc_nq);
	}
	splx(s);

	req.nr_op = (bp->b_flags & B_READ) ? NFSD_READ : NFSD_WRITE;
	req.nr_count = bp->b_bcount;
	req.nr_blkno = FSBN(bp);
	req.nr_seq = ++sc->sc_seq;
	req.nr_spare = 0;
	error = uiomove((caddr_t)&req, sizeof (req), uio);
	if (error == 0 && req.nr_op == NFSD_WRITE && bp->b_bcount) {
		error = uiomove(mapin(bp), bp->b_bcount, uio);
		mapout(bp);
	}
	if (error) {
		s = splbio();
		sc->sc_active = 0;
		if ((bp->av_forw = sc->sc_head) == 0)
			sc->sc_tail = bp;
		sc->sc_head = bp;
		sc->sc_nq++;
		splx(s);
	}
	return (error);
}

nfsdwrite(dev, uio, flag)
	dev_t dev;
	register struct uio *uio;
	int flag;
{
	register struct nfsdsc *sc;
	register struct buf *bp;
	struct nfsd_rep rep;
	u_int n;
	caddr_t addr;
	int error;

	sc = &nfsdsc[minor(dev)];
	if (uio->uio_resid < sizeof (rep))
		return (EINVAL);
	if (error = uiomove((caddr_t)&rep, sizeof (rep), uio))
		return (error);
	bp = sc->sc_active;
	if (bp == 0 || rep.np_seq != sc->sc_seq || rep.np_blkno != FSBN(bp))
		return (EINVAL);
	if (rep.np_error) {
		bp->b_error = rep.np_error;
		bp->b_flags |= B_ERROR;
	} else if (bp->b_flags & B_READ) {
		n = rep.np_count;
		if (n > bp->b_bcount)
			n = bp->b_bcount;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		addr = mapin(bp);
		if (n)
			error = uiomove(addr, n, uio);
		if (n < bp->b_bcount)
			bzero(addr + n, bp->b_bcount - n);
		mapout(bp);
		if (error) {
			bp->b_error = EIO;
			bp->b_flags |= B_ERROR;
		}
	}
	bp->b_resid = 0;
	sc->sc_active = 0;
	biodone(bp);
	return (error);
}

nfsdselect(dev, rw)
	dev_t dev;
	int rw;
{
	register struct nfsdsc *sc;
	int s, ready;

	sc = &nfsdsc[minor(dev)];
	if (rw != FREAD)
		return (1);
	s = splbio();
	ready = sc->sc_head != 0 || (sc->sc_flags & SC_CLOSED);
	if (!ready)
		sc->sc_selp = u.u_procp;
	splx(s);
	return (ready);
}

/*
 * Forget clean cached state for the device so that later references
 * fetch fresh blocks from the daemon.  Dirty or busy buffers and
 * referenced or modified inodes are left alone.
 */
static
nfsdinval(dev)
	dev_t dev;
{
	register struct buf *bp;
	register struct bufhd *hp;
	register struct inode *ip;
	int s;
#define	dp ((struct buf *)hp)

	s = splbio();
	for (hp = bufhash; hp < &bufhash[BUFHSZ]; hp++)
		for (bp = dp->b_forw; bp != dp; bp = bp->b_forw)
			if (bp->b_dev == dev &&
			    (bp->b_flags & (B_BUSY|B_DELWRI)) == 0)
				bp->b_flags |= B_INVAL;
	splx(s);
#undef dp
	for (ip = inode; ip < inodeNINODE; ip++) {
		if (ip->i_dev != dev || ip->i_count != 0 || ip->i_number == 0)
			continue;
		if (ip->i_flag & (ILOCKED|IUPD|IACC|ICHG|IMOD|IMOUNT|ITEXT))
			continue;
		remque(ip);
		ip->i_forw = ip;
		ip->i_back = ip;
		ip->i_number = 0;
		cacheinval(ip);
	}
	nchinval(dev);
}

static struct mount *
nfsdmount(dev)
	dev_t dev;
{
	register struct mount *mp;

	for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
		if (mp->m_inodp != NULL && mp->m_dev == dev)
			return (mp);
	return ((struct mount *)NULL);
}

nfsdioctl(dev, cmd, data, flag)
	dev_t dev;
	u_int cmd;
	caddr_t data;
	int flag;
{
	register struct mount *mp;
	register struct inode *ip;
	struct xmount *xmp;
	struct nfsd_owner *op;
	dev_t bdev;
	int i;

	bdev = makedev(NFSD_BMAJOR, minor(dev));
	switch (cmd) {
	case NFSDIOCINVAL:
		nfsdinval(bdev);
		return (0);

	case NFSDIOCSETFROM:
		if ((mp = nfsdmount(bdev)) == NULL)
			return (ENXIO);
		((struct nfsd_name *)data)->nn_name[NFSDNAMELEN - 1] = '\0';
		xmp = (struct xmount *)SEG5;
		mapseg5(mp->m_extern, XMOUNTDESC);
		bzero(xmp->xm_mntfrom, MNAMELEN);
		bcopy(((struct nfsd_name *)data)->nn_name, xmp->xm_mntfrom,
		    MNAMELEN - 1);
		normalseg5();
		return (0);

	case NFSDIOCSTAT:
		if ((mp = nfsdmount(bdev)) == NULL)
			return (ENXIO);
		mp->m_filsys.fs_tfree = ((struct nfsd_stat *)data)->ns_tfree;
		mp->m_filsys.fs_tinode = ((struct nfsd_stat *)data)->ns_tinode;
		return (0);

	case NFSDIOCOWNER:
		op = (struct nfsd_owner *)data;
		op->no_slot = 0;
		op->no_size = -1;
		op->no_mode = 0;
		op->no_count = 0;
		for (ip = inode; ip < inodeNINODE; ip++) {
			if (ip->i_dev != bdev || ip->i_number == 0)
				continue;
			if (op->no_blkno == 0) {
				if (ip->i_number == op->no_ino)
					goto found;
				continue;
			}
			switch (ip->i_mode & IFMT) {
			case IFREG:
			case IFDIR:
			case IFLNK:
				break;
			default:
				continue;
			}
			for (i = 0; i < NDADDR; i++)
				if (ip->i_db[i] == op->no_blkno) {
					op->no_slot = i;
					goto found;
				}
			for (i = 0; i < NIADDR; i++)
				if (ip->i_ib[i] == op->no_blkno) {
					op->no_slot = -(i + 1);
					goto found;
				}
		}
		op->no_ino = 0;
		return (0);
found:
		op->no_ino = ip->i_number;
		op->no_size = ip->i_size;
		op->no_mode = ip->i_mode;
		op->no_count = ip->i_count;
		return (0);

	case NFSDIOCLASTI:
		/* mountfs() sets fs_lasti to 1; start inode scans at the pool. */
		if ((mp = nfsdmount(bdev)) == NULL)
			return (ENXIO);
		mp->m_filsys.fs_lasti = *(int *)data;
		mp->m_filsys.fs_nbehind = 0;
		return (0);

	case NFSDIOCFAILALL:
		nfsdfail(&nfsdsc[minor(dev)], EIO);
		return (0);
	}
	return (EINVAL);
}
