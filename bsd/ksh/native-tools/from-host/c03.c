/*
 * C compiler, phase 1
 * Handles processing of declarations,
 * except for top-level processing of
 * externals.
 */

#if	!defined(lint) && defined(DOSCCS)
static	char	sccsid[] = "@(#)c03.c	2.2 (2.11BSD) 2025/8/29";
#endif
#include "c0.h"

/*
 * Process a sequence of declaration statements
 */
declist(sclass)
{
	register sc, offset;
	struct nmlist typer;

	offset = 0;
	sc = sclass;
	while (getkeywords(&sclass, &typer)) {
		offset = declare(sclass, &typer, offset);
		sclass = sc;
	}
	return(offset+align(INT, offset, 0));
}

/*
 * Read the keywords introducing a declaration statement
 * Store back the storage class, and fill in the type
 * entry, which looks like a hash table entry.
 */
getkeywords(scptr, tptr)
int *scptr;
struct nmlist *tptr;
{
	register skw, tkw, longf;
	int o, isadecl, ismos, unsignf;

	isadecl = 0;
	longf = 0;
	unsignf = 0;
	tptr->htype = INT;
	tptr->hstrp = NULL;
	tptr->hsubsp = NULL;
	tkw = -1;
	skw = *scptr;
	ismos = skw==MOS||skw==MOU? FMOS: 0;
	for (;;) {
		mosflg = isadecl? ismos: 0;
		o = symbol();
		if (o==NAME && csym->hclass==TYPEDEF && tkw < 0) {
			tkw = csym->htype;
			tptr->hsubsp = csym->hsubsp;
			tptr->hstrp = csym->hstrp;
			isadecl++;
			continue;
		}
		switch (o==KEYW? cval: -1) {
		case AUTO:
		case STATIC:
		case EXTERN:
		case REG:
		case TYPEDEF:
			if (skw && skw!=cval) {
				if (skw==ARG && cval==REG)
					cval = AREG;
				else
					error("Conflict in storage class");
			}
			skw = cval;
			break;

		case UNSIGN:
			unsignf++;
			break;

		case LONG:
			longf++;
			break;

		case ENUM:
			if (longf || unsignf)
				error("Perverse modifier on 'enum'");
			strdec(ismos, cval);
			cval = INT;
			goto types;

		case CONST:
		case VOLATIL:
		case SIGNED:
			continue;

		case UNION:
		case STRUCT:
			tptr->hstrp = strdec(ismos, cval);
			cval = STRUCT;
		case INT:
		case CHAR:
		case FLOAT:
		case DOUBLE:
		case VOID:
		types:
			if (tkw>=0 && (tkw!=INT || cval!=INT))
				error("Type clash");
			tkw = cval;
			if (unscflg && cval==CHAR)
				unsignf++;
			break;

		default:
			peeksym = o;
			if (isadecl==0)
				return(0);
			if (tkw<0)
				tkw = INT;
			if (skw==0)
				skw = blklev==0? DEFXTRN: AUTO;
			if (unsignf) {
				if (tkw==INT)
					tkw = UNSIGN;
				else if (tkw==CHAR)
					tkw = UNCHAR;
				else if (tkw==LONG)
					tkw = UNLONG;
				else
					error("Misplaced 'unsigned'");
			}
			if (longf) {
				if (tkw==FLOAT)
					tkw = DOUBLE;
				else if (tkw==INT)
					tkw = LONG;
				else if (tkw==UNSIGN)
					tkw = UNLONG;
				else
					error("Misplaced 'long'");
			}
			*scptr = skw;
			tptr->htype = tkw;
			return(1);
		}
		isadecl++;
	}
}

/*
 * Process a structure, union, or enum declaration; a subroutine
 * of getkeywords.
 */
