#define VERSION "3.48 01-27-98"
#define PUBDIR "/usr/spool/uucppublic"
#define SMALL 1

/*
 **************************************************************************
 *
 * sz.c By Chuck Forsberg,  Omen Technology INC
 *    Copyright 1997 Omen Technology Inc All Rights Reserved
 * 
 *********************************************************************
 *********************************************************************
 * 
 *
 *	This version implements numerous enhancements including ZMODEM
 *	Run Length Encoding and variable length headers.  These
 *	features were not funded by the original Telenet development
 *	contract.
 * 
 * 
 * This software may be freely used for educational (didactic
 * only) purposes.  "Didactic" means it is used as a study item
 * in a course teaching the workings of computer protocols.
 * 
 * This software may also be freely used to support file transfer
 * operations to or from duly licensed Omen Technology products.
 * This includes DSZ, GSZ, ZCOMM, Professional-YAM and PowerCom.
 * Institutions desiring to use rz/sz this way should add the
 * following to the sz compile line:	-DCOMPL
 * Programs based on stolen or public domain ZMODEM materials are
 * not included.  Use with other commercial or shareware programs
 * (Crosstalk, Procomm, etc.) REQUIRES REGISTRATION.
 * 
 *
 *  Any programs which incorporate part or all of this code must be
 *  provided in source form with this notice intact except by
 *  prior written permission from Omen Technology Incorporated.
 *  This includes compiled executables of this program.
 *
 *   The .doc files and the file "mailer.rz" must also be included.
 * 
 * Use of this software for commercial or administrative purposes
 * except when exclusively limited to interfacing Omen Technology
 * products requires license payment of $20.00 US per user
 * (less in quantity, see mailer.rz).  Use of this code by
 * inclusion, decompilation, reverse engineering or any other means
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
 *  USG UNIX (3.0) ioctl conventions courtesy Jeff Martin
 */

char *Copyrsz = "Copyright 1997 Omen Technology Inc All Rights Reserved";

char *substr();

#define LOGFILE "/tmp/szlog"
#define LOGFILE2 "szlog"
#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
extern int errno;
#define STATIC

int chkinvok(), usage(), chartest(), countem(), getzrxinit(), zsendcmd();
int wcsend(), wcs(), getnak(), wctxpn(), wctx(), zsendfile(), wcputsec();
int filbuf(), initzsendmsk(), sendzsinit(), zsendfdata(), getinsync();
void saybibi();

#define PATHLEN 1000
#define OK 0
#define FALSE 0
#ifdef TRUE
#undef TRUE
#endif
#define TRUE 1
#define ERROR (-1)
/* Ward Christensen / CP/M parameters - Don't change these! */
#define ENQ 005
#define CAN ('X'&037)
#define XOFF ('s'&037)
#define XON ('q'&037)
#define SOH 1
#define STX 2
#define EOT 4
#define ACK 6
#define NAK 025
#define SYN 026
#define CPMEOF 032
#define WANTCRC 0103	/* send C not NAK to get crc not checksum */
#define WANTG 0107	/* Send G not NAK to get nonstop batch xmsn */
#define TIMEOUT (-2)
#define RCDO (-3)
#define GCOUNT (-4)
#define RETRYMAX 10


#define HOWMANY 2
STATIC int Zmodem=0;		/* ZMODEM protocol requested by receiver */
unsigned Baudrate = 9600;		/* Default, set by first mode() call */
STATIC unsigned Txwindow;	/* Control the size of the transmitted window */
STATIC unsigned Txwspac;	/* Spacing between zcrcq requests */
STATIC unsigned Txwcnt;	/* Counter used to space ack requests */
STATIC long Lrxpos;	/* Receiver's last reported offset */
STATIC int errors;
char endmsg[80] = {0};	/* Possible message to display on exit */
char Zsendmask[33];	/* Additional control chars to mask */

#include "rbsb.c"	/* most of the system dependent stuff here */

#include "crctab.c"

STATIC int Filesleft;
STATIC long Totalleft;

/*
 * Attention string to be executed by receiver to interrupt streaming data
 *  when an error is detected.  A pause (0336) may be needed before the
 *  ^C (03) or after it.
 */
#ifdef READCHECK
STATIC char Myattn[] = { 0 };
#else
#ifdef USG
STATIC char Myattn[] = { 03, 0336, 0 };
#endif
#endif

FILE *in;

STATIC int Canseek = 1;	/* 1: Can seek 0: only rewind -1: neither (pipe) */

#ifndef SMALL
#ifndef TXBSIZE
#define TXBSIZE 32768
#endif

#define TXBMASK (TXBSIZE-1)
STATIC char Txb[TXBSIZE + 1024];	/* Circular buffer for file reads */
STATIC char *txbuf = Txb;		/* Pointer to current file segment */
#else
char txbuf[1024];
#endif


STATIC long vpos = 0;		/* Number of bytes read from file */

STATIC char Lastrx;
STATIC char Crcflg;
STATIC int Modem2=0;		/* XMODEM Protocol - don't send pathnames */
STATIC int Restricted=0;	/* restricted; no /.. or ../ in filenames */
STATIC int Fullname=0;		/* transmit full pathname */
STATIC int Unlinkafter=0;	/* Unlink file after it is sent */
STATIC int Dottoslash=0;	/* Change foo.bar.baz to foo/bar/baz */
STATIC int firstsec;
STATIC int errcnt=0;		/* number of files unreadable */
STATIC int Skipbitch=0;
STATIC int Skipcount=0;		/* Count of skipped files */
STATIC int blklen=128;		/* length of transmitted records */
STATIC int Optiong;		/* Let it rip no wait for sector ACK's */
STATIC int Eofseen;		/* EOF seen on input set by zfilbuf */
STATIC int BEofseen;		/* EOF seen on input set by fooseek */
STATIC int Totsecs;		/* total number of sectors this file */
STATIC int Filcnt=0;		/* count of number of files opened */
STATIC unsigned Rxbuflen=16384;	/* Receiver's max buffer length */
STATIC long Tframlen = 0;	/* Override for tx frame length */
STATIC int blkopt=0;		/* Override value for zmodem blklen */
STATIC int Rxflags = 0;
STATIC long bytcnt, maxbytcnt;
STATIC int Wantfcs32 = TRUE;	/* want to send 32 bit FCS */
STATIC char Lzconv;	/* Local ZMODEM file conversion request */
STATIC char Lzmanag;	/* Local ZMODEM file management request */
STATIC int Lskipnocor;
STATIC char Lztrans;
STATIC int Command;		/* Send a command, then exit. */
STATIC char *Cmdstr;		/* Pointer to the command string */
STATIC int Cmdack1;		/* Rx ACKs command, then do it */
STATIC int Exitcode;
STATIC int Test;		/* 1= Force receiver to send Attn, etc with qbf. */
			/* 2= Character transparency test */
