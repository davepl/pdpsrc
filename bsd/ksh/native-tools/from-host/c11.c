/*
 *  C compiler, part 2
 */

#if     !defined(lint) && defined(DOSCCS)
static  char    sccsid[] = "@(#)c11.c   2.3 (2.11BSD) 2025/8/29";
#endif

#include "c1.h"

degree(t)
register union tree *t;
{
	register union tree *t1;

	if (t==NULL || t->t.op==0)
		return(0);
	if (t->t.op == CON)
		return(-3);
	if (t->t.op == AMPER)
		return(-2);
	if (t->t.op==ITOL) {
		if ((t1 = isconstant(t)) && (t1->c.value>=0 || uns(t1)))
			return(-2);
		if (uns(t1 = t->t.tr1) && opdope[t1->t.op]&LEAF)
			return(-1);
	}
	if ((opdope[t->t.op] & LEAF) != 0) {
		if (t->t.type==CHAR || t->t.type==UNCHAR || t->t.type==FLOAT)
			return(1);
		return(0);
	}
	return(t->t.degree);
}

pname(p, flag)
register union tree *p;
{
	register i;

loop:
	switch(p->t.op) {

	case LCON:
		printf("$%o", flag<=10? UNS(p->l.lvalue>>16):
		   UNS(p->l.lvalue));
		return;

	case SFCON:
	case CON:
		printf("$");
		psoct(p->c.value);
		return;

	case FCON:
		printf("L%d", (p->c.value>0? p->c.value: -p->c.value));
		return;

	case NAME:
		i = p->n.offset;
		if (flag>10)
			i += 2;
		if (i) {
			psoct(i);
			if (p->n.class!=OFFS)
				putchar('+');
			if (p->n.class==REG)
				error("Illegal use of register");
		}
		switch(p->n.class) {

		case SOFFS:
		case XOFFS:
			pbase(p);

		case OFFS:
			printf("(r%d)", p->n.regno);
			return;

		case EXTERN:
		case STATIC:
			pbase(p);
			return;

		case REG:
			printf("r%d", p->n.nloc);
			return;

		}
		error("Compiler error: pname");
		return;

	case AMPER:
		putchar('$');
		p = p->t.tr1;
		if (p->t.op==NAME && p->n.class==REG)
			error("Illegal use of register");
		goto loop;

	case AUTOI:
		printf("(r%d)%s", p->n.nloc, flag==1?"":"+");
		return;

	case AUTOD:
		printf("%s(r%d)", flag==2?"":"-", p->n.nloc);
		return;

	case STAR:
		p = p->t.tr1;
		putchar('*');
		goto loop;

	}
	error("compiler error: bad pname");
}

pbase(p)
register union tree *p;
{

	if (p->n.class==SOFFS || p->n.class==STATIC)
		printf("L%d", p->n.nloc);
	else
		printf("%s", p->x.name);
}

xdcalc(p, nrleft)
register union tree *p;
{
	register d;

	if (p==NULL)
		return(0);
	d = dcalc(p, nrleft);
	if (d<20 && (p->t.type==CHAR || p->t.type==UNCHAR)) {
		if (nrleft>=1)
			d = 20;
		else
			d = 24;
	}
	return(d);
}

dcalc(p, nrleft)
register union tree *p;
{
	register union tree *p1;

	if (p==NULL)
		return(0);
	switch (p->t.op) {

	case NAME:
		if (p->n.class==REG && p->n.type!=CHAR && p->n.type!=UNCHAR)
			return(9);

	case AMPER:
	case FCON:
	case LCON:
	case AUTOI:
	case AUTOD:
		return(12);

	case CON:
	case SFCON:
		if (p->c.value==0)
			return(4);
		if (p->c.value==1)
			return(5);
		if (p->c.value > 0)
			return(8);
		return(12);

	case STAR:
		p1 = p->t.tr1;
		if (p1->t.op==NAME||p1->t.op==CON||p1->t.op==AUTOI||p1->t.op==AUTOD)
			if (p->t.type!=LONG && p->t.type!=UNLONG)
				return(12);
	}
	if (p->t.type==LONG || p->t.type==UNLONG)
		nrleft--;
	return(p->t.degree <= nrleft? 20: 24);
}

