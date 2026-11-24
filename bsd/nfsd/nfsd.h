#ifndef NFSD_H
#define NFSD_H
/*
 * NFS version 3 server for 2.11BSD (also builds on modern hosts for
 * testing).  One process answers the portmapper, MOUNT and NFS programs.
 *
 * As in the client (../nfs), no native integer is ever a wire structure:
 * protocol words are assembled from bytes, 32-bit arithmetic is done in
 * unsigned long, and 64-bit quantities are carried as a hi/lo pair.
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
#include <sys/file.h>
#include "../pdp11_unistd.h"
extern int errno;
time_t time();
#else
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

typedef unsigned char u8;
typedef unsigned long u32;
#define M32 0xffffffffUL

struct u64 { u32 hi, lo; };

/* Largest READ or WRITE.  The request and reply buffers are static and
 * must fit, with everything else, in the PDP-11's 64 KB of data space. */
#define IOSIZE 8192
#define REQSIZE (IOSIZE + 1024)
#define REPSIZE (IOSIZE + 512)

#ifdef PDP11
#define PATHMAX 256		/* MAXPATHLEN */
#define NAMEMAX 63		/* MAXNAMLEN */
#else
#define PATHMAX 1024
#define NAMEMAX 255
#endif

/* XDR buffer (xdr.c, shared with the client) */
struct xbuf {
	u8 *base;
	unsigned cap, len, pos;
	int err;
};

void xinit(struct xbuf *x, u8 *base, unsigned cap);
void xput32(struct xbuf *x, u32 v);
void xput64(struct xbuf *x, struct u64 *v);
void xputopaque(struct xbuf *x, u8 *p, unsigned n);
void xputfixed(struct xbuf *x, u8 *p, unsigned n);
void xputstr(struct xbuf *x, char *s);
u32 xget32(struct xbuf *x);
void xget64(struct xbuf *x, struct u64 *v);
unsigned xgetopaque(struct xbuf *x, u8 *p, unsigned max);
void xgetfixed(struct xbuf *x, u8 *p, unsigned n);
int xgetstr(struct xbuf *x, char *s, unsigned max);
u8 *xgetref(struct xbuf *x, unsigned *n);
void xskip(struct xbuf *x, unsigned n);
u32 getbe32(u8 *p);
void putbe32(u8 *p, u32 v);

/* RPC */
#define PMAP_PROG 100000UL
#define NFS_PROG 100003UL
#define MNT_PROG 100005UL

#define RPC_SUCCESS 0
#define RPC_PROG_UNAVAIL 1
#define RPC_PROG_MISMATCH 2
#define RPC_PROC_UNAVAIL 3
#define RPC_GARBAGE 4
#define RPC_SYSERR 5

/* nfsstat3; the low values are the BSD errno values */
#define NFS3_OK 0UL
#define NFS3ERR_PERM 1UL
#define NFS3ERR_NOENT 2UL
#define NFS3ERR_IO 5UL
#define NFS3ERR_ACCES 13UL
#define NFS3ERR_EXIST 17UL
#define NFS3ERR_XDEV 18UL
#define NFS3ERR_NOTDIR 20UL
#define NFS3ERR_ISDIR 21UL
#define NFS3ERR_INVAL 22UL
#define NFS3ERR_FBIG 27UL
#define NFS3ERR_NOSPC 28UL
#define NFS3ERR_ROFS 30UL
#define NFS3ERR_MLINK 31UL
#define NFS3ERR_NAMETOOLONG 63UL
#define NFS3ERR_NOTEMPTY 66UL
#define NFS3ERR_DQUOT 69UL
#define NFS3ERR_STALE 70UL
#define NFS3ERR_BADHANDLE 10001UL
#define NFS3ERR_NOT_SYNC 10002UL
#define NFS3ERR_BAD_COOKIE 10003UL
#define NFS3ERR_NOTSUPP 10004UL
#define NFS3ERR_TOOSMALL 10005UL
#define NFS3ERR_SERVERFAULT 10006UL

