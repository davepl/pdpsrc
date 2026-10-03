/*
 * C compiler
 */
#if	!defined(lint) && defined(DOSCCS)
static	char	sccsid[] = "@(#)c02.c	2.0 (2.11BSD) 2020/1/7";
#endif

#include "c0.h"

/*
 * Process a single external definition
 */
extdef()
{
	register o;
	int sclass, scflag;
	struct nmlist typer;
	register struct nmlist *ds;

	if(((o=symbol())==EOFC) || o==SEMI)
		return;
	peeksym = o;
	sclass = 0;
	blklev = 0;
	if (getkeywords(&sclass, &typer)==0) {
		sclass = EXTERN;
		if (peeksym!=NAME)
			goto syntax;
	}
	scflag = 0;
	if (sclass==DEFXTRN) {
		scflag++;
		sclass = EXTERN;
	}
	if (sclass!=EXTERN && sclass!=STATIC && sclass!=TYPEDEF)
		error("Illegal storage class");
	do {
		defsym = 0;
		paraml = NULL;
		parame = NULL;
		if (sclass==TYPEDEF) {
			decl1(TYPEDEF, &typer, 0, (struct nmlist *)NULL);
			continue;
		}
		decl1(EXTERN, &typer, 0, (struct nmlist *)NULL);
		if ((ds=defsym)==0)
			return;
		funcsym = ds;
		if ((ds->htype&XTYPE)==FUNC) {
			if ((peeksym=symbol())==LBRACE || peeksym==KEYW
			 || (peeksym==NAME && csym->hclass==TYPEDEF)) {
				funcblk.type = decref(ds->htype);
				funcblk.strp = ds->hstrp;
				setinit(ds);
				outcode("BS", SYMDEF, sclass==EXTERN?ds->name:"");
				cfunc();
				return;
			} else if (peeksym == COMMA || peeksym == SEMI) {
				/* clear potential named args */
				blklev++;
				blkend();
				paraml = NULL;
			}
			if (paraml)
				error("Inappropriate parameters");
		} else if ((o=symbol())==COMMA || o==SEMI) {
			peeksym = o;
			o = (length((union tree *)ds)+ALIGN) & ~ALIGN;
			if (sclass==STATIC) {
				setinit(ds);
				outcode("BSBBSBN", SYMDEF, "", BSS, NLABEL, ds->name, SSPACE, o);
			} else if (scflag)
				outcode("BSN", CSPACE, ds->name, o);
		} else {
			if (o!=ASSIGN) {
				error("Declaration syntax");
				peeksym = o;
			}
			setinit(ds);
			if (sclass==EXTERN)
				outcode("BS", SYMDEF, ds->name);
			outcode("BBS", DATA, NLABEL, ds->name);
			if (cinit(ds, 1, sclass) & ALIGN)
				outcode("B", EVEN);
		}
	} while ((o=symbol())==COMMA);
	if (o==SEMI)
		return;
syntax:
	if (o==RBRACE) {
		error("Too many }'s");
		peeksym = 0;
		return;
	}
	error("External definition syntax");
	errflush(o);
	statement();
}

/*
 * Process a function definition.
 */
cfunc()
{
	register char *cb;
	register sloc;

	sloc = isn;
	isn += 2;
	outcode("BBS", PROG, RLABEL, funcsym->name);
	regvar = 5;
	autolen = STAUTO;
	maxauto = STAUTO;
	blklev = 1;
	cb = locbase;
	declist(ARG);
	outcode("B", SAVE);
	if (proflg)
		outcode("BNS", PROFIL, isn++, funcsym->name);
	funchead();
	branch(sloc);
	label(sloc+1);
	retlab = isn++;
	blklev = 0;
	if ((peeksym = symbol()) != LBRACE)
		error("Compound statement required");
	statement();
	outcode("BNB", LABEL, retlab, RETRN);
	label(sloc);
/* add STAUTO; overlay bug fix, coupled with section in c11.c */
	outcode("BN", SETSTK, -maxauto+STAUTO);
	branch(sloc+1);
	locbase = cb;
}

/*
 * Process the initializers for an external definition.
 */
cinit(anp, flex, sclass)
struct nmlist *anp;
{
	struct nmlist np;
	register nel, ninit;
	int width, isarray, o, brace, realtype;
	union tree *s;

