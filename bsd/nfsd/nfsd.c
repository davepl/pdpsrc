/*
 * nfsd - NFS version 3 server for 2.11BSD.
 *
 * One process, no kernel changes.  It answers three RPC programs: the
 * portmapper on port 111 (2.11BSD has none), and MOUNT and NFS, both on
 * port 2049.  NFS is served over TCP; the portmapper and MOUNT are also
 * answered over UDP, which some clients use for them by default.
 *
 *	nfsd [-d] [-f exports] [-p port] [-P pmapport] [-w window]
 */
#include "nfsd.h"
#include <signal.h>
#include <syslog.h>
#include <pwd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#ifndef PDP11
#include <sys/select.h>
#include <arpa/inet.h>
#endif

struct export exports[MAXEXP];
int nexports;
struct req rq;
int debug;

static u8 reqbuf[REQSIZE];
static u8 repbuf[REPSIZE + 4];	/* the record mark goes in front */

#define MAXCONN 10
static struct conn {
	int fd;			/* -1 = free */
	u32 peer;
	unsigned port;
	u32 used;
} conns[MAXCONN];
static u32 connclock;

static unsigned nfsport = 2049, pmapport = 111;
static char *expfile = "/etc/exports";

/*
 * The socket buffer size of each TCP connection, in bytes (-w): the
 * receive window a client is offered, and how much of a reply waits in
 * the kernel.  It bounds the kernel memory one connection can tie up,
 * and on 2.11BSD that matters more than speed.  The whole network has
 * about 170 mbufs of 118 bytes and 12 pages of 1 KB, and most of the
 * mbufs are in use before any data moves.  The DEUNA driver copies
 * every received frame into mbufs alone, 13 for a full-size frame, and
 * the other paths do the same once the pages run out.  When no mbuf is
 * free, nothing more can be received and whoever tries to send sleeps
 * uninterruptibly: the machine looks dead from the network although
 * its console still works.  8 KB of buffer can take 70 mbufs, 4 KB 37,
 * 1 KB about 10.
 */
#ifdef PDP11
#define RCVWIN 1024
#define MAXWIN 6144		/* more than the kernel allows anyway */
#else
#define RCVWIN 32768
#define MAXWIN 32768
#endif
#define MINWIN 512
static int rcvwin = RCVWIN;
static int gothup, gotterm;
static char mpath[PATHMAX + NAMEMAX + 2];
static char wpath[PATHMAX + NAMEMAX + 2];

static char *
ipstr(a)
	u32 a;
{
	static char b[20];

	sprintf(b, "%d.%d.%d.%d", (int)((a >> 24) & 255),
	    (int)((a >> 16) & 255), (int)((a >> 8) & 255), (int)(a & 255));
	return b;
}

/* a.b.c.d; returns the character after it, or 0 if it is not one */
static char *
getip(s, ap)
	char *s;
	u32 *ap;
{
	u32 a;
	int i, n;

	a = 0;
	for (i = 0; i < 4; i++) {
		if (*s < '0' || *s > '9')
			return (char *)0;
		n = 0;
		while (*s >= '0' && *s <= '9')
			n = n * 10 + (*s++ - '0');
		if (n > 255)
			return (char *)0;
		a = (a << 8) | (u32)n;
		if (i < 3 && *s++ != '.')
			return (char *)0;
	}
	*ap = a & M32;
	return s;
}

/* May the client being served use this export? */
int
ex_allowed(ex)
	register struct export *ex;
{
	int i;

	if ((ex->flags & EX_RESV) && rq.port >= 1024)
		return 0;
	if (ex->nhosts == 0)
		return 1;
	for (i = 0; i < ex->nhosts; i++)
		if ((rq.peer & ex->hmask[i]) == ex->haddr[i])
			return 1;
	return 0;
}

/*
 * /etc/exports: one directory a line,
 *	/path [-option[,option...]] [host | network/bits] ...
 * Options: ro (the default), rw, root, mapall=uid[:gid], resvport.
 * Returns the number of mistakes found.
 */
