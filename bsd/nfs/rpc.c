/* ONC RPC version 2 over TCP: record marking, AUTH_UNIX and portmap. */
#include "nfs.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#ifdef PDP11
#include <arpa/inet.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>
#endif

struct rpcauth rpc_cred;
u8 rpc_sbuf[NFS_BUFSIZE];
u8 rpc_rbuf[NFS_BUFSIZE];
int rpc_verbose;
int rpc_timeout = 30;

static u32 rpc_xid;
static u32 cur_xid;

#define PMAP_PROG 100000UL
#define PMAP_VERS 2UL
#define PMAPPROC_GETPORT 3UL
#define IPPROTO_TCP_NUM 6UL

int
rpc_resolve(host, addr)
	char *host;
	u32 *addr;
{
	struct hostent *h;
	u8 *p;
	u32 a;
	int i, n, dots;
	char *s;

	/* dotted quad without relying on inet_addr's return type */
	dots = 0;
	for (s = host; *s; s++) {
		if (*s == '.')
			dots++;
		else if (*s < '0' || *s > '9')
			break;
	}
	if (*s == 0 && dots == 3) {
		a = 0;
		s = host;
		for (i = 0; i < 4; i++) {
			n = 0;
			while (*s >= '0' && *s <= '9')
				n = n * 10 + (*s++ - '0');
			if (n > 255)
				return EINVAL;
			a = (a << 8) | (u32)n;
			if (*s == '.')
				s++;
		}
		*addr = a & M32;
		return 0;
	}
	h = gethostbyname(host);
	if (h == 0 || h->h_length != 4)
		return EHOSTUNREACH;
	p = (u8 *)h->h_addr;
	*addr = getbe32(p);
	return 0;
}

static void
fillsin(sin, addr, port)
	struct sockaddr_in *sin;
	u32 addr;
	unsigned port;
{
	u8 *p;

	memset((char *)sin, 0, sizeof(*sin));
	sin->sin_family = AF_INET;
	sin->sin_port = htons((unsigned short)port);
	p = (u8 *)&sin->sin_addr;
	putbe32(p, addr);
}

static int
doconnect(c)
	struct rpcconn *c;
{
	struct sockaddr_in sin;
	int fd, port, e;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return errno;
	if (c->resv) {
		for (port = 1023; port >= 512; port--) {
			fillsin(&sin, 0UL, (unsigned)port);
			if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) == 0)
				break;
			if (errno != EADDRINUSE) {
				e = errno;
				close(fd);
				return e;
			}
		}
		if (port < 512) {
			close(fd);
			return EADDRINUSE;
		}
	}
	fillsin(&sin, c->addr, c->port);
	if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		e = errno;
		close(fd);
		return e;
	}
	c->fd = fd;
	return 0;
}

int
rpc_open(c, addr, port, prog, vers, resv)
	struct rpcconn *c;
	u32 addr;
	unsigned port;
	u32 prog, vers;
	int resv;
{
	c->fd = -1;
	c->addr = addr;
	c->port = (unsigned short)port;
	c->prog = prog;
	c->vers = vers;
	c->resv = resv;
	if (rpc_xid == 0)
		rpc_xid = ((u32)time((time_t *)0) << 8) ^ (u32)getpid();
	if (rpc_cred.machine[0] == 0) {
		gethostname(rpc_cred.machine, sizeof(rpc_cred.machine) - 1);
		rpc_cred.uid = (u32)geteuid();
		rpc_cred.gid = (u32)getgid();
		rpc_cred.ngroups = 0;
	}
	return doconnect(c);
}

void
rpc_close(c)
	struct rpcconn *c;
{
	if (c->fd >= 0)
		close(c->fd);
	c->fd = -1;
}

void
rpc_setcred(uid, gid, ngroups, groups)
	u32 uid, gid;
	int ngroups;
	u32 *groups;
{
	int i;

	if (rpc_cred.machine[0] == 0)
		gethostname(rpc_cred.machine, sizeof(rpc_cred.machine) - 1);
	rpc_cred.uid = uid;
	rpc_cred.gid = gid;
	if (ngroups > 16)
		ngroups = 16;
	rpc_cred.ngroups = ngroups;
	for (i = 0; i < ngroups; i++)
		rpc_cred.groups[i] = groups[i];
}

void
rpc_begin(c, x, proc)
	struct rpcconn *c;
	struct xbuf *x;
	u32 proc;
{
	unsigned mark, i;

	xinit(x, rpc_sbuf, sizeof(rpc_sbuf));
	x->len = 4;			/* record mark */
	rpc_xid = (rpc_xid + 1) & M32;
	cur_xid = rpc_xid;
	xput32(x, cur_xid);
	xput32(x, 0UL);			/* CALL */
	xput32(x, 2UL);			/* RPC version */
	xput32(x, c->prog);
	xput32(x, c->vers);
	xput32(x, proc);
	xput32(x, 1UL);			/* AUTH_UNIX */
	mark = x->len;
	xput32(x, 0UL);			/* length, patched below */
	xput32(x, (u32)time((time_t *)0));
	xputstr(x, rpc_cred.machine);
	xput32(x, rpc_cred.uid);
	xput32(x, rpc_cred.gid);
	xput32(x, (u32)rpc_cred.ngroups);
	for (i = 0; i < (unsigned)rpc_cred.ngroups; i++)
		xput32(x, rpc_cred.groups[i]);
	putbe32(x->base + mark, (u32)(x->len - mark - 4));
	xput32(x, 0UL);			/* verifier AUTH_NONE */
	xput32(x, 0UL);
}