notcompat(p, ast, deg, op)
register union tree *p;
{
	unsigned register at, st;

	at = p->t.type;
	/*
	 * an e or n UNCHAR is to be considered an UNSIGNED,
	 * as long as it is not pointed to.
	 */
	if (at==UNCHAR && deg<0100 && deg>=20)
		at = UNSIGN;
	st = ast;
	if (st==0)		/* word, byte */
		return(at!=CHAR && at!=INT && at!=UNSIGN && at<PTR);
	if (st==1)		/* word */
		return(at!=INT && at!=UNSIGN && at<PTR);
	if (st==9 && (at&XTYPE))
		return(0);
	st -= 2;
	if ((at&(~(TYPE+XTYPE))) != 0)
		at = 020;
	if ((at&(~TYPE)) != 0)
		at = at&TYPE | 020;
	if (st==FLOAT && at==DOUBLE)
		at = FLOAT;
	if (p->t.op==NAME && p->n.class==REG && op==ASSIGN && st==CHAR)
		return(0);
	return(st != at);
}

prins(op, c, itable, lbl)
struct instab *itable;
{
	extern char	jmijne[];
	register struct instab	*insp;
	register char	*ip;
	register int	skip;

	skip = 0;
	for (insp = itable; insp->iop != 0; insp++) {
		if (insp->iop == op) {
			ip = c ? insp->str2: insp->str1;
			if (!ip)
				break;
			if (ip != jmijne) {
				printf(ip, lbl);
			}
			else {
				skip = isn++;
				printf(ip, skip);
			}
			return(skip);
		}
	}
	error("No match' for op %d", op);
 	return(skip);
}

collcon(p)
register union tree *p;
{
	register op;

	if (p==NULL)
		return(0);
	if (p->t.op==STAR) {
		if (p->t.type==LONG+PTR || p->t.type==UNLONG+PTR) /* avoid *x(r); *x+2(r) */
			return(0);
		p = p->t.tr1;
	}
	if (p->t.op==PLUS) {
		op = p->t.tr2->t.op;
		if (op==CON || op==AMPER)
			return(1);
	}
	return(0);
}

isfloat(t)
register union tree *t;
{

	if ((opdope[t->t.op]&RELAT)!=0)
		t = t->t.tr1;
	if (t->t.type==FLOAT || t->t.type==DOUBLE) {
		nfloat = 1;
		return('f');
	}
	return(0);
}

oddreg(t, reg)
register union tree *t;
register reg;
{

	if (!isfloat(t)) {
		if (opdope[t->t.op]&RELAT) {
			if (t->t.tr1->t.type==LONG || t->t.tr1->t.type==UNLONG)
				return((reg+1) & ~01);
			return(reg);
		}
		switch(t->t.op) {
		case ULLSHIFT:
		case UASLSHL:
		case LLSHIFT:
		case ASLSHL:
		case PTOI:
			return((reg+1)&~01);

		case DIVIDE:
		case MOD:
		case ASDIV:
		case ASMOD:
		case ULSH:
		case ASULSH:
			reg++;

		case TIMES:
		case ASTIMES:
			return(reg|1);
		}
	}
	return(reg);
}

arlength(t)
{
	if (t>=PTR)
		return(2);
	switch(t) {

	case INT:
	case CHAR:
	case UNSIGN:
	case UNCHAR:
		return(2);

	case UNLONG:
	case LONG:
		return(4);

	case FLOAT:
	case DOUBLE:
		return(8);
	}
	error("botch: peculiar type %d", t);
	return(1024);
}

/*
 * Strings for switch code.
 */

/*
 * Modified Memorial day May 80 to uniquely identify switch tables
 * (as on Vax) so a shell script can optionally include them in RO code.
 * This is useful in overlays to reduce the size of data space load.
 * wfj 5/80 
 */