static int
readexports()
{
	static char line[256];
	struct stat sb;
	struct passwd *pw;
	register struct export *ex;
	FILE *f;
	char *p, *w, *q, *g;
	u32 a;
	int bad, n, lineno, bits;

	f = fopen(expfile, "r");
	if (f == 0) {
		syslog(LOG_ERR, "%s: %m", expfile);
		return 1;
	}
	nexports = 0;
	bad = lineno = 0;
	while (fgets(line, sizeof(line), f)) {
		lineno++;
		p = line;
		ex = (struct export *)0;
		for (;;) {
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p == 0 || *p == '\n' || *p == '#')
				break;
			w = p;
			while (*p && *p != ' ' && *p != '\t' && *p != '\n')
				p++;
			if (*p)
				*p++ = 0;
			if (ex == 0) {
				/* the directory */
				n = strlen(w);
				while (n > 1 && w[n - 1] == '/')
					w[--n] = 0;
				if (w[0] != '/' || n >= EXPATH ||
				    nexports >= MAXEXP ||
				    lstat(w, &sb) < 0 ||
				    (sb.st_mode & S_IFMT) != S_IFDIR) {
					syslog(LOG_ERR,
					    "%s line %d: cannot export %s",
					    expfile, lineno, w);
					bad++;
					break;
				}
				ex = &exports[nexports++];
				strcpy(ex->path, w);
				ex->flags = 0;
				ex->nhosts = 0;
				ex->rino = INO16(&sb);
				ex->rdev = DEV16(&sb);
				continue;
			}
			if (*w == '-') {
				for (w++; *w; w = q) {
					q = strchr(w, ',');
					if (q)
						*q++ = 0;
					else
						q = w + strlen(w);
					if (strcmp(w, "rw") == 0)
						ex->flags |= EX_RW;
					else if (strcmp(w, "ro") == 0)
						ex->flags &= ~EX_RW;
					else if (strcmp(w, "root") == 0)
						ex->flags |= EX_ROOT;
					else if (strcmp(w, "resvport") == 0)
						ex->flags |= EX_RESV;
					else if (strncmp(w, "mapall=", 7) == 0) {
						w += 7;
						ex->flags |= EX_MAPALL;
						g = strchr(w, ':');
						if (g)
							*g++ = 0;
						if (*w >= '0' && *w <= '9') {
							ex->mapuid = atoi(w);
							ex->mapgid = g ?
							    atoi(g) : NOBODY;
						} else if ((pw = getpwnam(w))) {
							ex->mapuid = pw->pw_uid;
							ex->mapgid = g ?
							    atoi(g) : pw->pw_gid;
						} else {
							syslog(LOG_ERR,
					    "%s line %d: no user %s",
							    expfile, lineno, w);
							bad++;
						}
					} else {
						syslog(LOG_ERR,
					    "%s line %d: unknown option %s",
						    expfile, lineno, w);
						bad++;
					}
				}
				continue;
			}
			/* a host or a network */
			q = getip(w, &a);
			bits = 32;
			if (q && *q == '/')
				bits = atoi(q + 1);
			else if (q && *q)
				q = 0;
			if (q == 0 || bits < 0 || bits > 32 ||
			    ex->nhosts >= MAXHOSTS) {
				syslog(LOG_ERR, "%s line %d: bad host %s",
				    expfile, lineno, w);
				bad++;
				continue;
			}
			ex->hmask[ex->nhosts] = bits == 0 ? 0 :
			    (M32 << (32 - bits)) & M32;
			ex->haddr[ex->nhosts] = a & ex->hmask[ex->nhosts];
			ex->nhosts++;
		}
	}
	fclose(f);
	return bad;
}

/* The portmapper: where our own three programs are, and nothing else. */
static struct pmap {
	u32 prog, vers, prot;
	int nfs;		/* on the NFS port (else the portmap port) */
} pmaps[] = {
	{ PMAP_PROG, 2, 6, 0 },
	{ PMAP_PROG, 2, 17, 0 },
	{ MNT_PROG, 1, 17, 1 },
	{ MNT_PROG, 3, 17, 1 },
	{ MNT_PROG, 1, 6, 1 },
	{ MNT_PROG, 3, 6, 1 },
	{ NFS_PROG, 3, 6, 1 },
};
#define NPMAP (sizeof(pmaps) / sizeof(pmaps[0]))