union str *
strdec(mosf, kind)
{
	register elsize, o;
	register struct nmlist *ssym;
	int savebits;
	struct nmlist **savememlist;
	union str *savesparent;
	int savenmems;
	union str *strp;
	struct nmlist *ds;
	struct nmlist *mems[NMEMS];
	struct nmlist typer;
	int tagkind;

	if (kind!=ENUM) {
		tagkind = STRTAG;
		mosflg = FTAG;
		if (kind==UNION)
			mosflg = FUNION;
	} else {
		tagkind = ENUMTAG;
		mosflg = FENUM;
	}
	ssym = 0;
	if ((o=symbol())==NAME) {
		ssym = csym;
		mosflg = mosf;
		do {
			o = symbol();
		} while (o == KEYW && (cval == CONST || cval == VOLATIL));
		if (o==LBRACE && ssym->hblklev<blklev)
			ssym = pushdecl(ssym);
		if (ssym->hclass && ssym->hclass!=tagkind) {
			defsym = ssym;
			redec();
			ssym = pushdecl(ssym);
		}
		if (ssym->hclass==0) {
			ssym->hclass = tagkind;
			ssym->hstrp = (union str *)Dblock(sizeof(struct SS));
			ssym->hstrp->S.ssize = 0;
			ssym->hstrp->S.memlist = NULL;
		}
		strp = ssym->hstrp;
	} else {
		strp = (union str *)Dblock(sizeof(struct SS));
		strp->S.ssize = 0;
		strp->S.memlist = NULL;
	}
	mosflg = 0;
	if (o != LBRACE) {
		if (ssym==0)
			goto syntax;
		if (ssym->hclass!=tagkind)
			error("Bad structure/union/enum name");
		peeksym = o;
	} else {
		ds = defsym;
		mosflg = 0;
		savebits = bitoffs;
		savememlist = memlist;
		savesparent = sparent;
		savenmems = nmems;
		memlist = mems;
		sparent = strp;
		nmems = 2;
		bitoffs = 0;
		if (kind==ENUM) {
			typer.htype = INT;
			typer.hstrp = strp;
			declare(ENUM, &typer, 0);
		} else
			elsize = declist(kind==UNION?MOU:MOS);
		bitoffs = savebits;
		defsym = ds;
		if (strp->S.ssize)
			error("%s redeclared", ssym->name);
		strp->S.ssize = elsize;
		*memlist++ = NULL;
		strp->S.memlist = (struct nmlist **)Dblock((memlist-mems)*sizeof(*memlist));
		for (o=0; &mems[o] != memlist; o++)
			strp->S.memlist[o] = mems[o];
		memlist = savememlist;
		sparent = savesparent;
		nmems = savenmems;
		if ((o = symbol()) != RBRACE)
			goto syntax;
	}
	return(strp);
   syntax:
	decsyn(o);
	return(0);
}

/*
 * Process a comma-separated list of declarators
 */
declare(askw, tptr, offset)
struct nmlist *tptr;
{
	register unsigned o;
	register int skw, isunion;
	struct nmlist abs, *aptr;

	skw = askw;
	isunion = 0;
	if (skw==MOU) {
		skw = MOS;
		isunion++;
		mosflg = FMOS;
		if ((peeksym=symbol()) == SEMI) {
			o = length((union tree *)tptr);
			if (o>offset)
				offset = o;
		}
	}
	do {
		if (skw==ENUM && (peeksym=symbol())==RBRACE) {
			o = peeksym;
			peeksym = -1;
			break;
		}
		if (skw == MOS) {
			abs.hclass = 0;
			abs.hflag = 0;
			abs.htype = 0;
			abs.hsubsp = 0;
			abs.hstrp = 0;
			abs.nextnm = 0;
			abs.sparent = 0;
			abs.hblklev = blklev;
			abs.name = "<none>";
			aptr = &abs;
		} else
			aptr = NULL;
		o = decl1(skw, tptr, isunion?0:offset, aptr);
		if (isunion) {
			o += align(CHAR, o, 0);
			if (o>offset)
				offset = o;
		} else
			offset += o;
	} while ((o=symbol()) == COMMA);
	if (o==RBRACE) {
		peeksym = o;
		o = SEMI;
	}
	if (o!=SEMI && (o!=RPARN || skw!=ARG1))
		decsyn(o);
	return(offset);
}

/*
 * Process a single declarator
 */
decl1(askw, atptr, offset, absname)
struct nmlist *atptr, *absname;
{
	int t1, a, elsize;
	register int skw;
	int type;
	register struct nmlist *dsym;
	register struct nmlist *tptr;
	struct tdim dim;
	int *dp;
	int isinit;