	np = *anp;
	realtype = np.htype;
	isarray = 0;
	if ((realtype&XTYPE) == ARRAY)
		isarray++;
	else
		flex = 0;
	width = length((union tree *)&np);
	nel = 1;
	/*
	 * If it's an array, find the number of elements.
	 * temporarily modify to look like kind of thing it's
	 * an array of.
	 */
	if (sclass==AUTO)
		if (isarray || realtype==STRUCT)
			error("No auto. aggregate initialization");
	if (isarray) {
		np.htype = decref(realtype);
		np.hsubsp++;
		if (width==0 && flex==0)
			error("0-length row: %s", anp->name);
		o = length((union tree *)&np);
		nel = (unsigned)width/o;
		width = o;
	}
	brace = 0;
	if ((peeksym=symbol())==LBRACE && (isarray || np.htype!=STRUCT)) {
		peeksym = -1;
		brace++;
	}
	ninit = 0;
	do {
		if ((o=symbol())==RBRACE)
			break;
		peeksym = o;
		if (o==STRING && (realtype==ARRAY+CHAR || realtype==ARRAY+UNCHAR)) {
			if (sclass==AUTO)
				error("No strings in automatic");
			peeksym = -1;
			putstr(0, flex?10000:nel);
			ninit += nchstr;
			o = symbol();
			break;
		} else if (np.htype==STRUCT) {
			strinit(&np, sclass);
		} else if ((np.htype&ARRAY)==ARRAY || peeksym==LBRACE)
			cinit(&np, 0, sclass);
		else {
			char *st;
			initflg++;
			st = starttree();
			s = tree(0);
			initflg = 0;
			if (np.hflag&FFIELD)
				error("No field initialization");
			*cp++ = nblock(&np);
			*cp++ = s;
			build(ASSIGN);
			if (sclass==AUTO||sclass==REG)
				rcexpr(*--cp);
			else if (sclass==ENUMCON) {
				if (s->t.op!=CON)
					error("Illegal enum constant for %s", anp->name);
				anp->hoffset = s->c.value;
			} else
				rcexpr(block(INIT,np.htype,(int *)NULL,
				  (union str *)NULL, (*--cp)->t.tr2, TNULL));
			endtree(st);
		}
		ninit++;
		if ((ninit&077)==0 && sclass==EXTERN)
			outcode("BS", SYMDEF, "");
	} while ((o=symbol())==COMMA && (ninit<nel || brace || flex));
	if (brace==0 || o!=RBRACE)
		peeksym = o;
	/*
	 * If there are too few initializers, allocate
	 * more storage.
	 * If there are too many initializers, extend
	 * the declared size for benefit of "sizeof"
	 */
	if (ninit<nel && sclass!=AUTO)
		outcode("BN", SSPACE, (nel-ninit)*width);
	else if (ninit>nel) {
		if (flex && nel==0) {
			np.hsubsp[-1] = ninit;
		} else
			error("Too many initializers: %s", anp->name);
		nel = ninit;
	}
	return(nel*width);
}

/*
 * Initialize a structure
 */
strinit(np, sclass)
struct nmlist *np;
{
	static struct nmlist junk;
	register struct nmlist **mlp;
	static struct nmlist *zerloc = NULL;
	register int o, brace;

	if ((mlp = np->hstrp->S.memlist)==NULL) {
		mlp = &zerloc;
		error("Undefined structure initialization");
	}
	brace = 0;
	if ((o = symbol()) == LBRACE)
		brace++;
	else
		peeksym = o;
	do {
		if ((o=symbol()) == RBRACE)
			break;
		peeksym = o;
		if (*mlp==0) {
			error("Too many structure initializers");
			cinit(&junk, 0, sclass);
		} else
			cinit(*mlp++, 0, sclass);
		if (*mlp ==  &structhole) {
			outcode("B", EVEN);
			mlp++;
		}
				/* DAG -- union initialization bug fix */
		if (*mlp && mlp[-1]->hoffset == (*mlp)->hoffset) {
			werror("union initialization non-portable");
			while (*mlp)	/* will NOT be &structhole */
				mlp++;	/* skip other members of union */
		}
	} while ((o=symbol())==COMMA && (*mlp || brace));
	if (sclass!=AUTO && sclass!=REG) {
		if (*mlp)
			outcode("BN", SSPACE, np->hstrp->S.ssize - (*mlp)->hoffset);
		outcode("B", EVEN);
	}
	if (o!=RBRACE || brace==0)
		peeksym = o;
}

