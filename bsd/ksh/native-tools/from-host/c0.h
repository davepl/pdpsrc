/*
 * 	C compiler-- first pass header
 */

#if	!defined(lint) && defined(DOSCCS)
static	char	sccsid[] = "@(#)c0.h	2.0 (2.11BSD) 2020/1/7";
#endif
#include <stdio.h>

/*
 * This parameter is the _only_ one which affects the recognized length
 * of symbols.  Symbol names are dynamically allocated and null terminated
 * now, the define below is the 'cutoff' or maximum length to permit.
 *
 * NOTE: there are _exactly_ 4 references to this in all of c0.  There are
 * _NO_ references to it in c1.  Just make sure that the value is less than
 * 79 and c1 will be oblivious to the length of a symbol name.
 *
 * NOTE: The optimizer (c2) needs to be updated if the size of a symbol
 * changes.  See the file c2.h
*/

#define	MAXCPS	32	/* # chars per symbol */

#define	LTYPE	long	/* change to int if no long consts */
#define	MAXINT	077777	/* Largest positive short integer */
#define	MAXUINT	0177777	/* largest unsigned integer */
#define	HSHSIZ	300	/* # entries in hash table for names */
#define	CMSIZ	40	/* size of expression stack */
#define	SSIZE	40	/* size of other expression stack */
#define	SWSIZ	300	/* size of switch table */
#define	NMEMS	128	/* Number of members in a structure */
#define	NBPW	16	/* bits per word, object machine */
#define	NBPC	8	/* bits per character, object machine */
#define	NCPW	2	/* chars per word, object machine */
#define	LNCPW	2	/* chars per word, compiler's machine */
#define	LNBPW	16	/* bits per word, compiler's machine */
/* dlf change
#define	STAUTO	(-6)	 offset of first auto variable */
int	STAUTO;
#define	STARG	4	/* offset of first argument */
#define	DCLSLOP	512	/* Amount trees lie above declaration stuff */


/*
 * # bytes in primitive types
 */
#define	SZCHAR	1
#define	SZINT	2
#define	SZPTR	2
#define	SZFLOAT	4
#define	SZLONG	4
#define	SZDOUB	8

/*
 * Structure of namelist
 */
struct nmlist {
	char	hclass;		/* storage class */
	char	hflag;		/* various flags */
	int	htype;		/* type */
	int	*hsubsp;	/* subscript list */
	union	str *hstrp;	/* structure description */
	int	hoffset;	/* post-allocation location */
	struct	nmlist *nextnm;	/* next name in chain */
	union	str *sparent;	/* Structure of which this is member */
	char	hblklev;	/* Block level of definition */
	char	*name;		/* ASCII name */
};

/*
 * format of a structure description
 *  Same gadget is also used for fields,
 *  which can't be structures also.
 * Finally, it is used for parameter collection.
 */
union str {
	struct SS {
		int	ssize;			/* structure size */
		struct nmlist **memlist;	/* member list */
	} S;
	struct FS {
		int	flen;			/* field width in bits */
		int	bitoffs;		/* shift count */
	} F;
	struct nmlist P;
};

/*
 * Structure of tree nodes for operators
 */
struct tnode {
	int	op;		/* operator */
	int	type;		/* data type */
	int	*subsp;		/* subscript list (for arrays) */
	union	str *strp;	/* structure description for structs */
	union	tree *tr1;	/* left operand */
	union	tree *tr2;	/* right operand */
};

/*
 * Tree node for constants
 */
struct	cnode {
	int	op;
	int	type;
	int	*subsp;
	union	str *strp;
	int	value;
};

/*
 * Tree node for long constants
 */
struct lnode {
	int	op;
	int	type;
	int	*subsp;
	union	str *strp;
	long	lvalue;
};

/*
 * tree node for floating
 * constants
 */
struct	fnode {
	int	op;
	int	type;
	int	*subsp;
	union	str *strp;
	char	*cstr;
};

/*
 * All possibilities for tree nodes
 */
union tree {
	struct	tnode t;
	struct	cnode c;
	struct	lnode l;
	struct	fnode f;
	struct	nmlist n;
	struct	FS fld;
};


/*
 * Place used to keep dimensions
 * during declarations
 */
struct	tdim {
	int	rank;
	int	dimens[5];
};

/*
 * Table for recording switches.
 */
struct swtab {
	int	swlab;
	int	swval;
};

#define	TNULL	(union tree *)NULL