static int
waitread(fd)
	int fd;
{
	fd_set r;
	struct timeval tv;
	int n;

	if (rpc_timeout <= 0)
		return 0;
	FD_ZERO(&r);
	FD_SET(fd, &r);
	tv.tv_sec = rpc_timeout;
	tv.tv_usec = 0;
	n = select(fd + 1, &r, (fd_set *)0, (fd_set *)0, &tv);
	if (n < 0)
		return errno;
	if (n == 0)
		return ETIMEDOUT;
	return 0;
}

static int
readn(fd, p, n)
	int fd;
	u8 *p;
	unsigned n;
{
	int r, e;

	while (n > 0) {
		if ((e = waitread(fd)) != 0)
			return e;
		r = read(fd, (char *)p, n);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return errno;
		}
		if (r == 0)
			return ECONNRESET;
		p += r;
		n -= (unsigned)r;
	}
	return 0;
}

static int
writen(fd, p, n)
	int fd;
	u8 *p;
	unsigned n;
{
	int r;

	while (n > 0) {
		r = write(fd, (char *)p, n);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return errno;
		}
		p += r;
		n -= (unsigned)r;
	}
	return 0;
}

/* Receive one complete record into rpc_rbuf. */
static int
recvrecord(fd, lenp)
	int fd;
	unsigned *lenp;
{
	u8 hdr[4], junk[64];
	u32 h, flen;
	unsigned len, chunk;
	int e, last, over;

	len = 0;
	over = 0;
	do {
		if ((e = readn(fd, hdr, 4)) != 0)
			return e;
		h = getbe32(hdr);
		last = (h & 0x80000000UL) != 0;
		flen = h & 0x7fffffffUL;
		while (flen > 0) {
			if (!over && flen <= (u32)(sizeof(rpc_rbuf) - len)) {
				if ((e = readn(fd, rpc_rbuf + len, (unsigned)flen)) != 0)
					return e;
				len += (unsigned)flen;
				flen = 0;
			} else {
				over = 1;
				chunk = flen > sizeof(junk) ? sizeof(junk) : (unsigned)flen;
				if ((e = readn(fd, junk, chunk)) != 0)
					return e;
				flen -= chunk;
			}
		}
	} while (!last);
	if (over)
		return EMSGSIZE;
	*lenp = len;
	return 0;
}

static int
transact(c, x, r)
	struct rpcconn *c;
	struct xbuf *x;
	struct xbuf *r;
{
	unsigned len;
	u32 v;
	int e, tries;

	if (c->fd < 0 && (e = doconnect(c)) != 0)
		return e;
	if ((e = writen(c->fd, x->base, x->len)) != 0)
		return e;
	for (tries = 0; tries < 8; tries++) {
		if ((e = recvrecord(c->fd, &len)) != 0)
			return e;
		xinit(r, rpc_rbuf, sizeof(rpc_rbuf));
		r->len = len;
		if (xget32(r) != cur_xid)
			continue;	/* stale reply from an earlier call */
		if (xget32(r) != 1UL)
			return EPROTO_RPC;
		v = xget32(r);		/* reply_stat */
		if (v != 0UL)
			return EACCES;	/* MSG_DENIED: auth or version */
		xget32(r);		/* verifier flavour */
		xskip(r, (unsigned)xget32(r));
		v = xget32(r);		/* accept_stat */
		if (r->err)
			return EPROTO_RPC;
		switch ((int)v) {
		case 0:
			return 0;
		case 1:		/* PROG_UNAVAIL */
		case 2:		/* PROG_MISMATCH */
			return EPROTONOSUPPORT;
		case 3:		/* PROC_UNAVAIL */
			return EOPNOTSUPP;
		default:
			return EINVAL;
		}
	}
	return EPROTO_RPC;
}

int
rpc_call(c, x, r)
	struct rpcconn *c;
	struct xbuf *x;
	struct xbuf *r;
{
	int e, attempt;

	if (x->err)
		return E2BIG;
	putbe32(x->base, 0x80000000UL | (u32)(x->len - 4));
	for (attempt = 0; attempt < 3; attempt++) {
		e = transact(c, x, r);
		if (e == 0 || e == EACCES || e == EPROTONOSUPPORT ||
		    e == EOPNOTSUPP || e == EINVAL || e == EMSGSIZE)
			return e;
		/* connection-level failure: reconnect and resend */
		if (rpc_verbose)
			fprintf(stderr, "rpc: %s; reconnecting\n", nfs_strerror(e));
		rpc_close(c);
	}
	return e;
}

int
pmap_getport(addr, prog, vers, port)
	u32 addr, prog, vers;
	unsigned *port;
{
	struct rpcconn pc;
	struct xbuf x, r;
	u32 p;
	int e;

	if ((e = rpc_open(&pc, addr, 111, PMAP_PROG, PMAP_VERS, 0)) != 0)
		return e;
	rpc_begin(&pc, &x, PMAPPROC_GETPORT);
	xput32(&x, prog);
	xput32(&x, vers);
	xput32(&x, IPPROTO_TCP_NUM);
	xput32(&x, 0UL);
	e = rpc_call(&pc, &x, &r);
	rpc_close(&pc);
	if (e)
		return e;
	p = xget32(&r);
	if (r.err)
		return EPROTO_RPC;
	if (p == 0 || p > 65535UL)
		return EPROTONOSUPPORT;
	*port = (unsigned)p;
	return 0;
}