static int
pmap_proc(proc, a, r)
	u32 proc;
	struct xbuf *a, *r;
{
	register struct pmap *m;
	u32 prog, vers, prot, port;

	switch ((int)proc) {
	case 0:
		return RPC_SUCCESS;
	case 3:			/* GETPORT */
		prog = xget32(a);
		vers = xget32(a);
		prot = xget32(a);
		(void)xget32(a);
		if (a->err)
			return RPC_GARBAGE;
		port = 0;
		for (m = pmaps; m < &pmaps[NPMAP]; m++)
			if (m->prog == prog && m->prot == prot &&
			    (m->vers == vers || port == 0))
				port = m->nfs ? nfsport : pmapport;
		xput32(r, port);
		return RPC_SUCCESS;
	case 4:			/* DUMP */
		for (m = pmaps; m < &pmaps[NPMAP]; m++) {
			xput32(r, (u32)1);
			xput32(r, m->prog);
			xput32(r, m->vers);
			xput32(r, m->prot);
			xput32(r, (u32)(m->nfs ? nfsport : pmapport));
		}
		xput32(r, (u32)0);
		return RPC_SUCCESS;
	}
	return RPC_PROC_UNAVAIL;
}

/* MOUNT: hand out the handle of an exported directory, or one below it. */
static void
mnt_mnt(r)
	struct xbuf *r;
{
	static u8 fh[FHMAX], fh2[FHMAX];
	struct stat sb;
	register struct export *ex, *best;
	unsigned n, len, bn;
	char *p, *q;
	int i;

	n = strlen(mpath);
	while (n > 1 && mpath[n - 1] == '/')
		mpath[--n] = 0;
	best = 0;
	bn = 0;
	for (i = 0; i < nexports; i++) {
		ex = &exports[i];
		len = strlen(ex->path);
		if (strncmp(mpath, ex->path, len) != 0)
			continue;
		if (len > 1 && mpath[len] != 0 && mpath[len] != '/')
			continue;
		if (best == 0 || len > bn) {
			best = ex;
			bn = len;
		}
	}
	if (best == 0 || !ex_allowed(best)) {
		syslog(LOG_NOTICE, "mount of %s refused to %s", mpath,
		    ipstr(rq.peer));
		xput32(r, NFS3ERR_ACCES);
		return;
	}
	cred_become(0, 0, 0, (u32 *)0);
	len = fh_root((int)(best - exports), fh);
	strcpy(wpath, best->path);
	/* walk down to a directory below the exported one */
	p = mpath + bn;
	while (*p) {
		while (*p == '/')
			p++;
		if (*p == 0)
			break;
		q = p;
		while (*p && *p != '/')
			p++;
		if (*p)
			*p++ = 0;
		if ((q[0] == '.' && (q[1] == 0 || (q[1] == '.' && q[2] == 0)))
		    || pjoin(wpath, wpath, q) < 0 ||
		    lstat(wpath, &sb) < 0) {
			xput32(r, NFS3ERR_NOENT);
			return;
		}
		if ((sb.st_mode & S_IFMT) != S_IFDIR) {
			xput32(r, NFS3ERR_NOTDIR);
			return;
		}
		n = fh_child(fh, len, &sb, fh2);
		if (n == 0) {
			xput32(r, NFS3ERR_NAMETOOLONG);
			return;
		}
		memcpy((char *)fh, (char *)fh2, n);
		len = n;
	}
	(void)fh_put(fh, len, wpath);
	syslog(LOG_INFO, "%s mounted by %s", wpath, ipstr(rq.peer));
	xput32(r, NFS3_OK);
	xputopaque(r, fh, len);
	xput32(r, (u32)1);		/* one flavor: AUTH_SYS */
	xput32(r, (u32)1);
}