/*
 * Mark already initialized
 */
setinit(np)
register struct nmlist *np;
{

	if (np->hflag&FINIT)
		error("%s multiply defined", np->name);
	np->hflag |= FINIT;
}

/*
 * Process one statement in a function.
 */
statement()
{
	register o, o1;
	int sauto, sreg;

stmt:
	switch(o=symbol()) {

	case EOFC:
		error("Unexpected EOF");
	case SEMI:
		return;

	case LBRACE:
		sauto = autolen;
		sreg = regvar;
		blockhead();
		while (!eof) {
			if ((o=symbol())==RBRACE) {
				autolen = sauto;
				if (sreg!=regvar)
					outcode("BN", SETREG, sreg);
				regvar = sreg;
				blkend();
				return;
			}
			peeksym = o;
			statement();
		}
		error("Missing '}'");
		return;

	case KEYW:
		switch(cval) {

		case GOTO:
			if (o1 = simplegoto())
				branch(o1);
			else 
				dogoto();
			goto semi;

		case RETURN:
			doret();
			goto semi;

		case ASM:
		{
			char	tmp[80],	/* tmp for line buffer */
				*p;

			if (symbol() != LPARN || (o1 = symbol()) != STRING)
				goto syntax;
			for (p = tmp; (o1 = mapch('"')) >= 0; )
				*p++ = o1&0177;
			*p = '\0';
			if (symbol() != RPARN)
				goto syntax;
			outcode("BF", ASSEM, tmp);
			goto semi;
		}

		case IF: {
			register o2;
			register union tree *np;

			np = pexpr(1);
			o2 = 0;
			if ((o1=symbol())==KEYW) switch (cval) {
			case GOTO:
				if (o2=simplegoto())
					goto simpif;
				cbranch(np, o2=isn++, 0);
				dogoto();
				label(o2);
				goto hardif;

			case RETURN:
				if (nextchar()==';') {
					o2 = retlab;
					goto simpif;
				}
				cbranch(np, o1=isn++, 0);
				doret();
				label(o1);
				o2++;
				goto hardif;

			case BREAK:
				o2 = brklab;
				goto simpif;

			case CONTIN:
				o2 = contlab;
			simpif:
				chconbrk(o2);
				cbranch(np, o2, 1);
			hardif:
				if ((o=symbol())!=SEMI)
					goto syntax;
				if ((o1=symbol())==KEYW && cval==ELSE) 
					goto stmt;
				peeksym = o1;
				return;
			}
			peeksym = o1;
			cbranch(np, o1=isn++, 0);
			statement();
			if ((o=symbol())==KEYW && cval==ELSE) {
				o2 = isn++;
				branch(o2);
				label(o1);
				statement();
				label(o2);
				return;
			}
			peeksym = o;
			label(o1);
			return;
		}

		case WHILE: {
			register o2;
			o1 = contlab;
			o2 = brklab;
			label(contlab = isn++);
			cbranch(pexpr(1), brklab=isn++, 0);
			statement();
			branch(contlab);
			label(brklab);
			contlab = o1;
			brklab = o2;
			return;
		}

		case BREAK:
			chconbrk(brklab);
			branch(brklab);
			goto semi;

		case CONTIN:
			chconbrk(contlab);
			branch(contlab);
			goto semi;

		case DO: {
			register int o2, o3;
			o1 = contlab;
			o2 = brklab;
			contlab = isn++;
			brklab = isn++;
			label(o3 = isn++);
			statement();
			label(contlab);
			contlab = o1;
			if ((o=symbol())==KEYW && cval==WHILE) {
				cbranch(tree(1), o3, 1);
				label(brklab);
				brklab = o2;
				goto semi;
			}
			goto syntax;
		}

		case CASE:
			o1 = conexp();
			if ((o=symbol())!=COLON)
				goto syntax;
			if (swp==0) {
				error("Case not in switch");
				goto stmt;
			}
			if(swp>=swtab+SWSIZ) {
				error("Switch table overflow");
			} else {
				swp->swlab = isn;
				(swp++)->swval = o1;
				label(isn++);
			}
			goto stmt;

		case SWITCH: {
			register union tree *np;
			register char *st;

			o1 = brklab;
			brklab = isn++;
			st = starttree();
			np = pexpr(0);
			chkw(np, -1);
			rcexpr(block(RFORCE,0,(int *)NULL,(union str *)NULL,np,TNULL));
			endtree(st);
			pswitch();
			brklab = o1;
			return;
		}

		case DEFAULT:
			if (swp==0)
				error("Default not in switch");
			if (deflab)
				error("More than 1 'default'");
			if ((o=symbol())!=COLON)
				goto syntax;
			label(deflab = isn++);
			goto stmt;

		case FOR: {
			register int o2;
			o1 = contlab;
			o2 = brklab;
			contlab = isn++;
			brklab = isn++;
			if (o=forstmt())
				goto syntax;
			contlab = o1;
			brklab = o2;
			return;
		}

		case ELSE:
			error("Inappropriate 'else'");
			statement();
			return;
		}
		error("Unknown keyword");
		goto syntax;

	case NAME: {
		register struct nmlist *np;
		if (nextchar()==':') {
			peekc = 0;
			np = csym;
			if (np->hclass>0) {
				if (np->hblklev==0) {
					np = pushdecl(np);
					np->hoffset = 0;
				} else {
					defsym = np;
					redec();
					goto stmt;
				}
			}
			np->hclass = STATIC;
			np->htype = ARRAY;
			np->hflag |= FLABL;
			if (np->hoffset==0)
				np->hoffset = isn++;
			label(np->hoffset);
			goto stmt;
		}
	}
	}
	peeksym = o;
	rcexpr(tree(1));

semi:
	if ((o=symbol())==SEMI)
		return;
syntax:
	error("Statement syntax");
	errflush(o);
}

