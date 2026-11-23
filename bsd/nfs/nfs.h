#ifndef NFS_H
#define NFS_H
/*
 * NFS version 3 client library for 2.11BSD (and modern hosts).
 *
 * Native integers are never wire structures: every protocol word is
 * assembled from bytes with shifts, because a PDP-11 long keeps its high
 * 16-bit word first.  int is 16 bits on the PDP-11, so all 32-bit protocol
 * arithmetic is done in unsigned long and reduced with M32.  64-bit NFSv3
 * quantities are carried as a hi/lo pair; files larger than 2 GB are
 * rejected rather than truncated.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if defined(pdp11) || defined(__pdp11__)
#define PDP11 1
#include <sys/time.h>
#include <sys/errno.h>
#else
#include <time.h>
#include <errno.h>
#endif
#include "../pdp11_unistd.h"

#ifdef PDP11
time_t time();
int getpid();
long atol();
#endif
#define EPROTO_RPC EIO

typedef unsigned char u8;
typedef unsigned long u32;
#define M32 0xffffffffUL

struct u64 { u32 hi, lo; };

/* Transfer size for READ/WRITE.  Keep the static buffers within the
 * PDP-11's 64 KB data space. */
#define NFS_IOSIZE 4096
#define NFS_BUFSIZE (NFS_IOSIZE + 1024)
#define NFS_FHMAX 64
#define NFS_NAMEMAX 255
#define NFS_PATHMAX 1024

/* XDR buffer */
struct xbuf {
	u8 *base;
	unsigned cap, len, pos;
	int err;
};

void xinit(struct xbuf *x, u8 *base, unsigned cap);
void xput32(struct xbuf *x, u32 v);
void xput64(struct xbuf *x, struct u64 *v);
void xputopaque(struct xbuf *x, u8 *p, unsigned n);	/* variable length */
void xputfixed(struct xbuf *x, u8 *p, unsigned n);	/* fixed length */
void xputstr(struct xbuf *x, char *s);
u32 xget32(struct xbuf *x);
void xget64(struct xbuf *x, struct u64 *v);
unsigned xgetopaque(struct xbuf *x, u8 *p, unsigned max);
void xgetfixed(struct xbuf *x, u8 *p, unsigned n);
int xgetstr(struct xbuf *x, char *s, unsigned max);	/* -1 if too long */
u8 *xgetref(struct xbuf *x, unsigned *n);	/* borrow variable opaque */
void xskip(struct xbuf *x, unsigned n);
u32 getbe32(u8 *p);
void putbe32(u8 *p, u32 v);

/* RPC over TCP with record marking and AUTH_UNIX */
struct rpcconn {
	int fd;
	u32 prog, vers;
	unsigned short port;
	u32 addr;		/* host order */
	int resv;		/* bind a reserved port */
};

struct rpcauth {
	u32 uid, gid;
	int ngroups;
	u32 groups[16];
	char machine[64];
};

extern struct rpcauth rpc_cred;
extern u8 rpc_sbuf[NFS_BUFSIZE];
extern u8 rpc_rbuf[NFS_BUFSIZE];
extern int rpc_verbose;
extern int rpc_timeout;		/* seconds per call; 0 = wait forever */

int rpc_resolve(char *host, u32 *addr);
int rpc_open(struct rpcconn *c, u32 addr, unsigned port, u32 prog, u32 vers, int resv);
void rpc_close(struct rpcconn *c);
/* Begin a call: returns an xbuf positioned after the RPC header. */
void rpc_begin(struct rpcconn *c, struct xbuf *x, u32 proc);
/* Send the call in x and receive the reply into r, positioned at the
 * procedure results.  Returns 0, or an errno value. */
int rpc_call(struct rpcconn *c, struct xbuf *x, struct xbuf *r);
int pmap_getport(u32 addr, u32 prog, u32 vers, unsigned *port);
void rpc_setcred(u32 uid, u32 gid, int ngroups, u32 *groups);

/* NFS version 3 */
#define NF3REG 1
#define NF3DIR 2
#define NF3BLK 3
#define NF3CHR 4
#define NF3LNK 5
#define NF3SOCK 6
#define NF3FIFO 7

struct nfsfh {
	unsigned len;
	u8 data[NFS_FHMAX];
};

struct nfsattr {
	u32 type, mode, nlink, uid, gid;
	struct u64 size, used;
	u32 rdev1, rdev2;
	struct u64 fsid, fileid;
	u32 atime, atime_ns, mtime, mtime_ns, ctime, ctime_ns;
};

