/*% cc -xenix -M0 -compat -Osa -K -i % -o undos
 *
 * Undos - change DOS format files to Unix, etc.
 */
char ID[] =
  "Undos Rev 03-06-94 Copyright Omen Technology Inc All Rights Reserved\n";

/*
 **************************************************************************
 *
 * undos.c By Chuck Forsberg,  Omen Technology INC
 *    Copyright 1997 Omen Technology Inc All Rights Reserved
 * 
 *********************************************************************
 *********************************************************************
 * 
 *
 * This software may be freely used for educational (didactic
 * only) purposes.  "Didactic" means it is used as a study item
 * in a course teaching the workings of computer protocols.
 * 
 * This software may also be freely used to support file transfer
 * operations to or from duly licensed Omen Technology products.
 * This includes DSZ, GSZ, ZCOMM, Professional-YAM and PowerCom.
 * Use with other commercial or shareware programs
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
 */



#include <stdio.h>
#include "../pdp11_unistd.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#if !defined(pdp11) && !defined(__pdp11__)
#include <utime.h>
#endif

#define LL 10240
#define SUB 032


#if defined(pdp11) || defined(__pdp11__)
/* 2.11BSD provides utime(2), but no <utime.h>. */
struct    utimbuf	   {
	time_t	   actime;	/* access time */
	time_t	   modtime;	/* modification time	*/
};
extern int utime();
#endif

void usage(), xperror(), chngfmt();

char Lbuf[LL+2];
char *Progname;
int Todos = 0;
int Tocpm = 0;
int Tomac = 0;
int Unmac = 0;
int Strip = 0;
int Stripsp = 0;
int Graphics = 0;
int Unparity = 0;
int Munged = 0;
int Lineflush = 0;	/* Flush output at end of each line */

main(argc, argv)
char **argv;
{
	Progname = *argv;
	if (! strcmp(Progname, "tocpm"))
		Todos = Tocpm = 1;
	if (! strcmp(Progname, "todos"))
		Todos = 1;
	if (! strcmp(Progname, "unmac"))
		Unmac = 1;
	if (! strcmp(Progname, "tomac"))
		Tomac = 1;
	if (! strcmp(Progname, "unparity"))
		Unparity = 1;

	if (! strcmp(argv[1], "-p")) {
		++Strip;  ++Stripsp; --argc; ++argv;
	}
	if (! strcmp(argv[1], "-s")) {
		++Strip; --argc; ++argv;
	}
	if (! strcmp(argv[1], "-g")) {
		Strip = 0;  ++Graphics; --argc; ++argv;
	}


	if (argc == 1) {
		chngfmt(NULL);  exit(0);
	}

	if (argc<2 || *argv[1]== '-')
		usage();
	while (--argc >= 1)
		chngfmt(*++argv);
	exit(Munged);
}

void
usage()
{
	fprintf(stderr, ID);
	fprintf(stderr, "\nUsage: {undos|tounix|todos|tocpm|unmac|tomac} [-p] [-s] [file ...]\n");
	fprintf(stderr, "	-p Strip trailing spaces, parity bit, ignore bytes < 007\n");
	fprintf(stderr, "	-s Strip parity bit, ignore bytes < 007\n");
	fprintf(stderr, "	-g Allow Graphics (line drawing) characters\n");
	fprintf(stderr, "-or-	unparity [file ...]\n");
	exit(1);
}