STATIC char *qbf=
 "The quick brown fox jumped over the lazy dog's back 1234567890\r\n";
STATIC long Lastsync;	/* Last offset to which we got a ZRPOS */
STATIC int Beenhereb4;		/* How many times we've been ZRPOS'd here */
STATIC int Ksendstr;		/* 1= Send esc-?-3-4-l to remote kermit */
STATIC char *ksendbuf = "\033[?34l";

STATIC jmp_buf intrjmp;	/* For the interrupt on RX CAN */


/* called by signal interrupt or terminate to clean things up */
void
bibi(n)
{
	canit(); fflush(stdout); mode(0);
	fprintf(stderr, "sz: caught signal %d; exiting\n", n);
	if (n == SIGQUIT)
		abort();
	if (n == 99)
		fprintf(stderr, "mode(2) in rbsb.c not implemented!!\n");
	exit(3);
}

/* Called when ZMODEM gets an interrupt (^X) */
void
onintr(c)
{
	signal(SIGINT, SIG_IGN);
	longjmp(intrjmp, -1);
}

STATIC int Zctlesc;	/* Encode control characters */
STATIC int Nozmodem = 0;	/* If invoked as "sb" */
STATIC char *Progname = "sz";
STATIC int Zrwindow = 1400;	/* RX window size (controls garbage count) */


/*
 * Log an error
 */
#ifndef POSIX
void
zperr1(s,p,u)
char *s, *p, *u;
{
	if (Verbose <= 0)
		return;
	fprintf(stderr, "Retry %d: ", errors);
	fprintf(stderr, s);
	fprintf(stderr, "\n");
}

void
zperr2(s,p,u)
char *s, *p, *u;
{
	if (Verbose <= 0)
		return;
	fprintf(stderr, "Retry %d: ", errors);
	fprintf(stderr, s, p);
	fprintf(stderr, "\n");
}

void
zperr3(s,p,u)
char *s, *p, *u;
{
	if (Verbose <= 0)
		return;
	fprintf(stderr, "Retry %d: ", errors);
	fprintf(stderr, s, p, u);
	fprintf(stderr, "\n");
}

#endif

#include "zm.c"
#include "zmr.c"

main(argc, argv)
char *argv[];
{
	register char *cp;
	register npats;
	char **patts;

	if ((cp = getenv("ZNULLS")) && *cp)
		Znulls = atoi(cp);
	if ((cp=getenv("SHELL")) && (substr(cp, "rsh") || substr(cp, "rksh")))
		Restricted=TRUE;
	inittty();
	chkinvok(argv[0]);

	Rxtimeout = 600;
	npats=0;
	if (argc<2)
		usage();
	while (--argc) {
		cp = *++argv;
		if (*cp++ == '-' && *cp) {
			while ( *cp) {
				if (isdigit(*cp)) {
					++cp;  continue;
				}
				switch(*cp++) {
				case '\\':
					 *cp = toupper(*cp);  continue;
				case '+':
					Lzmanag = ZMAPND; break;
				case 'a':
					if (Nozmodem || Modem2)
						usage();
					Lzconv = ZCNL;  break;
				case 'b':
					Lzconv = ZCBIN; break;
				case 'c':
					Lzmanag = ZMCHNG;  break;
				case 'd':
					++Dottoslash;
					/* **** FALL THROUGH TO **** */
				case 'f':
					Fullname=TRUE; break;
		                case 'g' :
					Ksendstr = TRUE; break;
				case 'e':
					Zctlesc = 1; break;
				case 'k':
					blklen=1024; break;
				case 'L':
					if (isdigit(*cp))
						blkopt = atoi(cp);
					else {
						if (--argc < 1)
							usage();
						blkopt = atoi(*++argv);
					}
					if (blkopt<24 || blkopt>1024)
						usage();
					break;
				case 'l':
					if (isdigit(*cp))
						Tframlen = atol(cp);
					else {
						if (--argc < 1)
							usage();
						Tframlen = atol(*++argv);
					}
					if (Tframlen<32 || Tframlen>65535L)
						usage();
					break;
				case 'N':
					Lzmanag = ZMNEWL;  break;
				case 'n':
					Lzmanag = ZMNEW;  break;
				case 'o':
					Wantfcs32 = FALSE; break;
				case 'p':
					Lzmanag = ZMPROT;  break;
				case 'r':
					if (Lzconv == ZCRESUM)
						Lzmanag = (Lzmanag & ZMMASK) | ZMCRC;
					Lzconv = ZCRESUM; break;
				case 'T':
					chartest(1); chartest(2);
					mode(0);  exit(0);
				case 'u':
					++Unlinkafter; break;
				case 'v':
					++Verbose; break;
				case 'w':
					if (isdigit(*cp))
						Txwindow = atoi(cp);
					else {
						if (--argc < 1)
							usage();
						Txwindow = atoi(*++argv);
					}
					if (Txwindow < 256)
						Txwindow = 256;
					Txwindow = (Txwindow/64) * 64;
					Txwspac = Txwindow/4;
					if (blkopt > Txwspac
					 || (!blkopt && Txwspac < 1024))
						blkopt = Txwspac;
					break;
				case 'x':
					Skipbitch = 1;  break;
				case 'Y':
					Lskipnocor = TRUE;
					/* **** FALLL THROUGH TO **** */
				case 'y':
					Lzmanag = ZMCLOB; break;
				case 'Z':
				case 'z':
					Lztrans = ZTRLE;  break;
				default:
					usage();
				}
			}
		}
		else if (Command) {
			if (argc != 1) {
				usage();
			}
			Cmdstr = *argv;
		}
		else if ( !npats && argc>0) {
			if (argv[0][0]) {
				npats=argc;
				patts=argv;
			}
		}
	}
	if (npats < 1 && !Command && !Test) 
		usage();
	if (Verbose) {
		if (freopen(LOGFILE, "a", stderr)==NULL)
			if (freopen(LOGFILE2, "a", stderr)==NULL) {
				printf("Can't open log file!");
				exit(2);
			}
		setbuf(stderr, NULL);
	}
	vfile("%s %s for %s tty=%s\n", Progname, VERSION, OS, Nametty);

	mode(3);

	if (signal(SIGINT, bibi) == SIG_IGN) {
		signal(SIGINT, SIG_IGN); signal(SIGKILL, SIG_IGN);
	} else {
		signal(SIGINT, bibi); signal(SIGKILL, bibi);
	}
#ifdef SIGQUIT
	signal(SIGQUIT, SIG_IGN);
#endif
#ifdef SIGTERM
	signal(SIGTERM, bibi);
#endif

	countem(npats, patts);

	if (!Modem2 && !Nozmodem) {
		if (Ksendstr)
			printf(ksendbuf);
		printf("rz\r");  fflush(stdout);
		stohdr(0x80L);	/* Show we can var header */
		if (Command)
			Txhdr[ZF0] = ZCOMMAND;
		zshhdr(4, ZRQINIT, Txhdr);
	}
	fflush(stdout);


	if (Command) {
		if (getzrxinit()) {
			Exitcode=1; canit();
		}
		else if (zsendcmd(Cmdstr, 1+strlen(Cmdstr))) {
			Exitcode=1; canit();
		}
	} else if (wcsend(npats, patts)==ERROR) {
		Exitcode=1;
		canit();
		sleep(20);
	}
	if (Skipcount) {
		printf("%d file(s) skipped by receiver request\r\n", Skipcount);
		if (Verbose) fprintf(stderr,
		  "%d file(s) skipped by receiver request\r\n", Skipcount);
	}
	if (endmsg[0]) {
		printf("\r\n%s: %s\r\n", Progname, endmsg);
		if (Verbose)
			fprintf(stderr, "%s\r\n", endmsg);
	}
	printf("%s %s finished.\r\n", Progname, VERSION);
	fflush(stdout);
	mode(0);
	if(errcnt || Exitcode)
		exit(1);

#ifndef REGISTERED
	/* Removing or disabling this code without registering is theft */
	if (!Usevhdrs)  {
		printf("\n\n\n**** UNREGISTERED COPY *****\r\n");
		printf("\n\n\nPlease read the License Agreement in sz.doc\n");
		fflush(stdout);
		sleep(10);
	}
#endif
	exit(0);
	/*NOTREACHED*/
}