char	dirsw[] = {"\
cmp	r0,$%o\n\
jhi	L%d\n\
asl	r0\n\
jmp	*L%d(r0)\n\
\t.data\n\
L%d:\
" };

char	hashsw[] = {"\
mov	r0,r1\n\
clr	r0\n\
div	$%o,r0\n\
asl	r1\n\
jmp	*L%d(r1)\n\
\t.data\n\
L%d:\
"};

/*
 * If the unsigned casts below won't compile,
 * try using the calls to lrem and ldiv.
 */

pswitch(afp, alp, deflab)
struct swtab *afp, *alp;
{
	int ncase, i, j, tabs, worst, best, range;
	register struct swtab *swp, *fp, *lp;
	int *poctab;

	fp = afp;
	lp = alp;
	if (fp==lp) {
		printf("jbr	L%d\n", deflab);
		return;
	}
	isn++;
	if (sort(fp, lp))
		return;
	ncase = lp-fp;
	lp--;
	range = lp->swval - fp->swval;
	/* direct switch */
	if (range>0 && range <= 3*ncase) {
		if (fp->swval)
			printf("sub	$%o,r0\n", UNS(fp->swval));
		printf(dirsw, UNS(range), deflab, isn, isn);
		isn++;
		for (i=fp->swval; ; i++) {
			if (i==fp->swval) {
				printf("L%d\n", fp->swlab);
				if (fp==lp)
					break;
				fp++;
			} else
				printf("L%d\n", deflab);
		}
		printf(".text\n");
		return;
	}
	/* simple switch */
	if (ncase<10) {
		for (fp = afp; fp<=lp; fp++)
			breq(fp->swval, fp->swlab);
		printf("jbr	L%d\n", deflab);
		return;
	}
	/* hash switch */
	best = 077777;
	poctab = (int *)getblk(((ncase+2)/2) * sizeof(*poctab));
	for (i=ncase/4; i<=ncase/2; i++) {
		for (j=0; j<i; j++)
			poctab[j] = 0;
		for (swp=fp; swp<=lp; swp++)
			/* lrem(0, swp->swval, i) */
			poctab[(unsigned)swp->swval%i]++;
		worst = 0;
		for (j=0; j<i; j++)
			if (poctab[j]>worst)
				worst = poctab[j];
		if (i*worst < best) {
			tabs = i;
			best = i*worst;
		}
	}
	i = isn++;
	printf(hashsw, UNS(tabs), i, i);
	isn++;
	for (i=0; i<tabs; i++)
		printf("L%d\n", isn+i);
	printf(".text\n");
	for (i=0; i<tabs; i++) {
		printf("L%d:", isn++);
		for (swp=fp; swp<=lp; swp++) {
			/* lrem(0, swp->swval, tabs) */
			if ((unsigned)swp->swval%tabs == i) {
				/* ldiv(0, swp->swval, tabs) */
				breq((int)((unsigned)swp->swval/tabs), swp->swlab);
			}
		}
		printf("jbr	L%d\n", deflab);
	}
}

breq(v, l)
{
	if (v==0)
		printf("tst	r0\n");
	else
		printf("cmp	r0,$%o\n", UNS(v));
	printf("jeq	L%d\n", l);
}

sort(afp, alp)
struct swtab *afp, *alp;
{
	register struct swtab *cp, *fp, *lp;
	int intch, t;

	fp = afp;
	lp = alp;
	while (fp < --lp) {
		intch = 0;
		for (cp=fp; cp<lp; cp++) {
			if (cp->swval == cp[1].swval) {
				error("Duplicate case (%d)", cp->swval);
				return(1);
			}
			if (cp->swval > cp[1].swval) {
				intch++;
				t = cp->swval;
				cp->swval = cp[1].swval;
				cp[1].swval = t;
				t = cp->swlab;
				cp->swlab = cp[1].swlab;
				cp[1].swlab = t;
			}
		}
		if (intch==0)
			break;
	}
	return(0);
}

ispow2(tree)
register union tree *tree;
{
	register int d;

	if (!isfloat(tree) && tree->t.tr2->t.op==CON) {
		d = tree->t.tr2->c.value;
		if (d>1 && (d&(d-1))==0)
			return(d);
	}
	return(0);
}

