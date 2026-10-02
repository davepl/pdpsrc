/*
 *   Z M . C
 *    Copyright 1994 Omen Technology Inc All Rights Reserved
 *    ZMODEM protocol primitives
 *
 * Entry point Functions:
 *	zsbhdr(type, hdr) send binary header
 *	zshhdr(type, hdr) send hex header
 *	zgethdr(hdr) receive header - binary or hex
 *	zsdata(buf, len, frameend) send data
 *	zrdata(buf, len) receive data
 *	stohdr(pos) store position data in Txhdr
 *	long rclhdr(hdr) recover position offset from header
 * 
 *
 *	This version implements numerous enhancements including ZMODEM
 *	Run Length Encoding and variable length headers.  These
 *	features were not funded by the original Telenet development
 *	contract.
 * 
 *  This software may be freely used for educational (didactic
 *  only) purposes.  This software may also be freely used to
 *  support file transfer operations to or from licensed Omen
 *  Technology products.  Use with other commercial or shareware
 *  programs (Crosstalk, Procomm, etc.) REQUIRES REGISTRATION.
 *
 *  Any programs which use part or all of this software must be
 *  provided in source form with this notice intact except by
 *  written permission from Omen Technology Incorporated.
 * 
 * Use of this software for commercial or administrative purposes
 * except when exclusively limited to interfacing Omen Technology
 * products requires a per port license payment of $20.00 US per
 * port (less in quantity).  Use of this code by inclusion,
 * decompilation, reverse engineering or any other means
 * constitutes agreement to these conditions and acceptance of
 * liability to license the materials and payment of reasonable
 * legal costs necessary to enforce this license agreement.
 *
 *
 *		Omen Technology Inc
 *		Post Office Box 4681
 *		Portland OR 97208
 *
 *	This code is made available in the hope it will be useful,
 *	BUT WITHOUT ANY WARRANTY OF ANY KIND OR LIABILITY FOR ANY
 *	DAMAGES OF ANY KIND.
 *
 */

#ifndef CANFDX
#include "zmodem.h"
int Rxtimeout = 100;		/* Tenths of seconds to wait for something */
#endif

int zsbh32(), zsendline(), zputhex(), zsda32(), zsdar32();
int zrdat32(), zrdatr32(), zdlread(), garbitch(), noxrd7();
int zrbhd32(), zrbhdr(), zgethex(), zrhhdr(), zgeth1();

/* Globals used by ZMODEM functions */
int Rxframeind;		/* ZBIN ZBIN32, or ZHEX type of frame */
int Rxtype;		/* Type of header received */
int Rxhlen;		/* Length of header received */
int Rxcount;		/* Count of data bytes received */
char Rxhdr[ZMAXHLEN];	/* Received header */
char Txhdr[ZMAXHLEN];	/* Transmitted header */
long Rxpos;		/* Received file position */
long Txpos;		/* Transmitted file position */
int Txfcs32;		/* TURE means send binary frames with 32 bit FCS */
int Crc32t;		/* Controls 32 bit CRC being sent */
			/* 1 == CRC32,  2 == CRC32 + RLE */
int Crc32r;		/* Indicates/controls 32 bit CRC being received */
			/* 0 == CRC16,  1 == CRC32,  2 == CRC32 + RLE */
int Usevhdrs;		/* Use variable length headers */
int Znulls;		/* Number of nulls to send at beginning of ZDATA hdr */
char Attn[ZATTNLEN+1];	/* Attention string rx sends to tx on err */
char *Altcan;		/* Alternate canit string */

static lastsent;	/* Last char we sent */

static char *frametypes[] = {
	"No Response to Error Correction Request",	/* -4 */
	"No Carrier Detect",		/* -3 */
	"TIMEOUT",		/* -2 */
	"ERROR",		/* -1 */
#define FTOFFSET 4
	"ZRQINIT",
	"ZRINIT",
	"ZSINIT",
	"ZACK",
	"ZFILE",
	"ZSKIP",
	"ZNAK",
	"ZABORT",
	"ZFIN",
	"ZRPOS",
	"ZDATA",
	"ZEOF",
	"ZFERR",
	"ZCRC",
	"ZCHALLENGE",
	"ZCOMPL",
	"ZCAN",
	"ZFREECNT",
	"ZCOMMAND",
	"ZSTDERR",
	"xxxxx"
#define FRTYPES 22	/* Total number of frame types in this array */
			/*  not including psuedo negative entries */
};