	skw = askw;
	tptr = atptr;
	mosflg = skw==MOS? FMOS: 0;
	dim.rank = 0;
	if (((peeksym=symbol())==SEMI || peeksym==RPARN) && absname==NULL)
		return(0);
	/*
	 * Filler field
	 */
	if (peeksym==COLON && skw==MOS) {
		peeksym = -1;
		t1 = conexp();
		if (t1<0) {
			error("Negative field width");
			t1 = 0;
		}
		elsize = align(tptr->htype, offset, t1);
		bitoffs += t1;
		return(elsize);
	}
	t1 = getype(&dim, absname);
	if (t1 == -1)
		return(0);
	if (defsym)
		absname = NULL;
	if (tptr->hsubsp) {
		type = tptr->htype;
		for (a=0; type&XTYPE;) {
			if ((type&XTYPE)==ARRAY || ((type&XTYPE)==FUNC))
				dim.dimens[dim.rank++] = tptr->hsubsp[a++];
			type >>= TYLEN;
		}
	}
	type = tptr->htype & ~TYPE;
	while (t1&XTYPE) {
		if (type&BIGTYPE) {
			typov();
			type = t1 = 0;
		}
		type = type<<TYLEN | (t1 & XTYPE);
		t1 >>= TYLEN;
	}
	type |= tptr->htype&TYPE;
	if ((type&XTYPE) == FUNC) {
		if (skw==AUTO)
			skw = EXTERN;
		if ((skw!=EXTERN && skw!=TYPEDEF) && absname==NULL)
			error("Bad func. storage class");
	}
	if (defsym)
		dsym = defsym;
	else if (absname)
		dsym = absname;
	else {
		error("Name required in declaration");
		return(0);
	}
	if (defsym)
	if (dsym->hblklev<blklev || dsym->hclass==MOS && skw==MOS) {
		if (skw==MOS && dsym->sparent==sparent)
			redec();
		defsym = dsym;
		if (skw==EXTERN) {
			for (; dsym!=NULL; dsym = dsym->nextnm) {
				if (dsym->hclass==EXTERN
				 && strcmp(dsym->name, defsym->name)==0) {
					defsym = dsym;
					break;
				}
			}
			dsym = defsym;
		} else
			defsym = dsym = pushdecl(dsym);
	}
	if ((dsym->htype&XTYPE)==FUNC) {
		declproto(dsym, &dim);	/* for functions check prototype */
	} else if (dim.rank == 0)
		dsym->hsubsp = NULL;
	else {
		/*
		 * If an array is declared twice, make sure the declarations
		 * agree in dimension.  This happens typically when a .h
		 * and .c file both declare a variable.
		 */
		if (dsym->hsubsp) {
			for (a=0, t1 = dsym->htype; t1&XTYPE; t1 >>= TYLEN) {
				if ((t1 & XTYPE) == PTR)
					continue;
				if ((t1 & XTYPE) == FUNC) {
					/* protocheck() */ ;
				} else { /* (t1 & XTYPE) == ARRAY */
					if (!dsym->hsubsp[a])
						dsym->hsubsp[a] = dim.dimens[a];
					else if (dim.dimens[a]
					    && dim.dimens[a] != dsym->hsubsp[a])
						redec();
				}
				a++;
			}
			if (a != dim.rank)
				error("compiler array botch");
		} else {
			dp = (int *)Dblock(dim.rank*sizeof(dim.rank));
			for (a=0; a<dim.rank; a++)
				dp[a] = dim.dimens[a];
			dsym->hsubsp = dp;
		}
	}
	if (!(dsym->hclass==0
	   || ((skw==ARG||skw==AREG) && dsym->hclass==ARG1)
	   || (skw==EXTERN && dsym->hclass==EXTERN && dsym->htype==type)
	   || (skw==TYPEDEF && dsym->hclass==TYPEDEF && dsym->htype==type))) {
		redec();
		goto syntax;
	}
	if (dsym->hclass && (dsym->htype&TYPE)==STRUCT && (type&TYPE)==STRUCT)
		if (dsym->hstrp != tptr->hstrp) {
			error("structure redeclaration");
		}
	dsym->htype = type;
	if (tptr->hstrp)
		dsym->hstrp = tptr->hstrp;
	if (skw==TYPEDEF) {
		dsym->hclass = TYPEDEF;
		return(0);
	}
	if (skw==ARG1) {
		if (paraml==NULL)
			paraml = dsym;
		else
			parame->sparent = (union str *)dsym;
		parame = dsym;
		dsym->hclass = skw;
		return(0);
	}
	elsize = 0;
	if (skw==MOS) {
		elsize = length((union tree *)dsym);
		if ((peeksym = symbol())==COLON) {
			elsize = 0;
			peeksym = -1;
			t1 = conexp();
			a = align(type, offset, t1);
			if (dsym->hflag&FFIELD) {
				if (dsym->hstrp->F.bitoffs!=bitoffs
				 || dsym->hstrp->F.flen!=t1)
					redec();
			} else {
				dsym->hstrp = (union str *)Dblock(sizeof(struct FS));
			}
			dsym->hflag |= FFIELD;
			dsym->hstrp->F.bitoffs = bitoffs;
			dsym->hstrp->F.flen = t1;
			bitoffs += t1;
		} else
			a = align(type, offset, 0);
		elsize += a;
		offset += a;
		if (++nmems >= NMEMS) {
			error("Too many structure members");
			nmems -= NMEMS/2;
			memlist -= NMEMS/2;
		}
		if (a)
			*memlist++ = &structhole;
		dsym->hoffset = offset;
		*memlist++ = dsym;
		dsym->sparent = sparent;
	}
	if (skw==REG)
		if ((dsym->hoffset = goodreg(dsym)) < 0)
			skw = AUTO;
	dsym->hclass = skw;
	isinit = 0;
	if ((a=symbol()) == ASSIGN)
		isinit++;
	else
		peeksym = a;
	if (skw==AUTO) {
	/*	if (STAUTO < 0) {	*/
			autolen -= rlength((union tree *)dsym);
			dsym->hoffset = autolen;
			if (autolen < maxauto)
				maxauto = autolen;
	/*	} else { 			*/
	/*		dsym->hoffset = autolen;	*/
	/*		autolen += rlength(dsym);	*/
	/*		if (autolen > maxauto)		*/
	/*			maxauto = autolen;	*/
	/*	}			*/
		if (isinit)
			cinit(dsym, 0, AUTO);
		isinit = 0;
	} else if (skw==STATIC) {
		dsym->hoffset = isn;
		if (isinit) {
			outcode("BBN", DATA, LABEL, isn++);
			if (cinit(dsym, 1, STATIC) & ALIGN)
				outcode("B", EVEN);
		} else
			outcode("BBNBN", BSS, LABEL, isn++, SSPACE,
			  rlength((union tree *)dsym));
		outcode("B", PROG);
		isinit = 0;
	} else if (skw==REG && isinit) {
		cinit(dsym, 0, REG);
		isinit = 0;
	} else if (skw==ENUM) {
		if (type!=INT)
			error("Illegal enumeration %s", dsym->name);
		dsym->hclass = ENUMCON;
		dsym->hoffset = offset;
		if (isinit)
			cinit(dsym, 0, ENUMCON);
		elsize = dsym->hoffset-offset+1;
		isinit = 0;
	}
	if (absname==0)
		prste(dsym);
	if (isinit)
		peeksym = ASSIGN;
syntax:
	return(elsize);
}