union tree *
pow2(tree)
register union tree *tree;
{
	register int d, i;

	if (d = ispow2(tree)) {
		for (i=0; (d>>=1)!=0; i++);
		tree->t.tr2->c.value = i;
		switch (tree->t.op) {

		case TIMES:
			tree->t.op = LSHIFT;
			break;

		case ASTIMES:
			tree->t.op = ASLSH;
			break;

		case PTOI:
			if (i==1 && tree->t.tr1->t.op==MINUS && !isconstant(tree->t.tr1->t.tr2)) {
				tree->t.op = PTOI1;
				tree->t.tr1 = tnode(LTOI, INT, tree->t.tr1, TNULL);
				return(optim(tree));
			}
			tree->t.op = LLSHIFT;
			tree->t.tr2->c.value = -i;
			i = tree->t.type;
			tree->t.type = LONG;
			tree = tnode(LTOI, i, tree, TNULL);
			break;

		case DIVIDE:
			tree->t.op = ULSH;
			tree->t.tr2->c.value = -i;
			break;

		case ASDIV:
			tree->t.op = ASULSH;
			tree->t.tr2->c.value = -i;
			break;

		case MOD:
			tree->t.op = AND;
			tree->t.tr2->c.value = (1<<i)-1;
			break;

		case ASMOD:
			tree->t.op = ASAND;
			tree->t.tr2->c.value = (1<<i)-1;
			break;

		default:
			error("pow2 botch");
		}
		tree = optim(tree);
	}
	return(tree);
}

cbranch(atree, lbl, cond, reg)
union tree *atree;
register lbl, reg;
{
	int l1, op;
	register union tree *tree;

again:
	if ((tree=atree)==NULL)
		return;
	switch(tree->t.op) {

	case LOGAND:
		if (cond) {
			cbranch(tree->t.tr1, l1=isn++, 0, reg);
			cbranch(tree->t.tr2, lbl, 1, reg);
			label(l1);
		} else {
			cbranch(tree->t.tr1, lbl, 0, reg);
			cbranch(tree->t.tr2, lbl, 0, reg);
		}
		return;

	case LOGOR:
		if (cond) {
			cbranch(tree->t.tr1, lbl, 1, reg);
			cbranch(tree->t.tr2, lbl, 1, reg);
		} else {
			cbranch(tree->t.tr1, l1=isn++, 1, reg);
			cbranch(tree->t.tr2, lbl, 0, reg);
			label(l1);
		}
		return;

	case EXCLA:
		cbranch(tree->t.tr1, lbl, !cond, reg);
		return;

	case SEQNC:
		rcexpr(tree->t.tr1, efftab, reg);
		atree = tree->t.tr2;
		goto again;

	case ITOL:
		tree = tree->t.tr1;
		break;

	case QUEST:
		l1 = isn;
		isn += 2;
		cbranch(tree->t.tr1, l1, 0, reg);
		cbranch(tree->t.tr2->t.tr1, lbl, cond, reg);
		branch(l1+1, 0, 0);
		label(l1);
		cbranch(tree->t.tr2->t.tr2, lbl, cond, reg);
		label(l1+1);
		return;

	}
	op = tree->t.op;
	if (opdope[op]&RELAT
	 && tree->t.tr1->t.op==ITOL && tree->t.tr2->t.op==ITOL
	 && uns(tree->t.tr1->t.tr1) == uns(tree->t.tr2->t.tr1)) {
		tree->t.tr1 = tree->t.tr1->t.tr1;
		tree->t.tr2 = tree->t.tr2->t.tr1;
		if (op>=LESSEQ && op<=GREAT
		 && uns(tree->t.tr1))
			tree->t.op = op = op+LESSEQP-LESSEQ;
	}
	if (tree->t.type==LONG || tree->t.type==UNLONG
	  || opdope[op]&RELAT&&(tree->t.tr1->t.type==LONG || tree->t.tr1->t.type==UNLONG)) {
		longrel(tree, lbl, cond, reg);
		return;
	}
	rcexpr(tree, cctab, reg);
	op = tree->t.op;
	if ((opdope[op]&RELAT)==0)
		op = NEQUAL;
	else {
		l1 = tree->t.tr2->t.op;
		if ((l1==CON || l1==SFCON) && tree->t.tr2->c.value==0)
			op += 200;		/* special for ptr tests */
		else
			op = maprel[op-EQUAL];
	}
	if (isfloat(tree))
		printf("cfcc\n");
	branch(lbl, op, !cond);
}