static char badcrc[] = "Bad CRC";

/* Send ZMODEM binary header hdr of type type */
zsbhdr(len, type, hdr)
register char *hdr;
{
	register int n;
	register unsigned short crc;

#ifndef DSZ
	vfile("zsbhdr: %c %d %s %lx", Usevhdrs?'v':'f', len,
	  frametypes[type+FTOFFSET], rclhdr(hdr));
#endif
	if (type == ZDATA)
		for (n = Znulls; --n >=0; )
			xsendline(0);

	xsendline(ZPAD); xsendline(ZDLE);

	switch (Crc32t=Txfcs32) {
	case 2:
		zsbh32(len, hdr, type, Usevhdrs?ZVBINR32:ZBINR32);
		flushmo();  break;
	case 1:
		zsbh32(len, hdr, type, Usevhdrs?ZVBIN32:ZBIN32);  break;
	default:
		if (Usevhdrs) {
			xsendline(ZVBIN);
			zsendline(len);
		}
		else
			xsendline(ZBIN);
		zsendline(type);
		crc = updcrc(type, 0);

		for (n=len; --n >= 0; ++hdr) {
			zsendline(*hdr);
			crc = updcrc((0377& *hdr), crc);
		}
		crc = updcrc(0,updcrc(0,crc));
		zsendline(((int)(crc>>8)));
		zsendline(crc);
	}
	if (type != ZDATA)
		flushmo();
}


/* Send ZMODEM binary header hdr of type type */
zsbh32(len, hdr, type, flavour)
register char *hdr;
{
	register int n;
	register unsigned long crc;

	xsendline(flavour); 
	if (Usevhdrs) 
		zsendline(len);
	zsendline(type);
	crc = 0xFFFFFFFFL; crc = UPDC32(type, crc);

	for (n=len; --n >= 0; ++hdr) {
		crc = UPDC32((0377 & *hdr), crc);
		zsendline(*hdr);
	}
	crc = ~crc;
	for (n=4; --n >= 0;) {
		zsendline((int)crc);
		crc >>= 8;
	}
}

/* Send ZMODEM HEX header hdr of type type */
zshhdr(len, type, hdr)
register char *hdr;
{
	register int n;
	register unsigned short crc;

#ifndef DSZ
	vfile("zshhdr: %c %d %s %lx", Usevhdrs?'v':'f', len,
	  frametypes[type+FTOFFSET], rclhdr(hdr));
#endif
	sendline(ZPAD); sendline(ZPAD); sendline(ZDLE);
	if (Usevhdrs) {
		sendline(ZVHEX);
		zputhex(len);
	}
	else
		sendline(ZHEX);
	zputhex(type);
	Crc32t = 0;

	crc = updcrc(type, 0);
	for (n=len; --n >= 0; ++hdr) {
		zputhex(*hdr); crc = updcrc((0377 & *hdr), crc);
	}
	crc = updcrc(0,updcrc(0,crc));
	zputhex(((int)(crc>>8))); zputhex(crc);

	/* Make it printable on remote machine */
	sendline(015); sendline(0212);
	/*
	 * Uncork the remote in case a fake XOFF has stopped data flow
	 */
	if (type != ZFIN && type != ZACK)
		sendline(021);
	flushmo();
}

/*
 * Send binary array buf of length length, with ending ZDLE sequence frameend
 */