/*
 * Process a for statement.
 */
forstmt()
{
	register int o;
	register union tree *st;
	register l;
	char *ss;

	if ((o=symbol()) != LPARN)
		return(o);
	if ((o=symbol()) != SEMI) {		/* init part */
		peeksym = o;
		rcexpr(tree(1));
		if ((o=symbol()) != SEMI)
			return(o);
	}
	l = isn;
	isn += 3;
	branch(l+0);
	label(l+1);
	branch(l+2);
	label(contlab);
	st = NULL;
	if ((o=symbol()) != SEMI) {		/* test part */
		peeksym = o;
		ss = starttree();
		st = tree(0);
		if ((o=symbol()) != SEMI) {
			endtree(ss);
			return(o);
		}
	}
	if ((o=symbol()) != RPARN) {	/* incr part */
		peeksym = o;
		rcexpr(tree(1));
		if ((o=symbol()) != RPARN) {
			if (st)
				endtree(ss);
			return(o);
		}
	}
	label(l+0);
	if (st) {
		cbranch(st, l+1, 1);
		endtree(ss);
	} else
		branch(l+1);
	branch(brklab);
	label(l+2);
	statement();
	branch(contlab);
	label(brklab);
	return(0);
}

/*
 * A parenthesized expression,
 * as after "if".
 */
union tree *
pexpr(eflag)
{
	register o;
	register union tree *t;

	if ((o=symbol())!=LPARN)
		goto syntax;
	t = tree(eflag);
	if ((o=symbol())!=RPARN)
		goto syntax;
	if (t->t.type==VOID)
		error("Illegal use of void");
	return(t);
syntax:
	error("Statement syntax");
	errflush(o);
	return(0);
}

/*
 * The switch statement, which involves collecting the
 * constants and labels for the cases.
 */
pswitch()
{
	register struct swtab *cswp, *sswp;
	int dl, swlab;

	cswp = sswp = swp;
	if (swp==0)
		cswp = swp = swtab;
	branch(swlab=isn++);
	dl = deflab;
	deflab = 0;
	statement();
	branch(brklab);
	label(swlab);
	if (deflab==0)
		deflab = brklab;
	outcode("BNN", SWIT, deflab, line);
	for (; cswp < swp; cswp++)
		outcode("NN", cswp->swlab, cswp->swval);
	outcode("0");
	label(brklab);
	deflab = dl;
	swp = sswp;
}