/* Say "bibi" to the receiver, try to do it cleanly */
void
saybibi()
{
	for (;;) {
		stohdr(0L);		/* CAF Was zsbhdr - minor change */
		zshhdr(4, ZFIN, Txhdr);	/*  to make debugging easier */
		switch (zgethdr(Rxhdr)) {
		case ZFIN:
			sendline('O'); sendline('O'); flushmo();
		case ZCAN:
		case TIMEOUT:
			return;
		}
	}
}

wcsend(argc, argp)
char *argp[];
{
	register n;

	Crcflg=FALSE;
	firstsec=TRUE;
	bytcnt = maxbytcnt = -1;
	vfile("wcsend: argc=%d", argc);
	if (Nozmodem) {
		printf("Start your local YMODEM receive.     ");
		fflush(stdout);
	}
	for (n=0; n<argc; ++n) {
		Totsecs = 0;
		if (wcs(argp[n])==ERROR)
			return ERROR;
	}
	Totsecs = 0;
	if (Filcnt==0) {	/* bitch if we couldn't open ANY files */
		if (!Nozmodem && !Modem2) {
			Command = TRUE;
			Cmdstr = "echo \"sz: Can't open any requested files\"";
			if (getnak()) {
				Exitcode=1; canit();
			}
			if (!Zmodem)
				canit();
			else if (zsendcmd(Cmdstr, 1+strlen(Cmdstr))) {
				Exitcode=1; canit();
			}
			Exitcode = 1; return OK;
		}
		canit();
		sprintf(endmsg, "Can't open any requested files");
		return ERROR;
	}
	if (Zmodem)
		saybibi();
	else if ( !Modem2)
		wctxpn("");
	return OK;
}

wcs(oname)
char *oname;
{
	register c;
	register char *p;
	struct stat f;
	char name[PATHLEN];

	strcpy(name, oname);
	vfile("wcs: name=%s", name);

	if (Restricted) {
		/* restrict pathnames to current tree or uucppublic */
		if ( substr(name, "../")
		 || (name[0]== '/' && strncmp(name, PUBDIR, strlen(PUBDIR))) ) {
			canit();  sprintf(endmsg,"Security Violation");
			return ERROR;
		}
	}

#ifdef TXBSIZE
	if ( !strcmp(name, "-")) {
		if ((p = getenv("ONAME")) && *p)
			strcpy(name, p);
		else
			sprintf(name, "s%d.sz", getpid());
		in = stdin;
	}
	else
#endif
		in=fopen(name, "r");

	if (in==NULL) {
		++errcnt;
		return OK;	/* pass over it, there may be others */
	}
	BEofseen = Eofseen = 0;  vpos = 0;

	/* Check for directory */
	fstat(fileno(in), &f);
#ifdef POSIX
	if (S_ISDIR(f.st_mode))
#else
	c = f.st_mode & S_IFMT;
	if (c == S_IFDIR || c == S_IFBLK)
#endif
	{
		fclose(in);
		return OK;
	}

	++Filcnt;
	switch (wctxpn(name)) {
	case ZSKIP:
	case ZFERR:
		return OK;
	case OK:
		break;
	default:
		return ERROR;
	}
	if (!Zmodem && wctx(f.st_size))
		return ERROR;

	if (Unlinkafter)
		unlink(oname);

	return 0;
}

/*
 * generate and transmit pathname block consisting of
 *  pathname (null terminated),
 *  file length, mode time and file mode in octal
 *  as provided by the Unix fstat call.
 *  N.B.: modifies the passed name, may extend it!
 */