static int
mnt_proc(vers, proc, a, r)
	u32 vers, proc;
	struct xbuf *a, *r;
{
	register struct export *ex;
	int i, n;

	switch ((int)proc) {
	case 0:
		return RPC_SUCCESS;
	case 1:			/* MNT */
		n = xgetstr(a, mpath, PATHMAX);
		if (a->err)
			return RPC_GARBAGE;
		if (vers != 3)
			xput32(r, NFS3ERR_ACCES);	/* no v2 handles */
		else if (n < 0)
			xput32(r, NFS3ERR_NAMETOOLONG);
		else
			mnt_mnt(r);
		return RPC_SUCCESS;
	case 2:			/* DUMP: we keep no list */
		xput32(r, (u32)0);
		return RPC_SUCCESS;
	case 3:			/* UMNT */
	case 4:			/* UMNTALL */
		return RPC_SUCCESS;
	case 5:			/* EXPORT */
		for (ex = exports; ex < &exports[nexports]; ex++) {
			xput32(r, (u32)1);
			xputstr(r, ex->path);
			for (i = 0; i < ex->nhosts; i++) {
				xput32(r, (u32)1);
				xputstr(r, ipstr(ex->haddr[i]));
			}
			xput32(r, (u32)0);
		}
		xput32(r, (u32)0);
		return RPC_SUCCESS;
	}
	return RPC_PROC_UNAVAIL;
}

/*
 * One RPC call in a, its reply into r.  Returns 0 if the message is not
 * a call we can answer at all.
 */
static int
rpc_serve(a, r)
	register struct xbuf *a, *r;
{
	struct xbuf c;
	u32 xid, rpcvers, prog, vers, proc, flavor, lo, hi;
	u8 *p;
	unsigned n, statpos;
	int st, i;

	xid = xget32(a);
	if (xget32(a) != 0)
		return 0;
	rpcvers = xget32(a);
	prog = xget32(a);
	vers = xget32(a);
	proc = xget32(a);
	flavor = xget32(a);
	p = xgetref(a, &n);
	if (a->err)
		return 0;
	rq.uid = rq.gid = NOBODY;
	rq.ngrp = 0;
	if (flavor == 1) {		/* AUTH_SYS */
		xinit(&c, p, n);
		c.len = n;
		(void)xget32(&c);
		(void)xgetref(&c, &n);
		rq.uid = xget32(&c);
		rq.gid = xget32(&c);
		n = (unsigned)xget32(&c);
		if (c.err || n > 16)
			return 0;
		for (i = 0; i < n; i++)
			rq.grp[i] = xget32(&c);
		if (c.err)
			return 0;
		rq.ngrp = n;
	}
	(void)xget32(a);		/* verifier */
	(void)xgetref(a, &n);
	if (a->err)
		return 0;

	xput32(r, xid);
	xput32(r, (u32)1);		/* REPLY */
	if (rpcvers != 2) {
		xput32(r, (u32)1);	/* denied: RPC version */
		xput32(r, (u32)0);
		xput32(r, (u32)2);
		xput32(r, (u32)2);
		return 1;
	}
	xput32(r, (u32)0);		/* accepted */
	xput32(r, (u32)0);		/* verifier: none */
	xput32(r, (u32)0);
	statpos = r->len;
	xput32(r, (u32)RPC_SUCCESS);
	lo = hi = 0;
	if (prog == PMAP_PROG) {
		lo = hi = 2;
		st = vers == 2 ? pmap_proc(proc, a, r) : RPC_PROG_MISMATCH;
	} else if (prog == MNT_PROG) {
		lo = 1;
		hi = 3;
		st = vers >= 1 && vers <= 3 ? mnt_proc(vers, proc, a, r) :
		    RPC_PROG_MISMATCH;
	} else if (prog == NFS_PROG) {
		lo = hi = 3;
		st = vers == 3 ? nfs_proc(proc, a, r) : RPC_PROG_MISMATCH;
	} else
		st = RPC_PROG_UNAVAIL;
	if (st == RPC_SUCCESS && r->err)
		st = RPC_SYSERR;
	if (st != RPC_SUCCESS) {
		r->len = statpos;
		r->err = 0;
		xput32(r, (u32)st);
		if (st == RPC_PROG_MISMATCH) {
			xput32(r, lo);
			xput32(r, hi);
		}
	}
	if (debug > 1 || (debug && prog != NFS_PROG))
		fprintf(stderr, "%s:%u prog %lu v%lu proc %lu -> %d, %u bytes\n",
		    ipstr(rq.peer), rq.port, prog, vers, proc, st, r->len);
	return 1;
}