/*
 * funchead is called at the start of each function
 * to process the arguments, which have been linked in a list.
 * This list is necessary because in
 * f(a, b) float b; int a; ...
 * the names are seen before the types.
 */
/*
 * Structure resembling a block for a register variable.
 */
struct	nmlist	hreg	= { REG, 0, 0, NULL, NULL, 0 };
struct	tnode	areg	= { NAME, 0, NULL, NULL, (union tree *)&hreg};
funchead()
{
	register pl;
	register struct nmlist *cs;
	register char *st;

	pl = STARG;
	protokrchk(paraml);
	while(paraml) {
		parame->sparent = NULL;
		cs = paraml;
		paraml = &paraml->sparent->P;
		if (cs->htype==FLOAT)
			cs->htype = DOUBLE;
		cs->hoffset = pl;
		if ((cs->htype&XTYPE) == ARRAY) {
			cs->htype -= (ARRAY-PTR);	/* set ptr */
			cs->hsubsp++;		/* pop dims */
		}
		pl += rlength((union tree *)cs);
		if (cs->hclass==AREG && (hreg.hoffset=goodreg(cs))>=0) {
			st = starttree();
			*cp++ = (union tree *)&areg;
			*cp++ = nblock(cs);
			areg.type = cs->htype;
			areg.strp = cs->hstrp;
			cs->hclass = AUTO;
			build(ASSIGN);
			rcexpr(*--cp);
			cs->hoffset = hreg.hoffset;
			cs->hclass = REG;
			endtree(st);
		} else
			cs->hclass = AUTO;
		prste(cs);
	}
	for (pl=0; pl<HSHSIZ; pl++) {
		for (cs = hshtab[pl]; cs!=NULL; cs = cs->nextnm) {
			if (cs->hclass == ARG || cs->hclass==AREG)
				error("Not an argument: %s", cs->name);
		}
	}
	outcode("BN", SETREG, regvar);
}

blockhead()
{
	register r;

	r = regvar;
	blklev++;
	declist(0);
	if (r != regvar)
		outcode("BN", SETREG, regvar);
}

/*
 * After the end of a block, delete local
 * symbols;
 * Also complain about undefined labels.
 */
blkend()
{
	register struct nmlist *cs, **lcs;
	register i;

	blklev--;
	for (i = 0; i < HSHSIZ; i++) {
		lcs = &hshtab[i];
		cs = *lcs;
		while (cs) {
			if (cs->hblklev > blklev
			 && (((cs->hflag&FLABL)==0 && cs->hclass!=EXTERN) || blklev<=0)) {
				if (cs->hclass==0)
					error("%s undefined", cs->name);
				if (cs->hclass==EXTERN)
					nameconflict(hshtab[i], cs);
				*lcs = cs->nextnm;
			} else
				lcs = &cs->nextnm;
			cs = cs->nextnm;
		}
	}
}

nameconflict(ocs, cs)
register struct nmlist *ocs, *cs;
{

	for (; ocs!=NULL; ocs = ocs->nextnm) 
		if (ocs!=cs && ocs->hclass==EXTERN && 
		    strncmp(cs->name, ocs->name, MAXCPS-1) == 0)
			error("names %s and %s conflict", cs->name, ocs->name);
}

/*
 * write out special definitions of local symbols for
 * benefit of the debugger.  None of these are used
 * by the assembler except to save them.
 */
prste(cs)
struct nmlist *cs;
{
	register nkind;

	switch (cs->hclass) {
	case REG:
		nkind = RNAME;
		break;

	case AUTO:
		nkind = ANAME;
		break;

	case STATIC:
		nkind = SNAME;
		break;

	default:
		return;

	}
	outcode("BSN", nkind, cs->name, cs->hoffset);
}

/*
 * In case of error, skip to the next
 * statement delimiter.
 */
errflush(ao)
{
	register o;

	o = ao;
	while(o>RBRACE) {	/* ; { } */
		if (o==STRING)
			putstr(0, 0);
		o = symbol();
	}
	peeksym  = o;
}