wctxpn(name)
char *name;
{
	register char *p, *q;
	char name2[PATHLEN];
	struct stat f;

	vfile("wctxpn: %s", name);
	if (Modem2) {
		if (*name && fstat(fileno(in), &f)!= -1) {
			fprintf(stderr, "Sending %s, %ld XMODEM blocks. ",
			  name, (long) ((127+f.st_size)>>7));
		}
		printf("Start your local XMODEM receive.     ");
		fflush(stdout);
		return OK;
	}
	zperr2("Awaiting pathname nak for %s", *name?name:"<END>");
	if ( !Zmodem)
		if (getnak())
			return ERROR;

	q = (char *) 0;
	if (Dottoslash) {		/* change . to . */
		for (p=name; *p; ++p) {
			if (*p == '/')
				q = p;
			else if (*p == '.')
				*(q=p) = '/';
		}
		if (q && strlen(++q) > 8) {	/* If name>8 chars */
			q += 8;			/*   make it .ext */
			strcpy(name2, q);	/* save excess of name */
			*q = '.';
			strcpy(++q, name2);	/* add it back */
		}
	}

	for (p=name, q=txbuf ; *p; )
		if ((*q++ = *p++) == '/' && !Fullname)
			q = txbuf;
	*q++ = 0;
	p=q;
	while (q < (txbuf + 1024))
		*q++ = 0;
	if (*name) {
		if (fstat(fileno(in), &f)!= -1)
			sprintf(p, "%lu %lo %o 3 %d %ld", (long) f.st_size,
			  (long) f.st_mtime,
			  f.st_mode, Filesleft, Totalleft);
		Totalleft -= f.st_size;
	}
	if (--Filesleft <= 0)
		Filesleft = Totalleft = 0;
	if (Totalleft < 0)
		Totalleft = 0;

	/* force 1k blocks if name won't fit in 128 byte block */
	if (txbuf[125])
		blklen=1024;
	else {		/* A little goodie for IMP/KMD */
		txbuf[127] = (f.st_size + 127) >>7;
		txbuf[126] = (f.st_size + 127) >>15;
	}
	vfile("wctxpn: %s", p);
	if (Zmodem)
		return zsendfile(txbuf, 1+strlen(p)+(p-txbuf));
	if (wcputsec(txbuf, 0, 128)==ERROR)
		return ERROR;
	return OK;
}

getnak()
{
	register firstch;

	Lastrx = 0;
	for (;;) {
		switch (firstch = readline(800)) {
		case ZPAD:
			if (getzrxinit())
				return ERROR;
			return FALSE;
		case TIMEOUT:
			sprintf(endmsg, "Timeout waiting for ZRINIT");
			return TRUE;
		case WANTG:
#ifdef MODE2OK
			mode(2);	/* Set cbreak, XON/XOFF, etc. */
#endif
			Optiong = TRUE;
			blklen=1024;
		case WANTCRC:
			Crcflg = TRUE;
		case NAK:
			return FALSE;
		case CAN:
			if ((firstch = readline(20)) == CAN && Lastrx == CAN) {
				sprintf(endmsg, "Got CAN waiting to send file");
				return TRUE;
			}
		default:
			break;
		}
		Lastrx = firstch;
	}
}


wctx(flen)
long flen;
{
	register int thisblklen;
	register int sectnum, attempts, firstch;
	long charssent;

	charssent = 0;  firstsec=TRUE;  thisblklen = blklen;
	vfile("wctx:file length=%ld", flen);

	while ((firstch=readline(Rxtimeout))!=NAK && firstch != WANTCRC
	  && firstch != WANTG && firstch!=TIMEOUT && firstch!=CAN)
		;
	if (firstch==CAN) {
		zperr1("Receiver CANcelled");
		return ERROR;
	}
	if (firstch==WANTCRC)
		Crcflg=TRUE;
	if (firstch==WANTG)
		Crcflg=TRUE;
	sectnum=0;
	for (;;) {
		if (flen <= (charssent + 896L))
			thisblklen = 128;
		if ( !filbuf(txbuf, thisblklen))
			break;
		if (wcputsec(txbuf, ++sectnum, thisblklen)==ERROR)
			return ERROR;
		charssent += thisblklen;
	}
	fclose(in);
	attempts=0;
	do {
		purgeline();
		sendline(EOT);
		flushmo();
		++attempts;
	}
		while ((firstch=(readline(Rxtimeout)) != ACK) && attempts < RETRYMAX);
	if (attempts == RETRYMAX) {
		zperr1("No ACK on EOT");
		return ERROR;
	}
	else
		return OK;
}

wcputsec(buf, sectnum, cseclen)
char *buf;
int sectnum;
int cseclen;	/* data length of this sector to send */
{
	register checksum, wcj;
	register char *cp;
	unsigned oldcrc;
	int firstch;
	int attempts;

	firstch=0;	/* part of logic to detect CAN CAN */

	if (Verbose>1)
		fprintf(stderr, "Sector %3d %2dk\n", Totsecs, Totsecs/8 );
	for (attempts=0; attempts <= RETRYMAX; attempts++) {
		Lastrx= firstch;
		sendline(cseclen==1024?STX:SOH);
		sendline(sectnum);
		sendline(-sectnum -1);
		oldcrc=checksum=0;
		for (wcj=cseclen,cp=buf; --wcj>=0; ) {
			sendline(*cp);
			oldcrc=updcrc((0377& *cp), oldcrc);
			checksum += *cp++;
		}
		if (Crcflg) {
			oldcrc=updcrc(0,updcrc(0,oldcrc));
			sendline((int)oldcrc>>8);
			sendline((int)oldcrc);
		}
		else
			sendline(checksum);
		flushmo();

		if (Optiong) {
			firstsec = FALSE; return OK;
		}
		firstch = readline(Rxtimeout);
gotnak:
		switch (firstch) {
		case CAN:
			if(Lastrx == CAN) {
cancan:
				zperr1("Cancelled");  return ERROR;
			}
			break;
		case TIMEOUT:
			zperr1("Timeout on sector ACK"); continue;
		case WANTCRC:
			if (firstsec)
				Crcflg = TRUE;
		case NAK:
			zperr1("NAK on sector"); continue;
		case ACK: 
			firstsec=FALSE;
			Totsecs += (cseclen>>7);
			return OK;
		case ERROR:
			zperr1("Got burst for sector ACK"); break;
		default:
			zperr2("Got %02x for sector ACK", firstch); break;
		}
		for (;;) {
			Lastrx = firstch;
			if ((firstch = readline(Rxtimeout)) == TIMEOUT)
				break;
			if (firstch == NAK || firstch == WANTCRC)
				goto gotnak;
			if (firstch == CAN && Lastrx == CAN)
				goto cancan;
		}
	}
	zperr1("Retry Count Exceeded");
	return ERROR;
}

