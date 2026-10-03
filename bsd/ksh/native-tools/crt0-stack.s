/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifdef LIBC_SCCS
	<@(#)crt0.s	2.4 (2.11BSD GTE) 2/02/95\0>
	.even
#endif LIBC_SCCS

/*
 * C runtime startoff.  When an a.out is loaded by the kernel, the kernel
 * sets up the stack as follows:
 *
 *	_________________________________
 *	| (NULL)			| 0177776: top of memory
 *	|-------------------------------|
 *	| 				|
 *	| environment strings		|
 *	| 				|
 *	|-------------------------------|
 *	| 				|
 *	| argument strings		|
 *	| 				|
 *	|-------------------------------|
 *	| envv[envc] (NULL)		| end of environment vector tag, a 0
 *	|-------------------------------|
 *	| envv[envc-1]			| pointer to last environment string
 *	|-------------------------------|
 *	| ...				|
 *	|-------------------------------|
 *	| envv[0]			| pointer to first environment string
 *	|-------------------------------|
 *	| argv[argc] (NULL)		| end of argument vector tag, a 0
 *	|-------------------------------|
 *	| argv[argc-1]			| pointer to last argument string
 *	|-------------------------------|
 *	| ...				|
 *	|-------------------------------|
 *	| argv[0]			| pointer to first argument string
 *	|-------------------------------|
 * sp->	| argc				| number of arguments
 *	---------------------------------
 *
 * Crt0 simply moves the argc down two places in the stack, calculates the
 * the addresses of argv[0] and envv[0], putting those values into the two
 * spaces opened up to set the stack up as main expects to see it.
 */


.bss
.globl	_environ		/ copy of envv vector pointer for getenv and
_environ: .=.+2			/   others
.text

/*
 * Paragraph below retained for historical purposes.
 *
 * The following zero has a number of purposes - it serves as a null terminated
 * string for uninitialized string pointers on separate I&D machines for
 * instance.  But we never would have put it here for that reason; programs
 * which use uninitialized pointer *should* die.  The real reason it's here is
 * so you can declare "char blah[] = "foobar" at the start of a C program
 * and not have printf generate "(null)" when you try to print it because
 * blah is at address zero on separate I&D machines ...  sick, sick, sick ...
 *
 * In porting bits and pieces of the 4.4-Lite C library the global program
 * name location '___progname' was needed.  Rather than take up another two
 * bytes of D space the 0th location was used.   The '(null)' string was
 * removed from doprnt.s so now when programs use uninitialized pointers
 * they will be rewarded with argv[0].  This is no sicker than before and
 * may cause bad programs to die sooner.
*/
	.data
	.globl	___progname, _strrchr

___progname: 0
	.text

.globl	_exit, _main


.globl	start
start:
	/ Prefault startup stack with a restartable memory access.
	mov	sp,r0
	sub	$4096.,sp
	tst	(sp)
	mov	r0,sp
	setd			/ set double precision
	sub	$4,sp		/ make room for argv and env pointers
	mov	sp,r0
	mov	4(sp),(r0)+	/ copy argc down
	mov	sp,(r0)		/ calculate position of arg pointers
	add	$6,(r0)
	mov	*(r0),___progname
	mov	(r0)+,(r0)	/ calculate position of env pointers
	add	(sp),(r0)
	add	(sp),(r0)
	add	$2,(r0)
	mov	(r0),_environ	/ environ = &envv[0]

#ifdef NONFP
.globl	setfpt			/ if non floating point version of the
				/   library, set up floating point simulator
	jsr	pc,setfpt	/   trap - see ../fpsim/README for uglyness
#endif

#ifdef MCRT0
.globl	_etext, _monstartup

	mov	$_etext,-(sp)	/ monstartup(2, &_etext)
	mov	$eprol,-(sp)
	jsr	pc,_monstartup
	cmp	(sp)+,(sp)+
#endif MCRT0

	clr	r5		/ for adb and longjmp/rollback ...
	mov	$'/,-(sp)
	mov	___progname,-(sp)
	jsr	pc,_strrchr
	tst	r0
	beq	1f
	inc	r0
	mov	r0,___progname
1:
	cmp	(sp)+,(sp)+
	jsr	pc,_main	/ call main
	mov	r0,(sp)		/   and pass main's return value to _exit ...
	jsr	pc,*$_exit


#ifdef MCRT0
.globl	_monitor, __cleanup, __exit

_exit:				/ catch call to exit
	clr	-(sp)		/ monitor(0) - write profiling information out
	jsr	pc,_monitor
	jsr	pc,__cleanup	/ the real exit calls _cleanup so we do too
	cmp	(sp)+,(sp)+	/ toss monitor's parameter and our return
	jsr	pc,__exit	/   address leaving the exit code and exit
eprol:				/ start of default histogram recording

#else !MCRT0

/*
 * null mcount and moncontrol,
 * just in case some routine is compiled for profiling
 */
.globl	mcount, _moncontrol
mcount:
_moncontrol:
	rts	pc		/ do nothing

#endif MCRT0
