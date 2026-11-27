/*
 * Host-test copy of the 2.11BSD libvmf source (public domain, see below),
 * used only by tests/run-host-regressions.py. The native build links the
 * system libvmf. Changes for a modern compiler, none to the algorithm:
 * standard headers and prototypes replace the old declarations, function
 * definitions are in prototype form, swap() takes a direction flag instead
 * of a pointer to read or write, and the list head is a whole object (see
 * seghead below).
 */
/*      Program Name:   vmf.c
 *      Author:  S.M. Schultz
 *
 *      -----------   Modification History   ------------
 *      Version Date            Reason For Modification
 *      1.0     1JAN80          1. Initial release.
 *      2.0     31Mar83         2. Cleanup.
 *	2.1	19Oct87		3. Experiment increasing number of segments.
 *	2.2	03Dec90		4. Merged error.c into this because it had
 *				   been reduced to two write() statements.
 *	3.0	08Sep93		5. Polish it up for use in 'ld.c' (2.11BSD).
 *				   Release into the Public Domain.
 *      --------------------------------------------------              
*/

#include "vmf.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>

/*
 * Choose ONE and only one of the following swap policies
 */
/* #define LRU                  /* Least Recently Used */
/* #define PERC 3               /* Percolation */
#define LRS                     /* Least Recently Swapped */

#ifndef DEBUG
#define debugseg(s,m)           /* do nothing */
#else
static void debugseg();
#endif

/*
 * This is vfm.c, the file of virtual memory management primitives.
 * Call vminit first to get the in memory segments set up.
 * Then call vmopen for each virtual space to be used.
 * Normal transactions against segments are handled via vmmapseg.
 * At wrapup time, call vmflush if any modified segments are
 * assigned to permanent files.
 */

#define NOSEGNO (-1)            /* can never match a segment number */

	/*
	 * The original declares only "static struct dlink seghead[1]" and its
	 * list search reads s_segno and s_vspace of that list head, which is
	 * storage past the end of the array. The head here has those two
	 * members, never matching a real segment, so the same search stays
	 * inside an object.
	 */
	static	struct	{
		struct	dlink	link;
		int	segno;
		struct	vspace	*vspace;
		int	lock_count;	/* nonzero: the head is never a victim */
		} seghead_storage = { { 0, 0 }, -1, 0, 1 };
#define	seghead	(&seghead_storage.link)
	long	nswaps, nmapsegs;      /* statistics */
	static	int	swap(struct vseg *, int);
	static	void	promote(struct vseg *), vmerror(char *);
#define	SWAPIN	0
#define	SWAPOUT	1

/*
 * vminit --- initialize virtual memory system with 'n' in-memory segments
 */

int
vminit(int n)
	{
	register struct vseg *s;
        static struct vseg *segs;

	segs = (struct vseg *)calloc(n, sizeof (struct vseg));
	if	(!segs)
		{
		errno = ENOMEM;
		return(-1);
		}
        seghead[0].fwd = seghead[0].back = seghead; /* selfpoint */

	for     (s = segs; s < &segs[n] ; s++)
		{
		s->s_link.fwd = seghead;
		s->s_link.back = seghead[0].back;
		s->s_link.back->fwd = s->s_link.fwd->back = (struct dlink *)s;
		s->s_segno = NOSEGNO;
		s->s_vspace = NULL;
		s->s_lock_count = 0;            /* vmunlocked */
		s->s_flags = 0;                 /* not DIRTY */
		}
	return(0);
	}

/*
 * vmmapseg --- convert segment number to real memory address
 */

struct vseg *
vmmapseg(struct vspace *vspace, unsigned segno)
	{
	register struct vseg *s;

	nmapsegs++;

	if	(segno >= vspace->v_maxsegno || segno < 0)
		{
#ifdef DEBUG
		fprintf(stderr,"vmmapseg vspace0%o segno%d\n", vspace, segno);
#endif
		vmerror("vmmapseg: bad segno");
		}

	/* look for segment in memory */
	for	(s = (struct vseg *)seghead[0].fwd;
		 s->s_segno != segno || s->s_vspace != vspace;
	    	 s = (struct vseg *)s->s_link.fwd)
		{
		if	(s == (struct vseg *)seghead)
			{     /* not in memory */
			int status;

			for (s = (struct vseg *)s->s_link.back; s->s_lock_count != 0; 
					s = (struct vseg *)s->s_link.back)
				{
				if (s == (struct vseg *)seghead)
					vmerror("Too many locked segs!");
				debugseg(s, "back skip");
				}
			debugseg(s, "dump on");
			if	(s->s_flags & S_DIRTY)
				if	(swap(s, SWAPOUT) != 0)
					{
					fprintf(stderr,
						"write swap, v=%p fd=%d\n",
						(void *)s->s_vspace,s->s_vspace->v_fd);
					exit(-2);
					}
			s->s_vspace = vspace;
			s->s_segno = segno;
			s->s_flags &= ~S_DIRTY;
			status = swap(s, SWAPIN);
			if	(status == -2)
				{
				fprintf(stderr, "can't read swap file");
				exit(-2);
				}
			else if (status == -1)
				(void)vmclrseg(s);
#ifdef LRS                              /* Least Recently Swapped */
			promote(s);
#endif
			break;
			}
		debugseg(s, "forward skip");
		}
#ifdef PERC
	{       /* percolate just-referenced segment up list */
	register struct dlink *neighbor, *target;
	int count;

	s->fwd->back = s->back;         /* delete */
	s->back->fwd = s->fwd;

	count = PERC;                   /* upward mobility */
	for	(target = s; target != seghead && count-- > 0; )
		target = target->back;
	neighbor = target->fwd;
	s->back = target;               /* reinsert */
	s->fwd = neighbor;
	target->fwd = neighbor->back = s;
	}
#endif
#ifdef LRU                              /* Least Recently Used */
	promote(s);
#endif
	debugseg(s, "vmmapseg returns");
	return(s);
	}

