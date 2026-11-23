#ifndef VD_H
#define VD_H
/*
 * mount_nfs: virtual UFS disk synthesized from an NFS export.
 *
 * Block layout of /dev/nfsN (1 KB blocks):
 *	0		boot block (zeros)
 *	1		superblock
 *	2 .. 4095	inode blocks, 16 dinodes each (inodes 1 .. 65504)
 *	(ino << 15) | sub	blocks of inode ino:
 *		sub < VD_MAXLBN		file/directory block sub
 *		VD_IND1			single indirect block
 *		VD_IND2			double indirect block
 *		VD_IND2 - 1 - k		k-th single indirect under VD_IND2
 *	VD_POOLBASE .. VD_POOLEND-1	pool of free blocks for new data
 *
 * Inode numbers below VD_PINO are given to files found on the server;
 * numbers from VD_POOLINO up are offered to the kernel for new files.
 * The numbers VD_PINO .. VD_POOLINO-1 are never inodes: their block range
 * is the pool.  Every block the kernel allocates must lie below 2^30,
 * because bmap() converts a new buffer back with dbtofsb(b_blkno), and
 * b_blkno (fsbtodb, a signed shift left) overflows above that.
 */
#include "nfs.h"

#ifdef PDP11
#include <sys/param.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/dir.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/nfsdisk.h>
#include <syslog.h>
#else
/* Look-alike definitions so the daemon can be compile-checked on a
 * modern host.  It only runs on 2.11BSD. */
#include <sys/ioctl.h>
#include <syslog.h>
#include <fcntl.h>
typedef long daddr_tt;
#define daddr_t daddr_tt
typedef unsigned short ino_tt;
#define ino_t ino_tt
#define DEV_BSIZE 1024
#define MAXBSIZE 1024
#define INOPB 16
#define NDADDR 4
#define NIADDR 3
#define NINDIR (DEV_BSIZE / 4)
#define ROOTINO ((ino_t)2)
#define NICINOD 100
#define NICFREE 50
#define MAXNAMLEN 63
#define DIRBLKSIZ 512
#define IFMT 0170000
#define IFCHR 0020000
#define IFDIR 0040000
#define IFBLK 0060000
#define IFREG 0100000
#define IFLNK 0120000
#define MNT_RDONLY 0x0001
#define MNT_SYNCHRONOUS 0x0002
#define MNT_NOEXEC 0x0004
#define MNT_NOSUID 0x0008
#define MNT_NODEV 0x0010
#define MNT_NOATIME 0x0080
#define itod(x) ((daddr_t)((((unsigned)(x) + 2 * INOPB - 1) / INOPB)))
#define itoo(x) ((int)(((x) + 2 * INOPB - 1) % INOPB))
struct fs {
	unsigned short fs_isize;
	daddr_t fs_fsize;
	short fs_nfree;
	daddr_t fs_free[NICFREE];
	short fs_ninode;
	ino_t fs_inode[NICINOD];
	char fs_flock, fs_fmod, fs_ilock, fs_ronly;
	long fs_time;
	daddr_t fs_tfree;
	ino_t fs_tinode;
	short fs_step, fs_cyl;
	char fs_fsmnt[12];
	ino_t fs_lasti, fs_nbehind;
	unsigned short fs_flags;
};
struct fblk { short df_nfree; daddr_t df_free[NICFREE]; };
struct icommon1 {
	unsigned short ic_mode, ic_nlink, ic_uid, ic_gid;
	long ic_size;
};
struct icommon2 { long ic_atime, ic_mtime, ic_ctime; };
struct dinode {
	struct icommon1 di_icom1;
	daddr_t di_addr[7];
	unsigned short di_reserved[5];
	unsigned short di_flags;
	struct icommon2 di_icom2;
};
#define di_mode di_icom1.ic_mode
#define di_nlink di_icom1.ic_nlink
#define di_uid di_icom1.ic_uid
#define di_gid di_icom1.ic_gid
#define di_size di_icom1.ic_size
#define di_atime di_icom2.ic_atime
#define di_mtime di_icom2.ic_mtime
#define di_ctime di_icom2.ic_ctime
struct direct {
	ino_t d_ino;
	unsigned short d_reclen, d_namlen;
	char d_name[MAXNAMLEN + 1];
};
#define DIRSIZ(dp) \
    ((((sizeof (struct direct) - (MAXNAMLEN+1)) + (dp)->d_namlen+1) + 3) &~ 3)
struct nfsd_req { short nr_op; unsigned short nr_count; daddr_t nr_blkno;
	unsigned short nr_seq; short nr_spare; };
struct nfsd_rep { short np_error; unsigned short np_count; daddr_t np_blkno;
	unsigned short np_seq; short np_spare; };