static void
closeconn(c, why)
	register struct conn *c;
	char *why;
{
	if (debug)
		fprintf(stderr, "%s:%u closed: %s\n", ipstr(c->peer), c->port,
		    why);
	(void)close(c->fd);
	c->fd = -1;
}

static int
writeall(fd, p, n)
	int fd;
	register u8 *p;
	register unsigned n;
{
	register int k;

	while (n) {
		k = write(fd, (char *)p, n);
		if (k <= 0) {
			if (k < 0 && errno == EINTR)
				continue;
			return -1;
		}
		p += k;
		n -= k;
	}
	return 0;
}

/* Wait for more of a request that has started to arrive. */
static int
readmore(fd, p, n)
	int fd;
	u8 *p;
	unsigned n;
{
	fd_set rd;
	struct timeval tv;
	int k;

	for (;;) {
		FD_ZERO(&rd);
		FD_SET(fd, &rd);
		tv.tv_sec = 30;
		tv.tv_usec = 0;
		k = select(fd + 1, &rd, (fd_set *)0, (fd_set *)0, &tv);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			return -1;
		k = read(fd, (char *)p, n);
		if (k < 0 && errno == EINTR)
			continue;
		return k > 0 ? k : -1;
	}
}

/*
 * A connection has data.  Take what is there, answer every complete
 * request in it, and wait only for the rest of a request that was cut
 * short, so one busy client cannot keep the others waiting for long.
 */
static void
serve_tcp(c)
	register struct conn *c;
{
	struct xbuf a, r;
	u32 mark;
	unsigned have, pos, len, i;
	int n;

	n = read(c->fd, (char *)reqbuf, REQSIZE);
	if (n <= 0) {
		if (n < 0 && errno == EINTR)
			return;
		closeconn(c, n < 0 ? "read error" : "by client");
		return;
	}
	have = n;
	pos = 0;
	rq.peer = c->peer;
	rq.port = c->port;
	c->used = ++connclock;
	while (pos < have) {
		if (pos && have - pos < 4) {
			for (i = 0; i < have - pos; i++)
				reqbuf[i] = reqbuf[pos + i];
			have -= pos;
			pos = 0;
		}
		while (have - pos < 4) {
			n = readmore(c->fd, reqbuf + have, 4 - (have - pos));
			if (n < 0) {
				closeconn(c, "request cut short");
				return;
			}
			have += n;
		}
		mark = getbe32(reqbuf + pos);
		/* one fragment per request, as every client sends it */
		if (!(mark & 0x80000000UL) ||
		    (mark & 0x7fffffffUL) > REQSIZE - 4 ||
		    (mark & 0x7fffffffUL) < 24) {
			closeconn(c, "bad record mark");
			return;
		}
		len = (unsigned)(mark & 0x7fffffffUL);
		if (have - pos - 4 < len) {
			if (pos) {
				for (i = 0; i < have - pos; i++)
					reqbuf[i] = reqbuf[pos + i];
				have -= pos;
				pos = 0;
			}
			while (have < len + 4) {
				n = readmore(c->fd, reqbuf + have,
				    len + 4 - have);
				if (n < 0) {
					closeconn(c, "request cut short");
					return;
				}
				have += n;
			}
		}
		xinit(&a, reqbuf + pos + 4, len);
		a.len = len;
		xinit(&r, repbuf + 4, REPSIZE);
		if (rpc_serve(&a, &r)) {
			putbe32(repbuf, 0x80000000UL | (u32)r.len);
			if (writeall(c->fd, repbuf, r.len + 4) < 0) {
				closeconn(c, "write error");
				return;
			}
		}
		pos += len + 4;
	}
}