/* fill buf with count chars padding with ^Z for CPM */
filbuf(buf, count)
register char *buf;
{
	register m;

	m = read(fileno(in), buf, count);
	if (m <= 0)
		return 0;
	while (m < count)
		buf[m++] = 032;
	return count;
}

/* Fill buffer with blklen chars */
zfilbuf()
{
	int n;

#ifdef TXBSIZE
	vfile("zfilbuf: bytcnt =%lu vpos=%lu blklen=%d", bytcnt, vpos, blklen);
	/* We assume request is within buffer, or just beyond */
	txbuf = Txb + (bytcnt & TXBMASK);
	if (vpos <= bytcnt) {
		n = fread(txbuf, 1, blklen, in);

		vpos += n;
		if (n < blklen)
			Eofseen = 1;
		vfile("zfilbuf: n=%d vpos=%lu Eofseen=%d", n, vpos, Eofseen);
		return n;
	}
	if (vpos >= (bytcnt+blklen))
		return blklen;
	/* May be a short block if crash recovery etc. */
	Eofseen = BEofseen;
	return (vpos - bytcnt);
#else
	n = fread(txbuf, 1, blklen, in);
	if (n < blklen) {
		Eofseen = 1;
		vfile("zfilbuf: n=%d vpos=%lu Eofseen=%d", n, vpos, Eofseen);
	}
	return n;
#endif
}

#ifdef TXBSIZE
/* Replacement for brain damaged fseek function.  Returns 0==success */
fooseek(fptr, pos, whence)
FILE *fptr;
long pos;
{
	long m, n;

	vfile("fooseek: pos =%lu vpos=%lu Canseek=%d", pos, vpos, Canseek);
	/* Seek offset < current buffer */
	if (pos < (vpos -TXBSIZE +1024)) {
		BEofseen = 0;
		if (Canseek > 0) {
			vpos = pos & ~TXBMASK;
			if (vpos > pos)
				vpos -= TXBSIZE;
			vfile("seek to vpos=%ld", vpos);
			if (fseek(fptr, vpos, 0))
				return 1;
		}
		else if (Canseek == 0) {
			vfile("seek to 00000");
			if (fseek(fptr, vpos = 0L, 0))
				return 1;
		} else
			return 1;
		while (vpos < pos) {
			n = fread(Txb, (size_t)1, (size_t)TXBSIZE, fptr);
			vpos += n;
			vfile("n=%d vpos=%ld", n, vpos);
			if (n < TXBSIZE) {
				BEofseen = 1;
				break;
			}
		}
		vfile("vpos=%ld", vpos);
		return 0;
	}
	/* Seek offset > current buffer (Crash Recovery, etc.) */
	if (pos > vpos) {
		if (Canseek)
			if (fseek(fptr, vpos = (pos & ~TXBMASK), 0))
				return 1;
		while (vpos <= pos) {
			txbuf = Txb + (vpos & TXBMASK);
			m = TXBSIZE - (vpos & TXBMASK);
			vfile("m=%ld vpos=%ld", m,vpos);
				n = fread(txbuf, (size_t)1, (size_t)m, fptr);
			vfile("n=%ld vpos=%ld", n,vpos);
			vpos += n;
			vfile("bo=%d m=%ld vpos=%ld", txbuf-Txb,m,vpos);
			if (n < m) {
				BEofseen = 1;
				break;
			}
		}
		return 0;
	}
	/* Seek offset is within current buffer */
	vfile("within buffer: vpos=%ld", vpos);
	return 0;
}
#define fseek fooseek
#endif


/*
 * substr(string, token) searches for token in string s
 * returns pointer to token within string if found, NULL otherwise
 */
char *
substr(s, t)
register char *s,*t;
{
	register char *ss,*tt;
	/* search for first char of token */
	for (ss=s; *s; s++)
		if (*s == *t)
			/* compare token with substring */
			for (ss=s,tt=t; ;) {
				if (*tt == 0)
					return s;
				if (*ss++ != *tt++)
					break;
			}
	return NULL;
}

char *usinfo[] = {
	"Send Files and Commands with ZMODEM/YMODEM/XMODEM Protocol\n",
	"Usage:	sz [-+abcdefgklLnNuvwxyYZ] [-] file ...",
	"\t	zcommand [-egv] COMMAND",
	"\t	zcommandi [-egv] COMMAND",
	"\t	sb [-adfkuv] [-] file ...",
	"\t	sx [-akuv] [-] file",
	""
};

usage()
{
	char **pp;

	fprintf(stderr, "\n%s %s for %s by Chuck Forsberg, Omen Technology INC\n",
	 Progname, VERSION, OS);
	fprintf(stderr, "\t\t\042The High Reliability Software\042\n");
	for (pp=usinfo; **pp; ++pp)
		fprintf(stderr, "%s\n", *pp);
	fprintf(stderr,"\nCopyright (c) 1997 Omen Technology INC All Rights Reserved\n");
	fprintf(stderr,
	 "See sz.doc and README for option descriptions and licensing information.\n\n");
	fprintf(stderr,
	"This program is designed to talk to terminal programs,\nnot to be called by one.\n");
	exit(3);
}

/*
 * Get the receiver's init parameters
 */