static char *Zendnames[] = { "ZCRCE", "ZCRCG", "ZCRCQ", "ZCRCW"};
zsdata(buf, length, frameend)
register char *buf;
{
	register unsigned short crc;

#ifndef DSZ
	vfile("zsdata: %d %s", length, Zendnames[frameend-ZCRCE&3]);
#endif
	switch (Crc32t) {
	case 1:
		zsda32(buf, length, frameend);  break;
	case 2:
		zsdar32(buf, length, frameend);  break;
	default:
		crc = 0;
		for (;--length >= 0; ++buf) {
			zsendline(*buf); crc = updcrc((0377 & *buf), crc);
		}
		xsendline(ZDLE); xsendline(frameend);
		crc = updcrc(frameend, crc);

		crc = updcrc(0,updcrc(0,crc));
		zsendline(((int)(crc>>8))); zsendline(crc);
	}
	if (frameend == ZCRCW)
		xsendline(XON);
	if (frameend != ZCRCG)
		flushmo();
}

zsda32(buf, length, frameend)
register char *buf;
{
	register int c;
	register unsigned long crc;

	crc = 0xFFFFFFFFL;
	for (;--length >= 0; ++buf) {
		c = *buf & 0377;
		zsendline(c);
		crc = UPDC32(c, crc);
	}
	xsendline(ZDLE); xsendline(frameend);
	crc = UPDC32(frameend, crc);

	crc = ~crc;
	for (c=4; --c >= 0;) {
		zsendline((int)crc);  crc >>= 8;
	}
}

/*
 * Receive array buf of max length with ending ZDLE sequence
 *  and CRC.  Returns the ending character or error code.
 *  NB: On errors may store length+1 bytes!
 */
zrdata(buf, length)
register char *buf;
{
	register int c;
	register unsigned short crc;
	register char *end;
	register int d;

	switch (Crc32r) {
	case 1:
		return zrdat32(buf, length);
	case 2:
		return zrdatr32(buf, length);
	}

	crc = Rxcount = 0;  end = buf + length;
	while (buf <= end) {
		if ((c = zdlread()) & ~0377) {
crcfoo:
			switch (c) {
			case GOTCRCE:
			case GOTCRCG:
			case GOTCRCQ:
			case GOTCRCW:
				crc = updcrc((d=c)&0377, crc);
				if ((c = zdlread()) & ~0377)
					goto crcfoo;
				crc = updcrc(c, crc);
				if ((c = zdlread()) & ~0377)
					goto crcfoo;
				crc = updcrc(c, crc);
				if (crc & 0xFFFF) {
					zperr1(badcrc);
					return ERROR;
				}
				Rxcount = length - (end - buf);
#ifndef DSZ
				vfile("zrdata: %d  %s", Rxcount,
				 Zendnames[d-GOTCRCE&3]);
#endif
				return d;
			case GOTCAN:
				zperr1("Sender Canceled");
				return ZCAN;
			case TIMEOUT:
				zperr1("TIMEOUT");
				return c;
			default:
				garbitch(); return c;
			}
		}
		*buf++ = c;
		crc = updcrc(c, crc);
	}
#ifdef DSZ
	garbitch(); 
#else
	zperr1("Data subpacket too long");
#endif
	return ERROR;
}

zrdat32(buf, length)
register char *buf;
{
	register int c;
	register unsigned long crc;
	register char *end;
	register int d;

	crc = 0xFFFFFFFFL;  Rxcount = 0;  end = buf + length;
	while (buf <= end) {
		if ((c = zdlread()) & ~0377) {
crcfoo:
			switch (c) {
			case GOTCRCE:
			case GOTCRCG:
			case GOTCRCQ:
			case GOTCRCW:
				d = c;  c &= 0377;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if (crc != 0xDEBB20E3) {
					zperr1(badcrc);
					return ERROR;
				}
				Rxcount = length - (end - buf);
#ifndef DSZ
				vfile("zrdat32: %d %s", Rxcount,
				 Zendnames[d-GOTCRCE&3]);
#endif
				return d;
			case GOTCAN:
				zperr1("Sender Canceled");
				return ZCAN;
			case TIMEOUT:
				zperr1("TIMEOUT");
				return c;
			default:
				garbitch(); return c;
			}
		}
		*buf++ = c;
		crc = UPDC32(c, crc);
	}
	zperr1("Data subpacket too long");
	return ERROR;
}

garbitch()
{
	zperr1("Garbled data subpacket");
}

