/*
 * C code generator header
 */

#include <stdio.h>
#include <setjmp.h>

#define	LTYPE	long	/* change to int for no long consts */
#define	NULL	0
#define	TNULL	(union tree *)NULL
#define	UNS(x)	((unsigned short)(x))

/*
 *  Tree node for unary and binary
 */
struct	tnode {
	int	op;
	int	type;
	int	degree;
	union	tree *tr1;
	union	tree *tr2;
};

/*
 * tree names for locals
 */
struct	tname {
	int	op;
	int	type;
	char	class;
	char	regno;
	int	offset;
	int	nloc;
};

/*
 * tree names for externals
 */
struct	xtname {
	int	op;
	int	type;
	char	class;
	char	regno;
	int	offset;
	char	*name;
};

/*
 * short constants
 */
struct	tconst {
	int	op;
	int	type;
	int	value;
};

/*
 * long constants
 */
struct	lconst {
	int	op;
	int	type;
	LTYPE	lvalue;
};

/*
 * Floating constants
 */
struct	ftconst {
	int	op;
	int	type;
	int	value;
	double	fvalue;
};

/*
 * Node used for field assignments
 */
struct	fasgn {
	int	op;
	int	type;
	int	degree;
	union	tree *tr1;
	union	tree *tr2;
	int	mask;
};

union	tree {
	struct	tnode t;
	struct tname n;
	struct	xtname x;
	struct	tconst c;
	struct	lconst l;
	struct	ftconst f;
	struct	fasgn F;
};

struct	optab {
	char	tabdeg1;
	char	tabtyp1;
	char	tabdeg2;
	char	tabtyp2;
	char	*tabstring;
};

struct	table {
	int	tabop;
	struct	optab *tabp;
};

struct	instab {
	int	iop;
	char	*str1;
	char	*str2;
};

struct	swtab {
	int	swlab;
	int	swval;
};

char	maprel[];
char	notrel[];
int	nreg;
int	isn;
int	line;
int	nerror;			/* number of hard errors */
struct	table	cctab[];
struct	table	efftab[];
struct	table	regtab[];
struct	table	sptab[];
struct	table	lsptab[1];
struct	instab	instab[];
struct	instab	branchtab[];
int	opdope[];
char	*opntab[];
int	nstack;
int	nfloat;
struct	tname	sfuncr;
char	*funcbase;
char	*curbase;
char	*coremax;
struct	tconst czero, cone;
long	totspace;
int	regpanic;		/* set when SU register alg. fails */
int	panicposs;		/* set when there might be need for regpanic */
jmp_buf	jmpbuf;
long	ftell();
char	*sbrk();
struct	optab *match();
union	tree *optim();
union	tree *unoptim();
union	tree *pow2();
union	tree *tnode();
union	tree *sdelay();
union	tree *ncopy();
union	tree *getblk();
union	tree *strfunc();
union	tree *isconstant();
union	tree *tconst();
union	tree *hardlongs();
union	tree *lconst();
union	tree *acommute();
union	tree *lvfield();
union	tree *paint();
long	ftell();

/*
 * Some special stuff for long comparisons
 */
int	xlab1, xlab2, xop, xzero;

/*
	operators
*/
#define	EOFC	0
#define	SEMI	1
#define	LBRACE	2
#define	RBRACE	3
#define	LBRACK	4
#define	RBRACK	5
#define	LPARN	6
#define	RPARN	7
#define	COLON	8
#define	COMMA	9
#define	FSEL	10
#define	FSELR	11
#define	FSELT	12
#define	FSELA	16
#define	ULSH	17
#define	ASULSH	18

#define	KEYW	19
#define	NAME	20
#define	CON	21
#define	STRING	22
#define	FCON	23
#define	SFCON	24
#define	LCON	25
#define	SLCON	26

#define	AUTOI	27
#define	AUTOD	28
#define	NULLOP	218
#define	INCBEF	30
#define	DECBEF	31
#define	INCAFT	32
#define	DECAFT	33
#define	EXCLA	34
#define	AMPER	35
#define	STAR	36
#define	NEG	37
#define	COMPL	38