getzrxinit()
{
	register n;
	struct stat f;

	for (n=10; --n>=0; ) {
		
		switch (zgethdr(Rxhdr)) {
		case ZCHALLENGE:	/* Echo receiver's challenge numbr */
			stohdr(Rxpos);
			zshhdr(4, ZACK, Txhdr);
			continue;
		case ZCOMMAND:		/* They didn't see out ZRQINIT */
			stohdr(0L);
			zshhdr(4, ZRQINIT, Txhdr);
			continue;
		case ZRINIT:
			if (Rxhlen==4 && (Rxhdr[ZF1] & ZRQNVH)) {
				stohdr(0x80L);	/* Show we can var header */
				zshhdr(4, ZRQINIT, Txhdr);
				continue;
			}
			Rxflags = 0377 & Rxhdr[ZF0];
#if COMPL
			Usevhdrs = 1;
#else
			Usevhdrs = Rxhdr[ZF1] & CANVHDR;
#endif
			Txfcs32 = (Wantfcs32 && (Rxflags & CANFC32));
			Zctlesc |= Rxflags & TESCCTL;
			if (Rxhdr[ZF1] & ZRRQQQ)	/* Escape ctrls */
				initzsendmsk(Rxhdr+ZRPXQQ);
			Rxbuflen = (0377 & Rxhdr[ZP0])+((0377 & Rxhdr[ZP1])<<8);
			if ( !(Rxflags & CANFDX))
				Txwindow = 0;
			vfile("Rxbuflen=%d Tframlen=%ld", Rxbuflen, Tframlen);
			signal(SIGINT, SIG_IGN);
#ifdef MODE2OK
			mode(2);	/* Set cbreak, XON/XOFF, etc. */
#endif

#ifndef READCHECK
#ifndef USG
			/* Use 1024 byte frames if no sample/interrupt */
			if (Rxbuflen < 32 || Rxbuflen > 1024) {
				Rxbuflen = 1024;
				vfile("Rxbuflen=%d", Rxbuflen);
			}
#endif
#endif

			/* Override to force shorter frame length */
			if (Rxbuflen && (Rxbuflen>Tframlen) && (Tframlen>=32))
				Rxbuflen = Tframlen;
			if ( !Rxbuflen && (Tframlen>=32))
				Rxbuflen = Tframlen;
			vfile("Rxbuflen=%d", Rxbuflen);


			/*
			 * If input is not a regular file, force ACK's to
			 *  prevent running beyond the buffer limits
			 */
			if ( !Command) {
				fstat(fileno(in), &f);
				if (
#ifdef POSIX
				    !S_ISREG(f.st_mode)
#else
				    (f.st_mode & S_IFMT) != S_IFREG
#endif
				    ) {
					Canseek = -1;
					f.st_size = 0;
					f.st_mtime = 0;
#ifdef TXBSIZE
					Txwindow = TXBSIZE - 1024;
					Txwspac = TXBSIZE/4;
#else
					sprintf(endmsg, "Can't seek on input");
					return ERROR;
#endif
				}
			}

			/* Set initial subpacket length */
			if (blklen < 1024) {	/* Command line override? */
				if (Baudrate > 300)
					blklen = 256;
				if (Baudrate > 1200)
					blklen = 512;
				if (Baudrate > 2400)
					blklen = 1024;
				if (Baudrate < 300)
					blklen = 1024;
			}
			if (Rxbuflen && blklen>Rxbuflen)
				blklen = Rxbuflen;
			if (blkopt && blklen > blkopt)
				blklen = blkopt;
			vfile("Rxbuflen=%d blklen=%d", Rxbuflen, blklen);
			vfile("Txwindow = %u Txwspac = %d", Txwindow, Txwspac);


			if (Lztrans == ZTRLE && (Rxflags & CANRLE))
				Txfcs32 = 2;
			else
				Lztrans = 0;

			return (sendzsinit());
		case ZCAN:
		case TIMEOUT:
			return ERROR;
		case ZRQINIT:
			if (Rxhdr[ZF0] == ZCOMMAND)
				continue;
		default:
			zshhdr(4, ZNAK, Txhdr);
			continue;
		}
	}
	return ERROR;
}

/* Send send-init information */
sendzsinit()
{
	register c;

	if (Myattn[0] == '\0' && (!Zctlesc || (Rxflags & TESCCTL)))
		return OK;
	errors = 0;
	for (;;) {
		stohdr(0L);
#ifdef ALTCANOFF
		Txhdr[ALTCOFF] = ALTCANOFF;
#endif
		if (Zctlesc) {
			Txhdr[ZF0] |= TESCCTL; zshhdr(4, ZSINIT, Txhdr);
		}
		else
			zsbhdr(4, ZSINIT, Txhdr);
		zsdata(Myattn, ZATTNLEN, ZCRCW);
		c = zgethdr(Rxhdr);
		switch (c) {
		case ZCAN:
			return ERROR;
		case ZACK:
			return OK;
		default:
			if (++errors > 19)
				return ERROR;
			continue;
		}
	}
}

/* Send file name and related info */
zsendfile(buf, blen)
char *buf;
{
	register c;
	register unsigned long crc;
	int m, n, i;
	char *p;
	long lastcrcrq = -1;
	long lastcrcof = -1;
	long l;

	for (errors=0; ++errors<11;) {
		Txhdr[ZF0] = Lzconv;	/* file conversion request */
		Txhdr[ZF1] = Lzmanag;	/* file management request */
		if (Lskipnocor)
			Txhdr[ZF1] |= ZMSKNOLOC;
		Txhdr[ZF2] = Lztrans;	/* file transport request */
		Txhdr[ZF3] = 0;
		zsbhdr(4, ZFILE, Txhdr);
		zsdata(buf, blen, ZCRCW);
again:
		c = zgethdr(Rxhdr);
		switch (c) {
		case ZRINIT:
			while ((c = readline(50)) > 0)
				if (c == ZPAD) {
					goto again;
				}
			continue;
		case ZCAN:
		case TIMEOUT:
		case ZABORT:
		case ZFIN:
			sprintf(endmsg, "Got %s on pathname", frametypes[c+FTOFFSET]);
			return ERROR;
		default:
			sprintf(endmsg, "Got %d frame type on pathname", c);
			continue;
		case ERROR:
		case ZNAK:
			continue;
		case ZCRC:
			l = Rxhdr[9] & 0377;
			l = (l<<8) + (Rxhdr[8] & 0377);
			l = (l<<8) + (Rxhdr[7] & 0377);
			l = (l<<8) + (Rxhdr[6] & 0377);
			if (Rxpos != lastcrcrq || l != lastcrcof) {
				lastcrcrq = Rxpos;
				crc = 0xFFFFFFFFL;
				if (Canseek >= 0) {
					fseek(in, bytcnt = l, 0);  i = 0;
					vfile("CRC32 on %ld bytes", Rxpos);
					do {
						/* No rx timeouts! */
						if (--i < 0) {
							i = 32768L/blklen;
							sendline(SYN);
							flushmoc();
						}
						bytcnt += m = n = zfilbuf();
						if (bytcnt > maxbytcnt)
							maxbytcnt = bytcnt;
						for (p = txbuf; --m >= 0; ++p) {
							c = *p & 0377;
							crc = UPDC32(c, crc);
						}
#ifdef DEBUG
						vfile("bytcnt=%ld crc=%08lX",
						  bytcnt, crc);
#endif
					} while (n && bytcnt < lastcrcrq);
					crc = ~crc;
#ifndef MMIO
					clearerr(in);	/* Clear possible EOF */
#endif
				}
			}
			stohdr(crc);
			zsbhdr(4, ZCRC, Txhdr);
			goto again;
		case ZFERR:
		case ZSKIP:
			++Skipcount;
			if (Skipbitch)
				++errcnt;
			fclose(in); return c;
		case ZRPOS:
			/*
			 * Suppress zcrcw request otherwise triggered by
			 * lastyunc==bytcnt
			 */
			if (fseek(in, Rxpos, 0))
				return ERROR;
			Lastsync = (maxbytcnt = bytcnt = Txpos = Lrxpos = Rxpos) -1;
			return zsendfdata();
		}
	}
	fclose(in); return ERROR;
}