/* Attribute changes for SETATTR/CREATE/MKDIR; a field is sent when its
 * bit is set in mask. */
#define SA_MODE 1
#define SA_UID 2
#define SA_GID 4
#define SA_SIZE 8
#define SA_ATIME 16	/* client time in atime */
#define SA_MTIME 32	/* client time in mtime */
#define SA_ATIME_NOW 64
#define SA_MTIME_NOW 128
struct nfssattr {
	int mask;
	u32 mode, uid, gid, atime, mtime;
	struct u64 size;
};

struct nfsfsstat {
	struct u64 tbytes, fbytes, abytes, tfiles, ffiles, afiles;
};

struct nfsmount {
	struct rpcconn nfs;
	u32 addr;
	char host[64];
	char path[NFS_PATHMAX];
	struct nfsfh root;
	int readonly;
};

/* Directory entry callback for nfs_readdir: return nonzero to stop. */
typedef int (*nfs_dirfn)(void *arg, char *name, struct u64 *fileid,
    struct u64 *cookie);

typedef int (*nfs_plusfn)(void *arg, char *name, struct u64 *fileid,
    struct nfsattr *a, struct nfsfh *fh);

int nfs_mount(struct nfsmount *m, char *host, char *path);
int nfs_unmount(struct nfsmount *m);
int nfs_null(struct nfsmount *m);
int nfs_getattr(struct nfsmount *m, struct nfsfh *fh, struct nfsattr *a);
int nfs_setattr(struct nfsmount *m, struct nfsfh *fh, struct nfssattr *s,
    struct nfsattr *a);
int nfs_lookup(struct nfsmount *m, struct nfsfh *dir, char *name,
    struct nfsfh *fh, struct nfsattr *a);
int nfs_readlink(struct nfsmount *m, struct nfsfh *fh, char *buf, unsigned max);
int nfs_read(struct nfsmount *m, struct nfsfh *fh, u32 off, unsigned count,
    u8 *buf, unsigned *got, int *eof, struct nfsattr *a);
int nfs_write(struct nfsmount *m, struct nfsfh *fh, u32 off, unsigned count,
    u8 *buf, unsigned *done, struct nfsattr *a);
int nfs_create(struct nfsmount *m, struct nfsfh *dir, char *name,
    struct nfssattr *s, int exclusive, struct nfsfh *fh, struct nfsattr *a);
int nfs_mkdir(struct nfsmount *m, struct nfsfh *dir, char *name,
    struct nfssattr *s, struct nfsfh *fh, struct nfsattr *a);
int nfs_symlink(struct nfsmount *m, struct nfsfh *dir, char *name,
    char *target, struct nfsfh *fh, struct nfsattr *a);
int nfs_mknod(struct nfsmount *m, struct nfsfh *dir, char *name, int type,
    struct nfssattr *s, u32 maj, u32 min, struct nfsfh *fh, struct nfsattr *a);
int nfs_remove(struct nfsmount *m, struct nfsfh *dir, char *name);
int nfs_rmdir(struct nfsmount *m, struct nfsfh *dir, char *name);
int nfs_rename(struct nfsmount *m, struct nfsfh *fdir, char *fname,
    struct nfsfh *tdir, char *tname);
int nfs_link(struct nfsmount *m, struct nfsfh *fh, struct nfsfh *dir,
    char *name);
int nfs_readdir(struct nfsmount *m, struct nfsfh *dir, nfs_dirfn fn,
    void *arg);
int nfs_readdirplus(struct nfsmount *m, struct nfsfh *dir, nfs_plusfn fn,
    void *arg, struct nfsattr *dirattr);
int nfs_fsstat(struct nfsmount *m, struct nfsfh *fh, struct nfsfsstat *s);
int nfs_errno(u32 status);
char *nfs_strerror(int e);

/* Path walking from the mount root (symlinks in the middle are followed). */
int nfs_walk(struct nfsmount *m, char *path, int follow, struct nfsfh *fh,
    struct nfsattr *a);
int nfs_parent(struct nfsmount *m, char *path, struct nfsfh *dir, char *leaf);

/* Helpers */
u32 u64small(struct u64 *v);	/* value, or M32 if it does not fit in 31 bits */
int nfs_mode(struct nfsattr *a);	/* st_mode including file type */

#endif