branch(lbl, aop, c)
{
	register int	op,
			skip;

	if(op = aop) {
		skip = prins(op, c, branchtab, lbl);
	} else {
		printf("jbr");
		skip = 0;
	}
	if (skip)
		printf("\tL%d\nL%d:", lbl, skip);
	else
		printf("\tL%d\n", lbl);
}

longrel(atree, lbl, cond, reg)
union tree *atree;
{
	int xl1, xl2, xo, xz;
	register int op, isrel;
	register union tree *tree;

	if (reg&01)
		reg++;
	reorder(&atree, cctab, reg);
	tree = atree;
	isrel = 0;
	if (opdope[tree->t.op]&RELAT) {
		isrel++;
		op = tree->t.op;
	} else
		op = NEQUAL;
	if (!cond)
		op = notrel[op-EQUAL];
	xl1 = xlab1;
	xl2 = xlab2;
	xo = xop;
	xlab1 = lbl;
	xlab2 = 0;
	xop = op;
	xz = xzero;
	xzero = !isrel || (tree->t.tr2->t.op==ITOL && tree->t.tr2->t.tr1->t.op==CON
		&& tree->t.tr2->t.tr1->c.value==0);
	if (tree->t.op==ANDN) {
		tree->t.op = TAND;
		tree->t.tr2 = optim(tnode(COMPL, LONG, tree->t.tr2, TNULL));
	}
	if (cexpr(tree, cctab, reg) < 0) {
		reg = rcexpr(tree, regtab, reg);
		printf("ashc	$0,r%d\n", reg);
		branch(xlab1, op, 0);
	}
	xlab1 = xl1;
	xlab2 = xl2;
	xop = xo;
	xzero = xz;
}

/*
 * Tables for finding out how best to do long comparisons.
 * First dimen is whether or not the comparison is with 0.
 * Second is which test: e.g. a>b->
 *	cmp	a,b
 *	bgt	YES		(first)
 *	blt	NO		(second)
 *	cmp	a+2,b+2
 *	bhi	YES		(third)
 *  NO:	...
 * Note some tests may not be needed.
 *
 * EQUAL = 60
 * NEQUAL= 61
 * LESSEQ= 62
 * LESS  = 63
 * GREATEQ=64
 * GREAT  =65
 * LESSEQP=66
 * LESSP  =67
 * GREATQP=68
 * GREATP =69
 *
 * Third dimension (lrtab[][][x]) indexed by "x - EQUAL".
 */
char	lrtab[2][3][10] = {
	0, NEQUAL, LESS, LESS, GREAT, GREAT, LESSP, LESSP, GREATP, GREATP,
	NEQUAL,	0, GREAT, GREAT, LESS, LESS, GREATP, GREATP, LESSP, LESSP,
	EQUAL,NEQUAL,LESSEQP,LESSP, GREATQP,GREATP,LESSEQP,LESSP,GREATQP,GREATP,

	0, NEQUAL, LESS, LESS,	GREATEQ,GREAT, LESSP, LESSP, GREATQP, GREATP,
	NEQUAL,	0, GREAT, 0, 0,	LESS, GREATP, 0, 0, LESSP,
	EQUAL,	NEQUAL,	EQUAL,	0, 0, NEQUAL, EQUAL, 0, 0, NEQUAL,
};

