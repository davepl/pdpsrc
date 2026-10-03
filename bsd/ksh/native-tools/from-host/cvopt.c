#include <stdio.h>

int	tabflg;
int	labno	= 1;
int opno;
FILE	*curbuf;
FILE	*obuf;
FILE	*oobuf;
char  oname[]="/tmp/cvoptaXXXXXX";
char ooname[]="/tmp/cvoptbXXXXXX";
char lbuf[BUFSIZ];
char *lbufp = lbuf;

main(argc, argv)
char **argv;
{
/*
	A1 -> A
	A2    B
	A     O
	B1    C
	B2    D
	BE    L
	BF    P
	C1    E
	C2    F
	F     G
	H     H
	R     I
	R1    J
	S     K
	I     M
	M     N

		*	+1
		S	+2
		C	+4
		1	+8

	z  -> 4
	c     10
	a     14
	e     20
	n     63
	*	+0100
*/

	int c, snlflg, nlflg, t, smode, m, ssmode, peekc, side;

	smode = nlflg = snlflg = ssmode = 0;
	if (argc>1)
		if (freopen(argv[1], "r", stdin) == NULL) {
			fprintf(stderr, "%s?\n", argv[1]);
			return(1);
		}
	if (argc>2) 
		if (freopen(argv[2], "w", stdout) == NULL) {
			fprintf(stderr, "%s?\n", argv[2]);
			return(1);
		}
	mktemp(oname);
	if ((obuf = fopen(oname, "w")) == NULL) {
		fprintf(stderr, "%s?\n", oname);
		exit(1);
	}
	mktemp(ooname);
	if ((oobuf = fopen(ooname, "w")) == NULL) {
		fprintf(stderr, "%s?\n", ooname);
		exit(1);
	}
	printf("#include \"c1.h\"");
	curbuf = obuf;
loop:
	c = getchar();
	if (c!='\n' && c!='\t')
		nlflg = 0;
	if (ssmode!=0 && c!='%') {
		ssmode = 0;
		curbuf = stdout;
		fprintf(curbuf, "\nstatic char L%d[]=\"", labno++);
	}
	switch(c) {

	case EOF:
		fprintf(obuf, "\t{0},\n};\n");
		fclose(obuf);
		if (freopen(oname, "r", stdin) == NULL) {
			fprintf(stderr, "%s?\n",oname);
			exit(1);
		}
		while ((c = getchar()) != EOF)
			putchar(c);
		unlink(oname);
		fclose(oobuf);
		if (freopen(ooname, "r", stdin) == NULL) {
			fprintf(stderr, "%s?\n",ooname);
			exit(1);
		}
		while ((c = getchar()) != EOF)
			putchar(c);
		unlink(ooname);
		return(0);

	case 'A':
		if ((c=getchar())=='1' || c=='2') {
			put(c+'A'-'1');
			goto loop;
		}
		put('O');
		ungetc(c, stdin);
		goto loop;

	case 'B':
		switch (getchar()) {

		case '1':
			put('C');
			goto loop;

		case '2':
			put('D');
			goto loop;

		case 'E':
			put('L');
			goto loop;

		case 'F':
			put('P');
			goto loop;
		}
		put('?');
		goto loop;

	case 'C':
		put(getchar()+'E'-'1');
		goto loop;

	case 'F':
		put('G');
		goto subtre;

	case 'R':
		if ((c=getchar()) == '1')
		put('J'); else {
			put('I');
			ungetc(c, stdin);
		}
		goto loop;

	case 'H':
		put('H');
		goto subtre;

	case 'I':
		put('M');
		goto loop;

	case 'S':
		put('K');
subtre:
		snlflg = 1;
		t = 'A';
l1:
		switch (c=getchar()) {

		case '*':
			t++;
			goto l1;

		case 'S':
			t += 2;
			goto l1;

		case 'C':
			t += 4;
			goto l1;

		case '1':
			t += 8;
			goto l1;

		case '2':
			t += 16;
			goto l1;
		}
		ungetc(c, stdin);
		put(t);
		goto loop;

	case '#':
		if(getchar()=='1')
			put('#'); else
			put('"');
		goto loop;

	case '%':
		if (smode)
			curbuf = obuf;
		if (ssmode==0) {
			if ((peekc=getchar())=='[') {
				printf("\n#define ");
				while((c=getchar())!=']' && c!=':')
					putchar(c);
				printf(" L%d\n",labno);
				if (c==':') getchar();
				getchar();
				curbuf = obuf;
				goto loop;
			}
			ungetc(peekc, stdin);
		}
		side=0;
loop1:
		switch (c=getchar()) {

		case ' ':
		case '\t':
			goto loop1;
		case 'a':
			m = 16;
			t = flag();
			goto pf;

		case ',':
			side=1;
			goto loop1;

		case 'i':
			m = 12;
			t = flag();
			goto pf;
		case 'z':
			m = 4;
			t = flag();
			goto pf;

		case 'r':
			m = 9;
			t = flag();
			goto pf;

		case '1':
			m = 5;
			t = flag();
			goto pf;

		case 'c':
			t = 0;
			m = 8;
			goto pf;

		case 'e':
			t = flag();
			m = 20;
			goto pf;

		case 'n':
			t = flag();
			m = 63;
pf:
			if ((c=getchar())=='*')
				m += 0100; else
				ungetc(c, stdin);
			if (side==0) {
				if (opno==0) fprintf(curbuf,"\nstruct optab optab[]={\n");
				fprintf(curbuf,"\t{");
			}
			fprintf(curbuf, "%d,%d,", m, t);
			goto loop1;
		case '[':
			printf("\n#define L%d ", labno++);
			while ((c=getchar())!=']')
				putchar(c);
			printf("\n");
			ssmode = 0;
			smode = 0;
			goto loop;

		case '{':
		for(;;) {
			while ((c=getchar())!='%') putc(c,oobuf);
			if ((c=getchar())=='}') goto loop;
			else {putc('%',oobuf); putc(c,oobuf);}
		}
			
		case '\n':
			fprintf(curbuf, "L%d},	/* %d */\n", labno,opno);
			++opno;
			ssmode = 1;
			nlflg = 1;
			smode = 1;
			goto loop;

		case '/':
			comment(c); goto loop1;

		}
		put(c);
		goto loop1;

	case '\t':
		if (nlflg) {
			nlflg = 0;
			goto loop;
		}
		if (smode) {
			tabflg++;
			goto loop;
		}
		put('\t');
		goto loop;

	case '\n':
		lbufp=lbuf;
		if (!smode)  {
			put('\n');
			goto loop;
		}
		if (nlflg) {
			nlflg = 0;
			fprintf(curbuf, "\";");
			curbuf = obuf;
			smode = 0;
			goto loop;
		}
		if (!snlflg)
			fprintf(curbuf, "\\n");
		snlflg = 0;
		nlflg = 1;
		goto loop;

	case '/':
		comment(c); goto loop;

	case 'X':
	case 'Y':
	case 'T':
		snlflg++;
		break;

	case ':':
		fseek(curbuf,(long)(lbuf-lbufp),2);
		*lbufp='\0';
		if (opno!=0) {fprintf(curbuf,"\t{0},\n"); ++opno;}
		printf("\n#define %s &optab[%d]\n",lbuf,opno);
		fprintf(curbuf,"/* %s */",lbuf);
		lbufp=lbuf;
		goto loop;

	}
	*lbufp++=c;
	put(c);
	goto loop;
}