/*
 * Read a ZMODEM header to hdr, either binary or hex.
 *
 *   Set Rxhlen to size of header (default 4) (valid iff good hdr)
 *  On success, set Zmodem to 1, set Rxpos and return type of header.
 *   Otherwise return negative on error.
 *   Return ERROR instantly if ZCRCW sequence, for fast error recovery.
 */
zgethdr(hdr)
char *hdr;
{
	register int c, n, cancount;

	n = Zrwindow + Baudrate;
	Rxframeind = Rxtype = 0;

startover:
	cancount = 5;
again:
	switch (c = readline(Rxtimeout)) {
	case 021: case 0221:
		goto again;
	case RCDO:
	case TIMEOUT:
		goto fifi;
	case CAN:
gotcan:
		if (--cancount <= 0) {
			c = ZCAN; goto fifi;
		}
		switch (c = readline(Rxtimeout)) {
		case TIMEOUT:
			goto again;
		case ZCRCW:
			switch (readline(Rxtimeout)) {
			case TIMEOUT:
				c = ERROR; goto fifi;
			case RCDO:
				goto fifi;
			default:
				goto agn2;
			}
		case RCDO:
			goto fifi;
		default:
			break;
		case CAN:
			if (--cancount <= 0) {
				c = ZCAN; goto fifi;
			}
			goto again;
		}
	/* **** FALL THRU TO **** */
	default:
agn2:
		if ( --n == 0) {
			c = GCOUNT;  goto fifi;
		}
		goto startover;
	case ZPAD:		/* This is what we want. */
		break;
	}
	cancount = 5;
splat:
	switch (c = noxrd7()) {
	case ZPAD:
		goto splat;
	case RCDO:
	case TIMEOUT:
		goto fifi;
	default:
		goto agn2;
	case ZDLE:		/* This is what we want. */
		break;
	}


	Rxhlen = 4;		/* Set default length */
	Rxframeind = c = noxrd7();
	switch (c) {
	case ZVBIN32:
		if ((Rxhlen = c = zdlread()) < 0)
			goto fifi;
		if (c > ZMAXHLEN)
			goto agn2;
		Crc32r = 1;  c = zrbhd32(hdr); break;
	case ZBIN32:
		if (Usevhdrs)
			goto agn2;
		Crc32r = 1;  c = zrbhd32(hdr); break;
	case ZVBINR32:
		if ((Rxhlen = c = zdlread()) < 0)
			goto fifi;
		if (c > ZMAXHLEN)
			goto agn2;
		Crc32r = 2;  c = zrbhd32(hdr); break;
	case ZBINR32:
		if (Usevhdrs)
			goto agn2;
		Crc32r = 2;  c = zrbhd32(hdr); break;
	case RCDO:
	case TIMEOUT:
		goto fifi;
	case ZVBIN:
		if ((Rxhlen = c = zdlread()) < 0)
			goto fifi;
		if (c > ZMAXHLEN)
			goto agn2;
		Crc32r = 0;  c = zrbhdr(hdr); break;
	case ZBIN:
		if (Usevhdrs)
			goto agn2;
		Crc32r = 0;  c = zrbhdr(hdr); break;
	case ZVHEX:
		if ((Rxhlen = c = zgethex()) < 0)
			goto fifi;
		if (c > ZMAXHLEN)
			goto agn2;
		Crc32r = 0;  c = zrhhdr(hdr); break;
	case ZHEX:
		if (Usevhdrs)
			goto agn2;
		Crc32r = 0;  c = zrhhdr(hdr); break;
	case CAN:
		goto gotcan;
	default:
		goto agn2;
	}
	for (n = Rxhlen; ++n < ZMAXHLEN; )	/* Clear unused hdr bytes */
		hdr[n] = 0;
	Rxpos = hdr[ZP3] & 0377;
	Rxpos = (Rxpos<<8) + (hdr[ZP2] & 0377);
	Rxpos = (Rxpos<<8) + (hdr[ZP1] & 0377);
	Rxpos = (Rxpos<<8) + (hdr[ZP0] & 0377);
fifi:
	switch (c) {
	case GOTCAN:
		c = ZCAN;
	/* **** FALL THRU TO **** */
	case ZNAK:
	case ZCAN:
	case ERROR:
	case TIMEOUT:
	case RCDO:
	case GCOUNT:
		zperr2("Got %s", frametypes[c+FTOFFSET]);
	/* **** FALL THRU TO **** */
#ifndef DSZ
	default:
		if (c >= -4 && c <= FRTYPES)
			vfile("zgethdr: %c %d %s %lx", Rxframeind, Rxhlen,
			  frametypes[c+FTOFFSET], Rxpos);
		else
			vfile("zgethdr: %c %d %lx", Rxframeind, c, Rxpos);
#endif
	}
	/* Use variable length headers if we got one */
	if (c >= 0 && c <= FRTYPES && Rxframeind & 040)
		Usevhdrs = 1;
	return c;
}