/* Send the data in the file */
zsendfdata()
{
	register c, e, n;
	register newcnt;
	register long tcount = 0;
	int junkcount;		/* Counts garbage chars received by TX */
	static int tleft = 6;	/* Counter for test mode */

	junkcount = 0;
	Beenhereb4 = 0;
somemore:
	if (setjmp(intrjmp)) {
waitack:
		junkcount = 0;
		c = getinsync(0);
gotack:
		switch (c) {
		default:
		case ZCAN:
			fclose(in);
			return ERROR;
		case ZRINIT:
			fclose(in);
			return ZSKIP;
		case ZSKIP:
			++Skipcount;
			if (Skipbitch)
				++errcnt;
			fclose(in);
			return c;
		case ZACK:
		case ZRPOS:
			break;
		}
#ifdef READCHECK
		/*
		 * If the reverse channel can be tested for data,
		 *  this logic may be used to detect error packets
		 *  sent by the receiver, in place of setjmp/longjmp
		 *  rdchk(Tty) returns non 0 if a character is available
		 */
		while (rdchk(Tty)) {
#ifdef EATSIT
			switch (checked)
#else
			switch (readline(1))
#endif
			{
			case CAN:
			case ZPAD:
				c = getinsync(1);
				goto gotack;
			case XOFF:		/* Wait a while for an XON */
				readline(100);
			}
		}
#endif
	}

	signal(SIGINT, onintr);
	newcnt = Rxbuflen;
	Txwcnt = 0;
	stohdr(Txpos);
	zsbhdr(4, ZDATA, Txhdr);

	/*
	 * Special testing mode.  This should force receiver to Attn,ZRPOS
	 *  many times.  Each time the signal should be caught, causing the
	 *  file to be started over from the beginning.
	 */
	if (Test) {
		if ( --tleft)
			while (tcount < 20000) {
				printf(qbf); fflush(stdout);
				tcount += strlen(qbf);
#ifdef READCHECK
				while (rdchk(Tty)) {
#ifdef EATSIT
					switch (checked)
#else
					switch (readline(1))
#endif
					{
					case CAN:
					case ZPAD:
						goto waitack;
					case XOFF:	/* Wait for XON */
						readline(100);
					}
				}
#endif
			}
		signal(SIGINT, SIG_IGN); canit();
		sleep(20); purgeline(); mode(0);
		printf("\nsz: Tcount = %ld\n", tcount);
		if (tleft) {
			printf("ERROR: Interrupts Not Caught\n");
			exit(1);
		}
		exit(0);
	}

	do {
		n = zfilbuf();
		if (Eofseen)
			e = ZCRCE;
		else if (junkcount > 3)
			e = ZCRCW;
		else if (bytcnt == Lastsync)
			e = ZCRCW;
		else if (Rxbuflen && (newcnt -= n) <= 0)
			e = ZCRCW;
		else if (Txwindow && (Txwcnt += n) >= Txwspac) {
			Txwcnt = 0;  e = ZCRCQ;
		} else
			e = ZCRCG;
		if (Verbose>1)
			fprintf(stderr, "%7ld ZMODEM%s\n",
			  Txpos, Crc32t?" CRC-32":"");
		zsdata(txbuf, n, e);
		bytcnt = Txpos += n;
		if (bytcnt > maxbytcnt)
			maxbytcnt = bytcnt;
		if (e == ZCRCW)
			goto waitack;
#ifdef READCHECK
		/*
		 * If the reverse channel can be tested for data,
		 *  this logic may be used to detect error packets
		 *  sent by the receiver, in place of setjmp/longjmp
		 *  rdchk(Tty) returns non 0 if a character is available
		 */
		fflush(stdout);
		while (rdchk(Tty)) {
#ifdef EATSIT
			switch (checked)
#else
			switch (readline(1))
#endif
			{
			case CAN:
			case ZPAD:
				c = getinsync(1);
				if (c == ZACK)
					break;
				/* zcrce - dinna wanna starta ping-pong game */
				zsdata(txbuf, 0, ZCRCE);
				goto gotack;
			case XOFF:		/* Wait a while for an XON */
				readline(100);
			default:
				++junkcount;
			}
		}
#endif	/* READCHECK */
		if (Txwindow) {
			while ((tcount = (Txpos - Lrxpos)) >= Txwindow) {
				vfile("%ld window >= %u", tcount, Txwindow);
				if (e != ZCRCQ)
					zsdata(txbuf, 0, e = ZCRCQ);
				c = getinsync(1);
				if (c != ZACK) {
					zsdata(txbuf, 0, ZCRCE);
					goto gotack;
				}
			}
			vfile("window = %ld", tcount);
		}
	} while (!Eofseen);
	signal(SIGINT, SIG_IGN);

	for (;;) {
		stohdr(Txpos);
		zsbhdr(4, ZEOF, Txhdr);
egotack:
		switch (getinsync(0)) {
		case ZACK:
			goto egotack;
		case ZNAK:
			continue;
		case ZRPOS:
			goto somemore;
		case ZRINIT:
			fclose(in);
			return OK;
		case ZSKIP:
			++Skipcount;
			if (Skipbitch)
				++errcnt;
			fclose(in);
			return c;
		default:
			sprintf(endmsg, "Got %d trying to send end of file", c);
		case ERROR:
			fclose(in);
			return ERROR;
		}
	}
}