#define NFSD_READ 1
#define NFSD_WRITE 2
#define NNFSD 4
struct nfsd_owner { daddr_t no_blkno; ino_t no_ino; short no_slot; long no_size; unsigned short no_mode; short no_count; };
struct nfsd_stat { daddr_t ns_tfree; ino_t ns_tinode; };
struct nfsd_name { char nn_name[90]; };
#define NFSDIOCINVAL _IO('N', 1)
#define NFSDIOCSETFROM _IOW('N', 2, struct nfsd_name)
#define NFSDIOCOWNER _IOWR('N', 3, struct nfsd_owner)
#define NFSDIOCSTAT _IOW('N', 4, struct nfsd_stat)
#define NFSDIOCFAILALL _IO('N', 5)
int mount(char *, char *, int);
#endif

#define VD_ISIZE	4096		/* first block after the inode area */
#define VD_MAXINO	65504		/* largest inode in the inode area */
#define VD_POOLINO	32768		/* first inode offered to the kernel */
/*
 * Test for pool inodes with a bit test, never "ino >= 32768": the 2.11 C
 * optimizer turns that unsigned compare into a sign test and can attach it
 * to the condition codes of a preceding, unrelated tst.
 */
#define VD_ISPOOL(ino)	(((unsigned)(ino) & 0100000) != 0)
#define VD_MAXLBN	32512		/* blocks per file: ~31.75 MB */
#define VD_IND1		32767
#define VD_IND2		32766
#define VD_BN(ino, sub)	(((daddr_t)(ino) << 15) | (daddr_t)(sub))
#define VD_INO(bn)	((unsigned)((bn) >> 15))
#define VD_SUB(bn)	((unsigned)((bn) & 0x7fffL))
#define VD_PINO		32512		/* first inode number reserved for the pool */
#define VD_POOLBASE	VD_BN(VD_PINO, 0)	/* pool blocks: never files */
#define VD_POOLEND	VD_BN(VD_POOLINO, 0)	/* 2^30 */
#define VD_ISPOOLBN(bn)	((bn) >= VD_POOLBASE && (bn) < VD_POOLEND)

/* Inode table record, kept in a sparse file indexed by inode number. */
struct irec {
	u32 fid_hi, fid_lo;		/* NFS fileid */
	u32 size;			/* bytes (clamped) */
	u32 mtime, atime, ctime;
	u32 attime;			/* when attributes were fetched */
	u32 dsize, dmtime;		/* synthesized directory size, at mtime */
	unsigned short mode, nlink, uid, gid;
	unsigned short parent;		/* directory found in */
	unsigned short gen;		/* bumped each time ino is reused */
	unsigned char fhlen, flags;
	unsigned char fh[NFS_FHMAX];
};
#define IR_USED		0x01
#define IR_ATTR		0x02		/* attributes valid */
#define IR_DSIZE	0x04		/* dsize valid for dmtime */
#define IR_LOCAL	0x08		/* created by the kernel, not yet on server */
#define IR_NOALIAS	0x10		/* (ino, lbn) blocks not served since reuse */

/* itab.c */
int itab_open(char *dir, int unit);
void itab_close(void);
struct irec *itab_get(unsigned ino);	/* NULL if unused */
int itab_put(unsigned ino, struct irec *r);
unsigned itab_find(struct u64 *fid);	/* 0 if unknown */
unsigned itab_enter(struct u64 *fid, struct nfsfh *fh, struct nfsattr *a,
    unsigned parent);
void itab_setattr(struct irec *r, struct nfsattr *a);
void itab_fh(struct irec *r, struct nfsfh *fh);
void itab_newlocal(unsigned ino, unsigned mode, unsigned uid, unsigned gid,
    unsigned nlink);
void itab_bind(unsigned ino, struct u64 *fid, struct nfsfh *fh,
    struct nfsattr *a, unsigned parent);
void itab_drop(unsigned ino);
extern unsigned itab_next;		/* next inode for a server file */

/* vdisk.c */
extern struct nfsmount vd_mnt;
extern int vd_readonly;
extern int vd_debug;			/* log NFS operations */
extern int vd_actimeo;
extern int vd_ctlfd;			/* attribute cache seconds */
unsigned itab_gen(unsigned ino);
int vd_init(char *tmpdir, int unit);
int vd_read(daddr_t bn, u8 *buf);
int vd_write(daddr_t bn, u8 *buf);
void vd_idle(void);
void vd_statfs(struct nfsd_stat *s);
u32 vd_now(void);
void vd_recfh(struct irec *r, struct nfsfh *fh);
struct irec *vd_freshrec(unsigned ino, int force);
int vd_dirblock(unsigned ino, unsigned lbn, u8 *buf);
int vd_dirupdate(unsigned ino, unsigned lbn, u8 *buf, u32 mtime);
void vd_dirtouch(unsigned ino, u32 mtime);
void vd_rcinval(unsigned ino);
void vd_mkdinode(unsigned ino, struct dinode *dp);

/* vdwrite.c */
int vw_init(char *tmpdir, int unit, int ctlfd);
int vw_write(daddr_t bn, u8 *buf);
int vw_poolread(daddr_t bn, u8 *buf);
void vw_inoread(daddr_t bn, u8 *buf);
void vw_idle(void);
void vw_freelist(struct fs *fs);
long vw_ksize(unsigned ino);		/* -1 if unknown */

#endif