/* Receive a binary style header (type and position) */
zrbhdr(hdr)
register char *hdr;
{
	register int c, n;
	register unsigned short crc;

	if ((c = zdlread()) & ~0377)
		return c;
	Rxtype = c;
	crc = updcrc(c, 0);

	for (n=Rxhlen; --n >= 0; ++hdr) {
		if ((c = zdlread()) & ~0377)
			return c;
		crc = updcrc(c, crc);
		*hdr = c;
	}
	if ((c = zdlread()) & ~0377)
		return c;
	crc = updcrc(c, crc);
	if ((c = zdlread()) & ~0377)
		return c;
	crc = updcrc(c, crc);
	if (crc & 0xFFFF) {
		zperr1(badcrc);
		return ERROR;
	}
#ifdef ZMODEM
	Protocol = ZMODEM;
#endif
	Zmodem = 1;
	return Rxtype;
}

/* Receive a binary style header (type and position) with 32 bit FCS */
zrbhd32(hdr)
register char *hdr;
{
	register int c, n;
	register unsigned long crc;

	if ((c = zdlread()) & ~0377)
		return c;
	Rxtype = c;
	crc = 0xFFFFFFFFL; crc = UPDC32(c, crc);
#ifdef DEBUGZ
	vfile("zrbhd32 c=%X  crc=%lX", c, crc);
#endif

	for (n=Rxhlen; --n >= 0; ++hdr) {
		if ((c = zdlread()) & ~0377)
			return c;
		crc = UPDC32(c, crc);
		*hdr = c;
#ifdef DEBUGZ
		vfile("zrbhd32 c=%X  crc=%lX", c, crc);
#endif
	}
	for (n=4; --n >= 0;) {
		if ((c = zdlread()) & ~0377)
			return c;
		crc = UPDC32(c, crc);
#ifdef DEBUGZ
		vfile("zrbhd32 c=%X  crc=%lX", c, crc);
#endif
	}
	if (crc != 0xDEBB20E3) {
		zperr1(badcrc);
		return ERROR;
	}
#ifdef ZMODEM
	Protocol = ZMODEM;
#endif
	Zmodem = 1;
	return Rxtype;
}


/* Receive a hex style header (type and position) */
zrhhdr(hdr)
char *hdr;
{
	register int c;
	register unsigned short crc;
	register int n;

	if ((c = zgethex()) < 0)
		return c;
	Rxtype = c;
	crc = updcrc(c, 0);

	for (n=Rxhlen; --n >= 0; ++hdr) {
		if ((c = zgethex()) < 0)
			return c;
		crc = updcrc(c, crc);
		*hdr = c;
	}
	if ((c = zgethex()) < 0)
		return c;
	crc = updcrc(c, crc);
	if ((c = zgethex()) < 0)
		return c;
	crc = updcrc(c, crc);
	if (crc & 0xFFFF) {
		zperr1(badcrc); return ERROR;
	}
	c = readline(Rxtimeout);
	if (c < 0)
		return c;
	c = readline(Rxtimeout);
#ifdef ZMODEM
	Protocol = ZMODEM;
#endif
	Zmodem = 1;
	if (c < 0)
		return c;
	return Rxtype;
}

/* Send a byte as two hex digits */
zputhex(c)
register int c;
{
	static char	digits[]	= "0123456789abcdef";

#ifdef DEBUGZ
	if (Verbose>8)
		vfile("zputhex: %02X", c);
#endif
	sendline(digits[(c&0xF0)>>4]);
	sendline(digits[(c)&0xF]);
}