/*
 * Respond to receiver's complaint, get back in sync with receiver
 */
getinsync(flag)
{
	register c;

	for (;;) {
		if (Test) {
			printf("\r\n\n\n***** Signal Caught *****\r\n");
			Rxpos = 0; c = ZRPOS;
		} else
			c = zgethdr(Rxhdr);
		switch (c) {
		case ZCAN:
		case ZABORT:
		case ZFIN:
		case TIMEOUT:
			sprintf(endmsg, "Got %s sending data", frametypes[c+FTOFFSET]);
			return ERROR;
		case ZRPOS:
			if (Rxpos > bytcnt) {
				vfile("getinsync: Rxpos=%lx bytcnt=%lx Maxbytcnt=%lx",
				  Rxpos, bytcnt, maxbytcnt);
				if (Rxpos > maxbytcnt)
					sprintf(endmsg,
					  "Nonstandard Protocol at %lX", Rxpos);
				return ZRPOS;
			}
			/* ************************************* */
			/*  If sending to a buffered modem, you  */
			/*   might send a break at this point to */
			/*   dump the modem's buffer.		 */
			clearerr(in);	/* In case file EOF seen */
			if (fseek(in, Rxpos, 0)) {
				sprintf(endmsg, "Bad Seek to %ld", Rxpos);
				return ERROR;
			}
			Eofseen = 0;
			bytcnt = Lrxpos = Txpos = Rxpos;
			if (Lastsync == Rxpos) {
				if (++Beenhereb4 > 12) {
					sprintf(endmsg, "Can't send block");
					return ERROR;
				}
				if (Beenhereb4 > 4)
					if (blklen > 32)
						blklen /= 2;
			} else
				Beenhereb4 = 0;
			Lastsync = Rxpos;
			return c;
		case ZACK:
			Lrxpos = Rxpos;
			if (flag || Txpos == Rxpos)
				return ZACK;
			continue;
		case ZRINIT:
			return c;
		case ZSKIP:
			++Skipcount;
			if (Skipbitch)
				++errcnt;
			return c;
		case ERROR:
		default:
			zsbhdr(4, ZNAK, Txhdr);
			continue;
		}
	}
}


/* Send command and related info */
zsendcmd(buf, blen)
char *buf;
{
	register c;
	long cmdnum;

	cmdnum = getpid();
	errors = 0;
	for (;;) {
		stohdr(cmdnum);
		Txhdr[ZF0] = Cmdack1;
		zsbhdr(4, ZCOMMAND, Txhdr);
		zsdata(buf, blen, ZCRCW);
listen:
		Rxtimeout = 100;		/* Ten second wait for resp. */
		Usevhdrs = 0;		/* Allow rx to send fixed len headers */
		c = zgethdr(Rxhdr);

		switch (c) {
		case ZRINIT:
			goto listen;	/* CAF 8-21-87 */
		case ERROR:
		case GCOUNT:
		case TIMEOUT:
			if (++errors > 11)
				return ERROR;
			continue;
		case ZCAN:
		case ZABORT:
		case ZFIN:
		case ZSKIP:
		case ZRPOS:
			return ERROR;
		default:
			if (++errors > 20)
				return ERROR;
			continue;
		case ZCOMPL:
			Exitcode = Rxpos;
			saybibi();
			return OK;
		case ZRQINIT:
			vfile("******** RZ *******");
			system("rz");
			vfile("******** SZ *******");
			goto listen;
		}
	}
}

/*
 * If called as sb use YMODEM protocol
 */
chkinvok(s)
char *s;
{
	register char *p;

	p = s;
	while (*p == '-')
		s = ++p;
	while (*p)
		if (*p++ == '/')
			s = p;
	if (*s == 'v') {
		Verbose=1; ++s;
	}
	Progname = s;
	if (s[0]=='z' && s[1] == 'c') {
		Command = TRUE;
		if (s[8] == 'i')
			Cmdack1 = ZCACK1;
	}
	if (s[0]=='s' && s[1]=='b') {
		Nozmodem = TRUE; blklen=1024;
	}
	if (s[0]=='s' && s[1]=='x') {
		Modem2 = TRUE;
	}
}

countem(argc, argv)
register char **argv;
{
	struct stat f;

	for (Totalleft = 0, Filesleft = 0; --argc >=0; ++argv) {
		f.st_size = -1;
		if (Verbose>2) {
			fprintf(stderr, "\nCountem: %03d %s ", argc, *argv);
			fflush(stderr);
		}
		if (access(*argv, 04) >= 0 && stat(*argv, &f) >= 0) {
			++Filesleft;  Totalleft += f.st_size;
		}
		if (Verbose>2)
			fprintf(stderr, " %ld", (long) f.st_size);
	}
	if (Verbose>2)
		fprintf(stderr, "\ncountem: Total %d %ld\n",
		  Filesleft, Totalleft);
}

chartest(m)
{
	register n;

	mode(m);
	printf("\r\n\nCharacter Transparency Test Mode %d\r\n", m);
	printf("If Pro-YAM/ZCOMM is not displaying ^M hit ALT-V NOW.\r\n");
	printf("Hit Enter.\021");  fflush(stdout);
	readline(500);

	for (n = 0; n < 256; ++n) {
		if (!(n%8))
			printf("\r\n");
		printf("%02x ", n);  fflush(stdout);
		sendline(n);	flushmo();
		printf("  ");  fflush(stdout);
		if (n == 127) {
			printf("Hit Enter.\021");  fflush(stdout);
			readline(500);
			printf("\r\n");  fflush(stdout);
		}
	}
	printf("\021\r\nEnter Characters, echo is in hex.\r\n");
	printf("Hit SPACE or pause 40 seconds for exit.\r\n");

	while (n != TIMEOUT && n != ' ') {
		n = readline(400);
		printf("%02x\r\n", n);
		fflush(stdout);
	}
	printf("\r\nMode %d character transparency test ends.\r\n", m);
	fflush(stdout);
}

/*
 * Set additional control chars to mask in Zsendmask
 * according to bit array stored in char array at p
 */
initzsendmsk(p)
register char *p;
{
	register c;

	for (c = 0; c < 33; ++c) {
		if (p[c>>3] & (1 << (c & 7))) {
			Zsendmask[c] = 1;
			vfile("Escaping %02o", c);
		}
	}
}


/* End of sz.c */
