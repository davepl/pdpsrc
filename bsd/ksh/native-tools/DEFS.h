/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)DEFS.h	1.2 (2.11BSD) 2020/1/7
 */

#ifdef __STDC__
#define __CONC(x,y)		x ## y
#else
#define __CONC(x,y)		x/**/y
#endif

#define ENTRY(x)	.globl __CONC(_,x); \
		__CONC(_,x): \
			PROFCODE(__CONC(_,x));

#define ASENTRY(x)	.globl x; \
		x: \
			PROFCODE(x);

#ifdef PROF
#define	PROFCODE(x)	.data; \
		1:	x+1; \
			.text; \
			.globl	mcount; \
			mov	$1b, r0; \
			jsr	pc,mcount;
#else /* !PROF */
#define	PROFCODE(x)	;
#endif /* PROF */