xlongrel(f)
{
	register int op, bno;

	op = xop;
	if (f==0) {
		if (bno = lrtab[xzero][0][op-EQUAL])
			branch(xlab1, bno, 0);
		if (bno = lrtab[xzero][1][op-EQUAL]) {
			xlab2 = isn++;
			branch(xlab2, bno, 0);
		}
		if (lrtab[xzero][2][op-EQUAL]==0)
			return(1);
	} else {
		branch(xlab1, lrtab[xzero][2][op-EQUAL], 0);
		if (xlab2)
			label(xlab2);
	}
	return(0);
}

label(l)
{
	printf("L%d:", l);
}

popstk(a)
{
	switch(a) {

	case 0:
		return;

	case 2:
		printf("tst	(sp)+\n");
		return;

	case 4:
		printf("cmp	(sp)+,(sp)+\n");
		return;
	}
	printf("add	$%o,sp\n", UNS(a));
}

werror(s)
char *s;
{

	fprintf(stderr, "%d: %s\n",line,s);
}

/* VARARGS1 */
error(s, p1, p2, p3, p4, p5, p6)
char *s;
{

	nerror++;
	fprintf(stderr, "%d: ", line);
	fprintf(stderr, s, p1, p2, p3, p4, p5, p6);
	putc('\n', stderr);
}

psoct(an)
{
	register int n;
	register char *sign;

	sign = "";
	if ((n = an) < 0) {
		n = -n;
		sign = "-";
	}
	printf("%s%o", sign, n);
}

/*
 * Read in an intermediate file.
 */
