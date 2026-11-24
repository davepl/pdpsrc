/* XDR encoding and decoding into bounded byte buffers. */
#include "nfsd.h"

u32
getbe32(p)
	u8 *p;
{
	return (((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) |
	    (u32)p[3]) & M32;
}

void
putbe32(p, v)
	u8 *p;
	u32 v;
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

void
xinit(x, base, cap)
	struct xbuf *x;
	u8 *base;
	unsigned cap;
{
	x->base = base;
	x->cap = cap;
	x->len = 0;
	x->pos = 0;
	x->err = 0;
}

static int
room(x, n)
	struct xbuf *x;
	unsigned n;
{
	if (x->err || n > x->cap - x->len) {
		x->err = 1;
		return 0;
	}
	return 1;
}

static int
avail(x, n)
	struct xbuf *x;
	unsigned n;
{
	if (x->err || n > x->len - x->pos) {
		x->err = 1;
		return 0;
	}
	return 1;
}

void
xput32(x, v)
	struct xbuf *x;
	u32 v;
{
	if (!room(x, 4))
		return;
	putbe32(x->base + x->len, v);
	x->len += 4;
}

void
xput64(x, v)
	struct xbuf *x;
	struct u64 *v;
{
	xput32(x, v->hi);
	xput32(x, v->lo);
}

void
xputfixed(x, p, n)
	struct xbuf *x;
	u8 *p;
	unsigned n;
{
	unsigned pad;

	pad = (4 - (n & 3)) & 3;
	if (!room(x, n + pad))
		return;
	if (n)
		memcpy(x->base + x->len, p, n);
	x->len += n;
	while (pad--)
		x->base[x->len++] = 0;
}

void
xputopaque(x, p, n)
	struct xbuf *x;
	u8 *p;
	unsigned n;
{
	xput32(x, (u32)n);
	xputfixed(x, p, n);
}

void
xputstr(x, s)
	struct xbuf *x;
	char *s;
{
	xputopaque(x, (u8 *)s, (unsigned)strlen(s));
}

u32
xget32(x)
	struct xbuf *x;
{
	u32 v;

	if (!avail(x, 4))
		return 0;
	v = getbe32(x->base + x->pos);
	x->pos += 4;
	return v;
}

void
xget64(x, v)
	struct xbuf *x;
	struct u64 *v;
{
	v->hi = xget32(x);
	v->lo = xget32(x);
}

void
xskip(x, n)
	struct xbuf *x;
	unsigned n;
{
	n = (n + 3) & ~3;
	if (!avail(x, n))
		return;
	x->pos += n;
}

void
xgetfixed(x, p, n)
	struct xbuf *x;
	u8 *p;
	unsigned n;
{
	unsigned r;

	r = (n + 3) & ~3;
	if (!avail(x, r))
		return;
	memcpy(p, x->base + x->pos, n);
	x->pos += r;
}

u8 *
xgetref(x, n)
	struct xbuf *x;
	unsigned *n;
{
	u32 len;
	u8 *p;

	len = xget32(x);
	if (x->err || len > (u32)(x->len - x->pos)) {
		x->err = 1;
		*n = 0;
		return (u8 *)0;
	}
	*n = (unsigned)len;
	p = x->base + x->pos;
	xskip(x, (unsigned)len);
	return p;
}

unsigned
xgetopaque(x, p, max)
	struct xbuf *x;
	u8 *p;
	unsigned max;
{
	unsigned n;
	u8 *q;

	q = xgetref(x, &n);
	if (q == 0)
		return 0;
	if (n > max) {
		x->err = 1;
		return 0;
	}
	memcpy(p, q, n);
	return n;
}

int
xgetstr(x, s, max)
	struct xbuf *x;
	char *s;
	unsigned max;
{
	unsigned n;
	u8 *q;

	q = xgetref(x, &n);
	if (q == 0) {
		s[0] = 0;
		return x->err ? -1 : 0;
	}
	if (n >= max) {
		s[0] = 0;
		return -1;
	}
	memcpy(s, q, n);
	s[n] = 0;
	return (int)n;
}