static void
serve_udp(fd)
	int fd;
{
	struct sockaddr_in sin;
	struct xbuf a, r;
	u8 *p;
#ifdef PDP11
	int slen;
#else
	socklen_t slen;
#endif
	int n;

	slen = sizeof(sin);
	n = recvfrom(fd, (char *)reqbuf, REQSIZE, 0,
	    (struct sockaddr *)&sin, &slen);
	if (n < 24)
		return;
	p = (u8 *)&sin.sin_addr;
	rq.peer = getbe32(p);
	rq.port = ntohs(sin.sin_port);
	xinit(&a, reqbuf, (unsigned)n);
	a.len = n;
	xinit(&r, repbuf + 4, REPSIZE);
	if (rpc_serve(&a, &r))
		(void)sendto(fd, (char *)repbuf + 4, r.len, 0,
		    (struct sockaddr *)&sin, sizeof(sin));
}

/*
 * Set the socket buffer sizes.  They are set on the listening sockets
 * too, so that a connection starts out with them rather than with the
 * system's.
 */
static void
setwindow(fd)
	int fd;
{
	static int told;
	int sz;

	sz = rcvwin;
	if ((setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&sz,
	    sizeof(sz)) < 0 ||
	    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *)&sz,
	    sizeof(sz)) < 0) && !told) {
		told = 1;
		syslog(LOG_WARNING, "socket buffers of %d not accepted: %m",
		    rcvwin);
	}
}

static void
newconn(lfd)
	int lfd;
{
	struct sockaddr_in sin;
	register struct conn *c, *old;
	u8 *p;
#ifdef PDP11
	int slen;
#else
	socklen_t slen;
#endif
	int fd, one;

	slen = sizeof(sin);
	fd = accept(lfd, (struct sockaddr *)&sin, &slen);
	if (fd < 0)
		return;
	old = conns;
	for (c = conns; c < &conns[MAXCONN]; c++) {
		if (c->fd < 0) {
			old = c;
			break;
		}
		if (c->used < old->used)
			old = c;
	}
	c = old;
	if (c->fd >= 0)
		closeconn(c, "making room");	/* the idlest one goes */
	one = 1;
	(void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&one,
	    sizeof(one));
	(void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&one,
	    sizeof(one));
	setwindow(fd);
	p = (u8 *)&sin.sin_addr;
	c->fd = fd;
	c->peer = getbe32(p);
	c->port = ntohs(sin.sin_port);
	c->used = ++connclock;
}

static int
mksock(type, port)
	int type;
	unsigned port;
{
	struct sockaddr_in sin;
	int fd, one;

	fd = socket(AF_INET, type, 0);
	if (fd < 0)
		return -1;
	one = 1;
	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
	    sizeof(one));
	if (type == SOCK_STREAM)
		setwindow(fd);
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons((unsigned short)port);
	if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0 ||
	    (type == SOCK_STREAM && listen(fd, 5) < 0)) {
		(void)close(fd);
		return -1;
	}
	return fd;
}

static void
onhup(sig)
	int sig;
{
	gothup = 1;
}