char	cvtab[4][4];
char	filename[64];
int	opdope[];
char	ctab[];
char	symbuf[MAXCPS+2];
struct	nmlist	*hshtab[HSHSIZ];
int	kwhash[(HSHSIZ+LNBPW-1)/LNBPW];
union	tree **cp;
int	isn;
struct	swtab	swtab[SWSIZ];
int	unscflg;
struct	swtab	*swp;
int	contlab;
int	brklab;
int	retlab;
int	deflab;
unsigned autolen;		/* make these int if necessary */
unsigned maxauto;		/* ... will only cause trouble rarely */
int	peeksym;
int	peekc;
int	eof;
int	line;
char	*locbase;
char	*treebase;
char	*treebot;
char	*coremax;
struct	nmlist	*defsym;
struct	nmlist	*funcsym;
int	proflg;
struct	nmlist	*csym;
int	cval;
LTYPE	lcval;
int	nchstr;
int	nerror;
struct	nmlist *paraml;
struct	nmlist *parame;
int	strflg;
int	mosflg;
int	initflg;
char	sbuf[BUFSIZ];
FILE	*sbufp;
int	regvar;
int	bitoffs;
struct	tnode	funcblk;
char	cvntab[];
char	numbuf[64];
struct	nmlist **memlist;
union	str *sparent;
int	nmems;
struct	nmlist	structhole;
int	blklev;
int	mossym;

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
#define	CAST	11
#define	ETYPE	12

#define	KEYW	19
#define	NAME	20
#define	CON	21
#define	STRING	22
#define	FCON	23
#define	SFCON	24
#define	LCON	25
#define	SLCON	26
#define	NULLOP	29
#define	XNULLOP	218	/* interface version */

#define	SIZEOF	91
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
#define	ASSAND	77
#define	ASOR	78
#define	ASXOR	79
#define	ASSIGN	80

#define	QUEST	90
#define	MAX	93
#define	MAXP	94
#define	MIN	95
#define	MINP	96
#define	SEQNC	97
#define	CALL	100
#define	MCALL	101
#define	JUMP	102
#define	CBRANCH	103
#define	INIT	104
#define	SETREG	105
#define	RFORCE	110
#define	BRANCH	111
#define	LABEL	112
#define	NLABEL	113
#define	RLABEL	114
#define	STRASG	115
#define	ITOC	109
#define	SEOF	200	/* stack EOF marker in expr compilation */

/*
  types
*/
#define	INT	0
#define	CHAR	1
#define	FLOAT	2
#define	DOUBLE	3
#define	STRUCT	4
#define	LONG	6
#define	UNSIGN	7
#define	UNCHAR	8
#define	UNLONG	9
#define	VOID	10
#define	UNION	8		/* adjusted later to struct */

#define	ALIGN	01
#define	TYPE	017
#define	BIGTYPE	060000
#define	TYLEN	2
#define	XTYPE	(03<<4)
#define	PTR	020
#define	FUNC	040
#define	ARRAY	060

/*
  storage classes
*/
#define	KEYWC	1
#define	TYPEDEF	9
#define	MOS	10
#define	AUTO	11
#define	EXTERN	12
#define	STATIC	13
#define	REG	14
#define	STRTAG	15
#define ARG	16
#define	ARG1	17
#define	AREG	18
#define	DEFXTRN	20
#define	MOU	21
#define	ENUMTAG	22
#define	ENUMCON	24

#define	SIGNED	5
#define	CONST	16
#define	VOLATIL	17

/*
  keywords
*/
#define	GOTO	20
#define	RETURN	21
#define	IF	22
#define	WHILE	23
#define	ELSE	24
#define	SWITCH	25
#define	CASE	26
#define	BREAK	27
#define	CONTIN	28
#define	DO	29
#define	DEFAULT	30
#define	FOR	31
#define	ENUM	32
#define	ASM	33

/*
  characters
*/
#define	ELLIPS	116
#define	BSLASH	117
#define	SHARP	118
#define	INSERT	119
#define	PERIOD	120
#define	SQUOTE	121
#define	DQUOTE	122
#define	LETTER	123
#define	DIGIT	124
#define	NEWLN	125
#define	SPACE	126
#define	UNKN	127

/*
 * Special operators in intermediate code
 */
#define	BDATA	200
#define	WDATA	201
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
#define	ASSEM	223

/*
  Flag bits
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
#define	PCVOK	040000

/*
 * Conversion codes
 */
#define	ITF	1
#define	ITL	2
#define	LTF	3
#define	ITP	4
#define	PTI	5
#define	FTI	6
#define	LTI	7
#define	FTL	8
#define	LTP	9
#define	ITC	10
#define	XX	15

/*
 * symbol table flags
 */

#define	FMOS	01
#define	FTAG	02
#define	FENUM	03
#define	FUNION	04
#define	FKIND	07
#define	FFIELD	020
#define	FINIT	040
#define	FLABL	0100

/*
 * functions
 */
char	*sbrk();
union	tree *tree();
char	*copnum();
union	tree *convert();
union	tree *chkfun();
union	tree *disarray();
union	tree *block();
union	tree *cblock();
union	tree *fblock();
char	*Dblock();
char	*Tblock();
char	*starttree();
union	tree *pexpr();
union	str *strdec();
union	tree *xprtype();
struct	nmlist *pushdecl();
unsigned hash();
union	tree *structident();
struct	nmlist *gentemp();
union	tree *nblock();