#define	STKS	100
getree()
{
	union tree *expstack[STKS], **sp;
	register union tree *tp;
	register int t, op;
	char s[80];		/* big for asm() stuff & long variable names */
	struct swtab *swp;
	long outloc;
	int lbl, cond, lbl2, lbl3, gotrsh;
	double atof();

	gotrsh = 0;
	curbase = funcbase;
	sp = expstack;
	for (;;) {
		if (sp >= &expstack[STKS])
			error("Stack overflow botch");
		op = geti();
		if ((op&0177400) != 0177000) {
			error("Intermediate file error");
			exit(1);
		}
		lbl = 0;
		switch(op &= 0377) {

	case SINIT:
		printf("%o\n", UNS(geti()));
		break;

	case EOFC:
		return;

	case BDATA:
		if (geti() == 1) {
			printf(".byte ");
			for (;;)  {
				printf("%o", UNS(geti()));
				if (geti() != 1)
					break;
				printf(",");
			}
			printf("\n");
		}
		break;

	case PROG:
		printf(".text\n");
		break;

	case DATA:
		printf(".data\n");
		break;

	case BSS:
		printf(".bss\n");
		break;

	case SYMDEF:
		outname(s);
		printf(".globl\t%s\n", s);
		sfuncr.nloc = 0;
		break;

	case RETRN:
		printf("jmp\tcret\n");
		break;

	case CSPACE:
		outname(s);
		printf(".comm\t%s,%o\n", s, UNS(geti()));
		break;

	case SSPACE:
		printf(".=.+%o\n", UNS(t=geti()));
		totspace += (unsigned)t;
		break;

	case EVEN:
		printf(".even\n");
		break;

	case SAVE:
		printf("jsr	r5,csv\n");
		break;

	case SETSTK:
		t = geti();
		if (t==2)
			printf("tst	-(sp)\n");
		else if (t != 0)
			printf("sub	$%o,sp\n", UNS(t));
		break;

	case PROFIL:
		t = geti();
		outname(s);
		printf("mov	$L%d,r0\njsr	pc,mcount\n", t);
		printf(".data\nL%d:%s+1\n.text\n", t, s);
		break;

	case ASSEM:
		outname(s);
		printf("%s\n", s);
		break;

	case SNAME:
		outname(s);
		printf("~%s=L%d\n", s+1, geti());
		break;

	case ANAME:
		outname(s);
		printf("~%s=%o\n", s+1, UNS(geti()));
		break;

	case RNAME:
		outname(s);
		printf("~%s=r%d\n", s+1, geti());
		break;

	case SWIT:
		t = geti();
		line = geti();
		curbase = funcbase;
		while(swp=(struct swtab *)getblk(sizeof(*swp)), swp->swlab = geti())
			swp->swval = geti();
		pswitch((struct swtab *)funcbase, swp, t);
		break;

	case C3BRANCH:		/* for fortran [sic] */
		lbl = geti();
		lbl2 = geti();
		lbl3 = geti();
		goto xpr;

	case CBRANCH:
		lbl = geti();
		cond = geti();

	case EXPR:
	xpr:
		line = geti();
		if (sp != &expstack[1]) {
			error("Expression input botch");
			exit(1);
		}
		--sp;
		regpanic = 0;
		if (setjmp(jmpbuf)) {
			regpanic = 10;
			fseek(stdout, outloc, 0);
		}
		nstack = 0;
		panicposs = 0;
		*sp = tp = optim(*sp);
		if (regpanic==0 && panicposs)
			outloc = ftell(stdout);
		if (op==CBRANCH)
			cbranch(tp, lbl, cond, 0);
		else if (op==EXPR) {
			int onreg = nreg;
			if (gotrsh)
				nreg = 1;
			rcexpr(tp, efftab, 0);
			nreg = onreg;
			gotrsh = 0;
		} else {
			if (tp->t.type==LONG || tp->t.type==UNLONG) {
				rcexpr(tnode(RFORCE, tp->t.type, tp, TNULL), efftab, 0);
				printf("ashc	$0,r0\n");
			} else {
				rcexpr(tp, cctab, 0);
				if (isfloat(tp))
					printf("cfcc\n");
			}
			printf("jgt	L%d\n", lbl3);
			printf("jlt	L%d\njbr	L%d\n", lbl, lbl2);
		}
		curbase = funcbase;
		break;

	case NAME:
		t = geti();
		if (t==EXTERN) {
			tp = getblk(sizeof(struct xtname));
			tp->t.type = geti();
			outname(s);
			tp->x.name = (char *)getblk(strlen(s) + 1);
			strcpy(tp->x.name, s);
		} else {
			tp = getblk(sizeof(struct tname));
			tp->t.type = geti();
			tp->n.nloc = geti();
		}
		tp->t.op = NAME;
		tp->n.class = t;
		tp->n.regno = 0;
		tp->n.offset = 0;
		*sp++ = tp;
		break;

	case CON:
		t = geti();
		*sp++ = tconst(geti(), t);
		break;

	case LCON:
		geti();	/* ignore type, assume long */
		t = geti();
		op = geti();
		if (t==0 && op>=0 || t == -1 && op<0) {
			*sp++ = tnode(ITOL, LONG, tconst(op, INT), TNULL);
			break;
		}
		tp = getblk(sizeof(struct lconst));
		tp->t.op = LCON;
		tp->t.type = LONG;
		tp->l.lvalue = ((long)t<<16) + UNS(op);	/* nonportable */
		*sp++ = tp;
		break;

	case FCON:
		t = geti();
		outname(s);
		tp = getblk(sizeof(struct ftconst));
		tp->t.op = FCON;
		tp->t.type = t;
		tp->f.value = isn++;
		tp->f.fvalue = atof(s);
		*sp++ = tp;
		break;

	case FSEL:
		tp = tnode(FSEL, geti(), *--sp, TNULL);
		t = geti();
		tp->t.tr2 = tnode(COMMA, INT, tconst(geti(), INT), tconst(t, INT));
		if (tp->t.tr2->t.tr1->c.value==16)
			tp = paint(tp->t.tr1, tp->t.type);
		*sp++ = tp;
		break;

	case STRASG:
		tp = getblk(sizeof(struct fasgn));
		tp->t.op = STRASG;
		tp->t.type = geti();
		tp->F.mask = geti();
		tp->t.tr1 = *--sp;
		tp->t.tr2 = NULL;
		*sp++ = tp;
		break;

	case NULLOP:
		*sp++ = tnode(0, 0, TNULL, TNULL);
		break;

	case LABEL:
		label(geti());
		break;

	case NLABEL:
		outname(s);
		printf("%s:\n", s);
		break;

	case RLABEL:
		outname(s);
		printf("%s:\n~~%s:\n", s, s+1);
		break;

	case BRANCH:
		branch(geti(), 0, 0);
		break;

	case SETREG:
		nreg = geti()-1;
		break;

	default:
		if (opdope[op]&BINARY) {
			if (sp < &expstack[1]) {
				error("Binary expression botch");
				exit(1);
			}
			tp = *--sp;
			--sp;
			*sp = tnode(op, geti(), *sp, tp);
			if ((*sp)->t.type == UNLONG &&
			    (op == ASRSH || op == RSHIFT))
				gotrsh = 1;
			sp++;
		} else
			sp[-1] = tnode(op, geti(), sp[-1], TNULL);
		break;
	}
	}
}