#define	DOT	39
#define	PLUS	40
#define	MINUS	41
#define	TIMES	42
#define	DIVIDE	43
#define	MOD	44
#define	RSHIFT	45
#define	LSHIFT	46
#define	AND	47
#define	ANDN	55
#define	OR	48
#define	EXOR	49
#define	ARROW	50
#define	ITOF	51
#define	FTOI	52
#define	LOGAND	53
#define	LOGOR	54
#define	FTOL	56
#define	LTOF	57
#define	ITOL	58
#define	LTOI	59
#define	ITOP	13
#define	PTOI	14
#define	LTOP	15

#define	EQUAL	60
#define	NEQUAL	61
#define	LESSEQ	62
#define	LESS	63
#define	GREATEQ	64
#define	GREAT	65
#define	LESSEQP	66
#define	LESSP	67
#define	GREATQP	68
#define	GREATP	69

#define	ASPLUS	70
#define	ASMINUS	71
#define	ASTIMES	72
#define	ASDIV	73
#define	ASMOD	74
#define	ASRSH	75
#define	ASLSH	76
#define	ASAND	77
#define	ASOR	78
#define	ASXOR	79
#define	ASSIGN	80
#define	TAND	81
#define	LTIMES	82
#define	LDIV	83
#define	LMOD	84
#define	ASANDN	85
#define	LASTIMES 86
#define	LASDIV	87
#define	LASMOD	88

#define	QUEST	90
/* #define	MAX	93	/* not used; wanted macros in param.h */
#define	MAXP	94
/* #define	MIN	95	/* not used; wanted macros in param.h */
#define	MINP	96
#define	LLSHIFT	91
#define	ASLSHL	92
#define	SEQNC	97
#define	CALL1	98
#define	CALL2	99
#define	CALL	100
#define	MCALL	101
#define	JUMP	102
#define	CBRANCH	103
#define	INIT	104
#define	SETREG	105
#define	LOAD	106
#define	PTOI1	107
#define	ITOC	109
#define	RFORCE	110

/*
 * Intermediate code operators
 */
#define	BRANCH	111
#define	LABEL	112
#define	NLABEL	113
#define	RLABEL	114
#define	STRASG	115
#define	STRSET	116
#define	UDIV	117
#define	UMOD	118
#define	ASUDIV	119
#define	ASUMOD	120
#define	ULTIMES	121	/* present for symmetry */
#define	ULDIV	122
#define	ULMOD	123
#define	ULASTIMES 124	/* present for symmetry */
#define	ULASDIV	125
#define	ULASMOD	126
#define	ULTOF	127
#define	ULLSHIFT 128	/* << for unsigned long */
#define	UASLSHL	129	/* <<= for unsigned long */

#define	BDATA	200
#define	PROG	202
#define	DATA	203
#define	BSS	204
#define	CSPACE	205
#define	SSPACE	206
#define	SYMDEF	207
#define	SAVE	208
#define	RETRN	209
#define	EVEN	210
#define	PROFIL	212
#define	SWIT	213
#define	EXPR	214
#define	SNAME	215
#define	RNAME	216
#define	ANAME	217
#define	SETSTK	219
#define	SINIT	220
#define	GLOBAL	221
#define	C3BRANCH	222
#define	ASSEM	223

/*
 *	types
 */
#define	INT	0
#define	CHAR	1
#define	FLOAT	2
#define	DOUBLE	3
#define	STRUCT	4
#define	RSTRUCT	5
#define	LONG	6
#define	UNSIGN	7
#define	UNCHAR	8
#define	UNLONG	9
#define	VOID	10

#define	TYLEN	2
#define	TYPE	017
#define	XTYPE	(03<<4)
#define	PTR	020
#define	FUNC	040
#define	ARRAY	060

/*
	storage	classes
*/
#define	KEYWC	1
#define	MOS	10
#define	AUTO	11
#define	EXTERN	12
#define	STATIC	13
#define	REG	14
#define	STRTAG	15
#define	ARG	16
#define	OFFS	20
#define	XOFFS	21
#define	SOFFS	22

/*
	Flag	bits
*/

#define	BINARY	01
#define	LVALUE	02
#define	RELAT	04
#define	ASSGOP	010
#define	LWORD	020
#define	RWORD	040
#define	COMMUTE	0100
#define	RASSOC	0200
#define	LEAF	0400
#define	CNVRT	01000