/*
 * swap --- swap a segment in or out
 *      (called only from this file)
 */

static int
swap(struct vseg *seg, int direction)  /* used only from this file */
	{
	off_t file_address;
	register struct vspace *v;

	v = seg->s_vspace;
	nswaps++;
	file_address = seg->s_segno;
	file_address *= (BYTESPERSEG);
	file_address += v->v_foffset;
#ifdef SWAPTRACE
	printf("fd%d blk%d\tswap %s\n", v->v_fd, file_address,
		iofunc == read ? "in" : "out");
#endif
	if	(lseek(v->v_fd, file_address, SEEK_SET) == -1L)
		return(-2);

	switch	(direction == SWAPIN ?
		    read(v->v_fd, seg->s_cinfo, BYTESPERSEG) :
		    write(v->v_fd, seg->s_cinfo, BYTESPERSEG))
		{
		case BYTESPERSEG:
			return(0);
		case 0:
			return(-1);
		default:
			return(-2);
		}
	}

void
vmclrseg(struct vseg *seg)
	{

	(void)bzero(seg->s_cinfo, BYTESPERSEG);
	vmmodify(seg);
	}

/*
 * vmlock --- vmlock a segment into real memory
 */

void
vmlock(struct vseg *seg)
	{

	seg->s_lock_count++;
	if	(seg->s_lock_count < 0)
		vmerror("vmlock: overflow");
	}

/*
 * vmunlock --- unlock a segment
 */

void
vmunlock(struct vseg *seg)
	{

        --seg->s_lock_count;
	if	(seg->s_lock_count < 0)
		vmerror("vmlock: underflow");
	}

/*
 * vmmodify --- declare a segment to have been modified
 */

void
vmmodify(struct vseg *seg)
	{

	VMMODIFY(seg);
	debugseg(seg, "vmmodify");
	}

/*
 * vmflush --- flush out virtual space buffers
 */

void
vmflush(void)
	{
	register struct vseg *s;

	for	(s = (struct vseg *)seghead[0].fwd; s != (struct vseg *)seghead;
		 s = (struct vseg *)s->s_link.fwd)
		if	(s->s_flags & S_DIRTY)
			swap(s, SWAPOUT);
	}

/*
 * debugseg --- output debugging information about a seg in mem
 */
#ifdef DEBUG
static void
debugseg(s, msg)
	char 	*msg;
	register struct	vseg *s;
	{
	fprintf(stderr, "seg%o vspace%o segno%d flags%o vmlock%d %s\r\n",
		s, s->s_vspace, s->s_segno, s->s_flags, s->s_lock_count, msg);
	}
#endif

/*
 * vmopen --- open a virtual space associated with a file
 */

int
vmopen(struct vspace *vs, char *filename)
	{
	register int	fd;
	char	junk[32];

	if	(!filename)
		{
		strcpy(junk, "/tmp/vmXXXXXX");
		fd = mkstemp(junk);
		unlink(junk);
		}
	else
		fd = open(filename, O_RDWR|O_CREAT, 0664);

	if	(fd != -1)
		{
		vs->v_fd = fd;
		vs->v_foffset = 0;
		vs->v_maxsegno = MAXSEGNO;
		}
	return(fd);
	}

/*
 * vmclose --- closes a virtual space associated with a file
 * invalidates all segments associated with that file
 */

void
vmclose(struct vspace *vs)
	{
	register struct vseg *s;

	vmflush();
	/* invalidate all segments associated with that file */
	for	(s = (struct vseg *)seghead[0].fwd; s != (struct vseg *)seghead;
		 s = (struct vseg *)s->s_link.fwd) 
		{
		if	(s->s_vspace == vs) 
			{
			s->s_segno = NOSEGNO;
			s->s_vspace = NULL;
			s->s_lock_count = 0;            /* vmunlocked */
			s->s_flags &= ~S_DIRTY;
			}
		}
	close(vs->v_fd);
	}

/*
 * promote --- put a segment at the top of the list
 */

static void
promote(struct vseg *s)
	{

        s->s_link.fwd->back = s->s_link.back;         /* delete */
	s->s_link.back->fwd = s->s_link.fwd;

	s->s_link.fwd = seghead[0].fwd;   /* insert at top of totem pole */
	s->s_link.back = seghead;
	seghead[0].fwd = s->s_link.fwd->back = (struct dlink *)s;
	}

/*
 * vmerror --- print error message and commit suicide
 *      Message should always make clear where called from.
 *	Example:	vmerror("In floogle: can't happen!");
 */

static void
vmerror(char *msg)
	{
	fprintf(stderr, "%s\n", msg);
	abort();	/* terminate process with core dump */
	}
