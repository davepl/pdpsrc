/*
 * nfsdisk - user-served virtual disk for NFS mounts on 2.11BSD.
 *
 * /dev/nfsN (block, major NFSD_BMAJOR) is an ordinary disk to the
 * filesystem code.  Every buffer the kernel queues on it is passed to the
 * daemon holding /dev/nfsdN (character, major NFSD_CMAJOR), which
 * synthesizes the UFS blocks from an NFS server and writes back replies.
 *
 * The daemon read()s one request at a time:
 *	struct nfsd_req, followed by nr_count data bytes for writes.
 * and answers it with write():
 *	struct nfsd_rep, followed by np_count data bytes for reads.
 * read() returns 0 (end of file) once the block device has been closed
 * by unmount and no requests remain.
 */
#ifndef _SYS_NFSDISK_H_
#define	_SYS_NFSDISK_H_

#define	NNFSD		4		/* units */
#define	NFSD_BMAJOR	13
#define	NFSD_CMAJOR	27

struct nfsd_req {
	short	nr_op;			/* NFSD_READ or NFSD_WRITE */
	u_short	nr_count;		/* bytes */
	daddr_t	nr_blkno;		/* DEV_BSIZE block number */
	u_short	nr_seq;			/* echoed in the reply */
	short	nr_spare;
};

struct nfsd_rep {
	short	np_error;		/* 0 or errno */
	u_short	np_count;		/* bytes of data that follow (reads) */
	daddr_t	np_blkno;		/* must match the request */
	u_short	np_seq;			/* must match the request */
	short	np_spare;
};

#define	NFSD_READ	1
#define	NFSD_WRITE	2

/*
 * In-core inode query (read-write support).  With no_blkno nonzero, find
 * the in-core inode whose block map holds that block; otherwise look up
 * inode no_ino.  no_size is -1 if no such inode is in core.
 */
struct nfsd_owner {
	daddr_t	no_blkno;		/* in: block, or 0 */
	ino_t	no_ino;			/* in/out: inode, 0 if none */
	short	no_slot;		/* out: 0..3 direct, -1..-3 indirect level */
	off_t	no_size;		/* out: in-core size */
	u_short	no_mode;		/* out: in-core mode */
	short	no_count;		/* out: reference count */
};

#define	NFSD_MAXQ	8		/* queued buffers before writers wait */

/* Statistics update for statfs(). */
struct nfsd_stat {
	daddr_t	ns_tfree;		/* free 1K blocks */
	ino_t	ns_tinode;		/* free inodes */
};

#define	NFSDNAMELEN	90		/* MNAMELEN */
struct nfsd_name {
	char	nn_name[NFSDNAMELEN];
};

#define	NFSDIOCINVAL	_IO('N', 1)		/* drop clean cached blocks/inodes */
#define	NFSDIOCSETFROM	_IOW('N', 2, struct nfsd_name)	/* mounted-from name */
#define	NFSDIOCOWNER	_IOWR('N', 3, struct nfsd_owner)
#define	NFSDIOCSTAT	_IOW('N', 4, struct nfsd_stat)
#define	NFSDIOCFAILALL	_IO('N', 5)		/* error all queued requests */
#define	NFSDIOCLASTI	_IOW('N', 6, int)	/* set fs_lasti (mount resets it) */

#endif /* _SYS_NFSDISK_H_ */