flag() {
	register c, f;

	f = 0;
l1:
	switch(c=getchar()) {

	case 'w':
		f = 1;
		goto l1;

	case 'i':
		f = 2;
		goto l1;

	case 'b':
		if (f==9)		/* unsigned word/int seen yet? */
			f = 10;		/*  yes - it is unsigned byte */
		else
			f = 3;		/*  no - it is regular (signed) byte */
		goto l1;

	case 'f':
		f = 4;
		goto l1;

	case 'd':
		f = 5;
		goto l1;

	case 'u':
		if (f==3)		/* regular (signed) byte seen ? */
			f = 10;		/*  yes - unsigned byte now */
		else if (f == 8)	/* regular (signed) long seen? */
			f = 11;		/*  yes - it is unsigned long now */
		else
			f = 9;		/* otherwise we have unsigned word */
		goto l1;

	case 's':
		f = 6;
		goto l1;

	case 'l':
		if (f == 9)		/* seen unsigned yet? */
			f = 11;		/*  yes - it is unsigned long now */
		else
			f = 8;		/*  no - it is unsigned word now */
		goto l1;

	case 'p':
		f += 16;
		goto l1;
	}
	ungetc(c, stdin);
	return(f);
}

put(c)
{
	if (tabflg) {
		tabflg = 0;
		fprintf(curbuf, "\\%o", c+0200);
	} else {
		if (c=='"') putc('\\',curbuf);
		putc(c, curbuf);
	}
}

comment(c)
register char c;
{
	putc(c,curbuf);
	if ((c=getchar())=='*') for (;;) {
		do putc(c,curbuf); while ((c=getchar())!='*');
		putc(c,curbuf);
		if ((c=getchar())=='/') {putc(c,curbuf); break;}
	} else ungetc(c,stdin);
}