/* Exports (/etc/exports) */
#define MAXEXP 8
#define MAXHOSTS 6
#define EXPATH 80
#define EX_RW 1			/* writable */
#define EX_ROOT 2		/* client root is root here */
#define EX_MAPALL 4		/* every client user is mapuid/mapgid */
#define EX_RESV 8		/* client must use a reserved port */

struct export {
	char path[EXPATH];	/* no trailing slash; "/" for the root */
	int flags;
	unsigned rino, rdev;	/* the directory's identity, in handles */
	unsigned mapuid, mapgid;
	int nhosts;		/* 0 = any host */
	u32 haddr[MAXHOSTS], hmask[MAXHOSTS];
};

extern struct export exports[MAXEXP];
extern int nexports;

/* The request being served */
struct req {
	u32 peer;		/* client address, host order */
	unsigned port;
	u32 uid, gid;
	int ngrp;
	u32 grp[16];
};
extern struct req rq;
extern int debug;
extern u8 wverf[8];		/* write verifier: changes when we restart */

#define NOBODY 65534U

/*
 * File handles.  A handle names an object by the inode numbers on the
 * path from the export's directory down to it, so it can be turned back
 * into a path after a restart with no table kept on disk:
 *
 *	0	FHMAGIC
 *	1	export index
 *	2	depth: how many inode numbers follow
 *	3	file type (st_mode >> 12)
 *	4,5	inode number of the export's directory
 *	6,7	its device
 *	8...	depth inode numbers, two bytes each, high byte first
 */
#define FHMAX 64
#define FHHDR 8
#define FHMAGIC 0x4e
#define MAXDEPTH ((FHMAX - FHHDR) / 2)
#define FTYPE(m) ((unsigned)(((m) & S_IFMT) >> 12))
#define DIRTYPE FTYPE(S_IFDIR)
#define INO16(sp) ((unsigned)((sp)->st_ino) & 0xffffU)
#define DEV16(sp) ((unsigned)((sp)->st_dev) & 0xffffU)

/* Handle cache: handle -> path, with an open descriptor for a few */
#define NFH 40
#define MAXFD 8

struct fhent {
	unsigned len;		/* 0 = free */
	u8 fh[FHMAX];
	char *path;
	int fd;			/* -1 = not open */
	int fmode;		/* 1 = readable, 2 = writable */
	long off;		/* descriptor's offset, -1 = unknown */
	u32 age;
};

extern u32 fh_err;		/* nfsstat3 from a failed fh_get */
extern int fh_nopen;

void fh_init(void);
struct fhent *fh_get(u8 *fh, unsigned len, struct stat *st);
struct fhent *fh_put(u8 *fh, unsigned len, char *path);
unsigned fh_root(int ex, u8 *out);
unsigned fh_child(u8 *dir, unsigned dlen, struct stat *st, u8 *out);
unsigned fh_parent(u8 *fh, unsigned len, u8 *out);
int fh_fd(struct fhent *e, int wr, struct stat *st);
void fh_setfd(struct fhent *e, int fd, int fmode);
void fh_closeall(void);
void fh_droppath(char *path);
void fh_rename(char *from, char *to);
int pjoin(char *dst, char *dir, char *name);

void cred_init(void);
void cred_become(unsigned uid, unsigned gid, int ng, u32 *grp);
void cred_set(struct export *ex);
void cred_root(void);
void cred_user(void);
unsigned cred_uid(void);
unsigned cred_gid(void);
int cred_ingroup(unsigned gid);

/* Directory reading (dir.c) */
struct dent {
	u32 ino;
	char *name;
	unsigned namlen;
	long next;		/* cookie: where the following entry starts */
};
int dir_start(char *path, int fd, long off);
int dir_read(struct dent *d);	/* 1 = entry, 0 = end, -1 = error */
void dir_end(void);

int ex_allowed(struct export *ex);
int nfs_proc(u32 proc, struct xbuf *a, struct xbuf *r);
u32 nfserr(int e);

#endif