/*
 * Push down an outer-block declaration
 * after redeclaration in an inner block.
 */
struct nmlist *
pushdecl(sp)
register struct nmlist *sp;
{
	register struct nmlist *nsp, **hsp;

	nsp = (struct nmlist *)Dblock(sizeof(struct nmlist));
	*nsp = *sp;
	nsp->hclass = 0;
	nsp->hflag &= FKIND;
	nsp->htype = 0;
	nsp->hoffset = 0;
	nsp->hblklev = blklev;
	nsp->hstrp = NULL;
	nsp->hsubsp = NULL;
	nsp->sparent = NULL;
	hsp = &hshtab[hash(sp->name)];
	nsp->nextnm = *hsp;
	*hsp = nsp;
	return(nsp);
}

/*
 * Read a declarator and get the implied type
 */
getype(dimp, absname)
register struct tdim *dimp;
struct nmlist *absname;
{
	static struct nmlist argtype;
	int type;
	register int o;
	register struct nmlist *ds;

	defsym = 0;
	type = 0;
more:	switch(o=symbol()) {
	case KEYW:
		if (cval == CONST || cval == VOLATIL) {
			mosflg = mossym;
			mossym = 0;
			goto more;
		}
		break;

	case TIMES:
		type = getype(dimp, absname);
		if (type==-1)
			return(type);
		if (type&BIGTYPE) {
			typov();
			type = 0;
		}
		return(type<<TYLEN | PTR);

	case LPARN:
		if (absname==NULL || nextchar()!=')') {
			type = getype(dimp, absname);
			if (type==-1)
				return(type);
			if ((o=symbol()) != RPARN)
				goto syntax;
			goto getf;
		}

	default:
		peeksym = o;
		if (absname)
			goto getf;
		break;

	case NAME:
		defsym = ds = csym;
	getf:
		switch(o=symbol()) {

		case LPARN:
			blklev++;
			ds = defsym;
			dimp->dimens[dimp->rank++] = ansidecl();
			defsym = ds;
			blklev--;

			if ((o=symbol()) != RPARN)
				goto syntax;
			if (type&BIGTYPE) {
				typov();
				type = 0;
			}
			type = type<<TYLEN | FUNC;
			goto getf;

		case LBRACK:
			if (dimp->rank>=5) {
				error("Rank too large");
				dimp->rank = 4;
			}
			if ((o=symbol()) != RBRACK) {
				peeksym = o;
				ds = defsym;
				cval = conexp();
				defsym = ds;
				if ((o=symbol())!=RBRACK)
					goto syntax;
			} else {
				if (dimp->rank!=0)
					error("Null dimension");
				cval = 0;
			}
			dimp->dimens[dimp->rank++] = cval;
			if (type&BIGTYPE) {
				typov();
				type = 0;
			}
			type = type<<TYLEN | ARRAY;
			goto getf;
		}
		peeksym = o;
		return(type);
	}
syntax:
	decsyn(o);
	return(-1);
}