/*
 * Send character c with ZMODEM escape sequence encoding.
 */
zsendline(c)
register c;
{
	switch (c &= 0377) {
	case 0377:
		lastsent = c;
		if (Zctlesc || Zsendmask[32]) {
			xsendline(ZDLE);  c = ZRUB1;
		}
		xsendline(c);
		break;
	case ZDLE:
		xsendline(ZDLE);  xsendline (lastsent = (c ^= 0100));
		break;
	case 021: case 023:
	case 0221: case 0223:
		xsendline(ZDLE);  c ^= 0100;  xsendline(lastsent = c);
		break;
	default:
		if (((c & 0140) == 0) && (Zctlesc || Zsendmask[c & 037])) {
			xsendline(ZDLE);  c ^= 0100;
		}
		xsendline(lastsent = c);
	}
}

/* Decode two lower case hex digits into an 8 bit byte value */
zgethex()
{
	register int c;

	c = zgeth1();
#ifdef DEBUGZ
	if (Verbose>8)
		vfile("zgethex: %02X", c);
#endif
	return c;
}
zgeth1()
{
	register int c, n;

	if ((c = noxrd7()) < 0)
		return c;
	n = c - '0';
	if (n > 9)
		n -= ('a' - ':');
	if (n & ~0xF)
		return ERROR;
	if ((c = noxrd7()) < 0)
		return c;
	c -= '0';
	if (c > 9)
		c -= ('a' - ':');
	if (c & ~0xF)
		return ERROR;
	c += (n<<4);
	return c;
}

/*
 * Read a byte, checking for ZMODEM escape encoding
 *  including CAN*5 which represents a quick abort
 */
zdlread()
{
	register int c;

again:
	/* Quick check for non control characters */
	if ((c = readline(Rxtimeout)) & 0140)
		return c;
	switch (c) {
	case ZDLE:
		break;
	case 023:
	case 0223:
	case 021:
	case 0221:
		goto again;
	default:
		if (Zctlesc && !(c & 0140)) {
			goto again;
		}
		return c;
	}
again2:
	if ((c = readline(Rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = readline(Rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = readline(Rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = readline(Rxtimeout)) < 0)
		return c;
	switch (c) {
	case CAN:
		return GOTCAN;
	case ZCRCE:
	case ZCRCG:
	case ZCRCQ:
	case ZCRCW:
		return (c | GOTOR);
	case ZRUB0:
		return 0177;
	case ZRUB1:
		return 0377;
	case 023:
	case 0223:
	case 021:
	case 0221:
		goto again2;
	default:
		if (Zctlesc && ! (c & 0140)) {
			goto again2;
		}
		if ((c & 0140) ==  0100)
			return (c ^ 0100);
		break;
	}
	if (Verbose>1)
		zperr2("Bad escape sequence %x", c);
	return ERROR;
}

/*
 * Read a character from the modem line with timeout.
 *  Eat parity, XON and XOFF characters.
 */
noxrd7()
{
	register int c;

	for (;;) {
		if ((c = readline(Rxtimeout)) < 0)
			return c;
		switch (c &= 0177) {
		case XON:
		case XOFF:
			continue;
		default:
			if (Zctlesc && !(c & 0140))
				continue;
		case '\r':
		case '\n':
		case ZDLE:
			return c;
		}
	}
	/* NOTREACHED */
}

/* Store long integer pos in Txhdr */
stohdr(pos)
long pos;
{
	Txhdr[ZP0] = pos;
	Txhdr[ZP1] = pos>>8;
	Txhdr[ZP2] = pos>>16;
	Txhdr[ZP3] = pos>>24;
}

/* Recover a long integer from a header */
long
rclhdr(hdr)
register char *hdr;
{
	register long l;

	l = (hdr[ZP3] & 0377);
	l = (l << 8) | (hdr[ZP2] & 0377);
	l = (l << 8) | (hdr[ZP1] & 0377);
	l = (l << 8) | (hdr[ZP0] & 0377);
	return l;
}

/* End of zm.c */