geti()
{
	register short i;

	i = getchar() & 0xff;
	i |= (getchar() & 0xff) << 8;
	return(i);
}

static
outname(s)
register char *s;
{
	register int c;

	while (c = getchar())
		*s++ = c;
	*s++ = '\0';
}

strasg(atp)
union tree *atp;
{
	register union tree *tp;
	register nwords, i;

	nwords = atp->F.mask/sizeof(short);
	tp = atp->t.tr1;
	while (tp->t.op == SEQNC) {
		rcexpr(tp->t.tr1, efftab, 0);
		tp = tp->t.tr2;
	}
	if (tp->t.op != ASSIGN) {
		if (tp->t.op==RFORCE) {	/* function return */
			if (sfuncr.nloc==0) {
				sfuncr.nloc = isn++;
				printf(".bss\nL%d:.=.+%o\n.text\n", sfuncr.nloc,
					UNS(nwords*sizeof(short)));
			}
			atp->t.tr1 = tnode(ASSIGN, STRUCT, (union tree *)&sfuncr, tp->t.tr1);
			strasg(atp);
			printf("mov	$L%d,r0\n", sfuncr.nloc);
			return;
		}
		if (tp->t.op==CALL) {
			rcexpr(tp, efftab, 0);
			return;
		}
		error("Illegal structure operation");
		return;
	}
	tp->t.tr2 = strfunc(tp->t.tr2);
	if (nwords==1)
		paint(tp, INT);
	else if (nwords==sizeof(short))
		paint(tp, LONG);
	else {
		if (tp->t.tr1->t.op!=NAME && tp->t.tr1->t.op!=STAR
		 || tp->t.tr2->t.op!=NAME && tp->t.tr2->t.op!=STAR) {
			error("unimplemented structure assignment");
			return;
		}
		tp->t.tr1 = tnode(AMPER, STRUCT+PTR, tp->t.tr1, TNULL);
		tp->t.tr2 = tnode(AMPER, STRUCT+PTR, tp->t.tr2, TNULL);
		tp->t.op = STRSET;
		tp->t.type = STRUCT+PTR;
		tp = optim(tp);
		rcexpr(tp, efftab, 0);
		if (nwords < 7) {
			for (i=0; i<nwords; i++)
				printf("mov	(r1)+,(r0)+\n");
			return;
		}
		if (nreg<=1)
			printf("mov	r2,-(sp)\n");
		printf("mov	$%o,r2\n", UNS(nwords));
		printf("L%d:mov	(r1)+,(r0)+\ndec\tr2\njne\tL%d\n", isn, isn);
		isn++;
		if (nreg<=1)
			printf("mov	(sp)+,r2\n");
		return;
	}
	rcexpr(tp, efftab, 0);
}

/*
 * Reduce the degree-of-reference by one.
 * e.g. turn "ptr-to-int" into "int".
 */
decref(t)
register t;
{
	if ((t & ~TYPE) == 0) {
		error("Illegal indirection");
		return(t);
	}
	return(((unsigned)t>>TYLEN) & ~TYPE | t&TYPE);
}

/*
 * Increase the degree of reference by
 * one; e.g. turn "int" to "ptr-to-int".
 */
incref(t)
{
	return(((t&~TYPE)<<TYLEN) | (t&TYPE) | PTR);
}