void
chngfmt(name)
char *name;
{
	register c;
	register char *p;
	register n;
	register FILE *fin;
	FILE *fout;
	int linno = 0;
	long fpos;
	struct stat st, ost;
	struct utimbuf times;
	char outnam[64];
	int nlong = LL;

	if (name) {
		if (stat(name, &st)) {
			xperror(name); return;
		}
		if ((st.st_mode & S_IFMT) != S_IFREG) {
			fprintf(stderr, "%s: %s is not a regular file\n", Progname, name);
			return;
		}
		if ((fin = fopen(name, "r")) == NULL) {
			xperror(name); return;
		}
		strcpy(outnam, "undosXXXXXX");
		mktemp(outnam);
		if ((fout = fopen(outnam, "w")) == NULL) {
			xperror(outnam); exit(2);
		}
	} else {
		fin = stdin; fout = stdout;
	}
	if (fstat(fileno(fout), &ost)) {
		xperror("Can't fstat output!"); return;
	}
	if ((ost.st_mode & S_IFMT) != S_IFREG) {
		Lineflush = 1;
	}

	if (Unparity) {
		while ((c = getc(fin)) != EOF)
			if (putc((c & 0177), fout) == EOF) {
				xperror(outnam); exit(2);
			}
		goto closeit;
	}
	for (;;) {
		++linno;
		Lbuf[0] = 0;
		for (p=Lbuf+1, n=LL; --n>0; ) {
ignore:
			if ((c = getc(fin)) == EOF)
				break;
			if ( !c)
				goto ignore;
			if (c & 0200 && !Graphics) {
				if (Strip) {
					if ((c &= 0177) < 7)
						goto ignore;
				} else if (name)
					goto thisbin; 
			}
			if (c < '\7') {
				if (Strip) {
					if ((c &= 0177) < 7)
						goto ignore;
				} else if (name)
					goto thisbin; 
			}
			if (c == SUB) {
				if (linno == 1 && name)	/* ARC or ZOO file */
					goto thisbin;
				break;
			}
			if (c == '\r' && Unmac)
				c = '\n';
			*p++ = c;
			if (c == '\n')
				break;
		}
		*p = '\0';
		if (n < nlong)
			nlong = n;

		if (n == 0 && name) {
thisbin:
			if (n) {
				fprintf(stderr, "%s: %s is a binary file", Progname, name);
				fprintf(stderr, " line=%d char =%2X\n", linno, c);
			} else {
				fprintf(stderr, "line=%d char =%2X\n", linno, c);
				fprintf(stderr, "%s: %s has long line!\n", Progname, name);
				if (!Unmac)
					fprintf(stderr, "Try unmac?\n");
			}
			Munged = 1;  fclose(fin);  fclose(fout);
			unlink(outnam);  return;
		}

		if (Todos) {
			if (*--p == '\n' && p[-1] != '\r') {
				*p++ = '\r'; *p++ = '\n'; *p = 0;
			}
		} else if (Tomac) {
			if (*--p == '\n') {
				if (p[-1] == '\r')
					--p;
				*p++ = '\r'; *p = 0;
			}
		} else {
			if (*--p == '\n' && *--p == '\r') {
				while (p>(Lbuf+1) && p[-1] == '\r')
					--p;
				if (Stripsp)
					while (p>(Lbuf+1) && p[-1] == ' ')
						--p;
				*p++ = '\n'; *p = 0;
			}
		}
		if (Lbuf[1] && fputs(Lbuf+1, fout) == EOF) {
			xperror(outnam); exit(2);
		}
		switch (c) {
		case EOF:
			if (ferror(fin)) {
				xperror(name); exit(3);
			}
		case SUB:
			if (Tocpm) {
				fpos = ftell(fout);
				do {
					putc(SUB, fout);
				} while (++fpos & 127);
			}
closeit:
			if ( !name)
				return;
			fclose(fout); fclose(fin);
			if (st.st_nlink > 1) 
				sprintf(Lbuf, "trap '' 1 2 3 15; cp %s %s", outnam, name);
			else
				sprintf(Lbuf, "mv %s %s", outnam, name);
			system(Lbuf);
			times.actime = st.st_atime;
			times.modtime = st.st_mtime;
			if (utime(name, &times)) {
				xperror("Can't set file date");
			}
			if (st.st_nlink > 1) 
				unlink(outnam);
			nlong = LL - nlong;
			if (nlong > 132)
				fprintf(stderr, "Longest line in %s has %d bytes.\n",
				  name ? name:"stdin", nlong);
			return;
		}
		if (Lineflush)
			fflush(fout);
	}
}

void
xperror(s)
char *s;
{
	int saved_errno;

	saved_errno = errno;
	fprintf(stderr, "%s: ", Progname);
	errno = saved_errno;
	perror(s);
}