/*
 * More bits required for type than allowed.
 */
typov()
{
	error("Type is too complicated");
}

/*
 * Enforce alignment restrictions in structures,
 * including bit-field considerations.
 */
align(type, offset, aflen)
{
	register a, t, flen;
	char *ftl;

	flen = aflen;
	a = offset;
	t = type;
	ftl = "Field too long";
	if (flen==0) {
		a += (NBPC+bitoffs-1) / NBPC;
		bitoffs = 0;
	}
	while ((t&XTYPE)==ARRAY)
		t = decref(t);
	if (t!=CHAR && t!=UNCHAR) {
		a = (a+ALIGN) & ~ALIGN;
		if (a>offset)
			bitoffs = 0;
	}
	if (flen) {
		if (type==INT || type==UNSIGN) {
			if (flen > NBPW)
				error(ftl);
			if (flen+bitoffs > NBPW) {
				bitoffs = 0;
				a += NCPW;
			}
		} else if (type==CHAR || type==UNCHAR) {
			if (flen > NBPC)
				error(ftl);
			if (flen+bitoffs > NBPC) {
				bitoffs = 0;
				a += 1;
			}
		} else
			error("Bad type for field");
	}
	return(a-offset);
}

/*
 * Complain about syntax error in declaration
 */
decsyn(o)
{
	error("Declaration syntax");
	errflush(o);
}

/*
 * Complain about a redeclaration
 */
redec()
{
	error("%s redeclared", defsym->name);
}

/*
 * Determine if a variable is suitable for storage in
 * a register; if so return the register number
 */
goodreg(hp)
struct nmlist *hp;
{
	int type;

	type = hp->htype;
	if ((type!=INT && type!=UNSIGN && (type&XTYPE)==0)
	 || (type&XTYPE)>PTR || regvar<3)
		return(-1);
	return(--regvar);
}