static void
onterm(sig)
	int sig;
{
	gotterm = 1;
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	static int lfd[4];	/* nfs tcp, nfs udp, portmap tcp, portmap udp */
	register struct conn *c;
	fd_set rd;
	struct timeval tv;
	time_t now;
	int i, n, maxfd;

	while (argc > 1 && argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'd':
			debug++;
			break;
		case 'f':
		case 'p':
		case 'P':
		case 'w':
			if (argc < 3)
				goto usage;
			if (argv[1][1] == 'f')
				expfile = argv[2];
			else if (argv[1][1] == 'p')
				nfsport = atoi(argv[2]);
			else if (argv[1][1] == 'w') {
				rcvwin = atoi(argv[2]);
				if (rcvwin < MINWIN || rcvwin > MAXWIN) {
					fprintf(stderr,
				    "nfsd: window must be %d to %d bytes\n",
					    MINWIN, MAXWIN);
					exit(1);
				}
			} else
				pmapport = atoi(argv[2]);
			argc--;
			argv++;
			break;
		default:
		usage:
			fprintf(stderr,
	    "usage: nfsd [-d] [-f exports] [-p port] [-P pmapport] [-w window]\n");
			exit(1);
		}
		argc--;
		argv++;
	}
	openlog("nfsd", debug ? LOG_PID | LOG_PERROR : LOG_PID, LOG_DAEMON);
	if (readexports() || nexports == 0) {
		fprintf(stderr, "nfsd: nothing usable in %s (see syslog)\n",
		    expfile);
		exit(1);
	}
	lfd[0] = mksock(SOCK_STREAM, nfsport);
	lfd[1] = mksock(SOCK_DGRAM, nfsport);
	lfd[2] = lfd[3] = -1;
	if (pmapport) {
		lfd[2] = mksock(SOCK_STREAM, pmapport);
		lfd[3] = mksock(SOCK_DGRAM, pmapport);
	}
	if (lfd[0] < 0 || lfd[1] < 0 ||
	    (pmapport && (lfd[2] < 0 || lfd[3] < 0))) {
		perror("nfsd: cannot bind (already running?)");
		exit(1);
	}
	if (!debug)
		(void)daemon(0, 0);
	(void)umask(0);
	(void)signal(SIGPIPE, SIG_IGN);
	(void)signal(SIGHUP, onhup);
	(void)signal(SIGTERM, onterm);
	if (debug)
		(void)signal(SIGINT, onterm);
	for (c = conns; c < &conns[MAXCONN]; c++)
		c->fd = -1;
	fh_init();
	cred_init();
	(void)time(&now);
	putbe32(wverf, (u32)now);
	putbe32(wverf + 4, (u32)getpid());
	syslog(LOG_INFO, "serving %d director%s, window %d", nexports,
	    nexports == 1 ? "y" : "ies", rcvwin);

	while (!gotterm) {
		if (gothup) {
			gothup = 0;
			cred_become(0, 0, 0, (u32 *)0);
			fh_closeall();
			if (readexports())
				syslog(LOG_ERR, "%s has mistakes", expfile);
			else
				syslog(LOG_INFO, "%s read again", expfile);
		}
		FD_ZERO(&rd);
		maxfd = 0;
		for (i = 0; i < 4; i++)
			if (lfd[i] >= 0) {
				FD_SET(lfd[i], &rd);
				if (lfd[i] > maxfd)
					maxfd = lfd[i];
			}
		for (c = conns; c < &conns[MAXCONN]; c++)
			if (c->fd >= 0) {
				FD_SET(c->fd, &rd);
				if (c->fd > maxfd)
					maxfd = c->fd;
			}
		/* files are not held open once the clients go quiet */
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		n = select(maxfd + 1, &rd, (fd_set *)0, (fd_set *)0,
		    fh_nopen ? &tv : (struct timeval *)0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			syslog(LOG_ERR, "select: %m");
			break;
		}
		if (n == 0) {
			fh_closeall();
			continue;
		}
		for (c = conns; c < &conns[MAXCONN]; c++)
			if (c->fd >= 0 && FD_ISSET(c->fd, &rd))
				serve_tcp(c);
		if (FD_ISSET(lfd[1], &rd))
			serve_udp(lfd[1]);
		if (lfd[3] >= 0 && FD_ISSET(lfd[3], &rd))
			serve_udp(lfd[3]);
		if (FD_ISSET(lfd[0], &rd))
			newconn(lfd[0]);
		if (lfd[2] >= 0 && FD_ISSET(lfd[2], &rd))
			newconn(lfd[2]);
	}
	fh_closeall();
	syslog(LOG_INFO, "exiting");
	exit(0);
}
