MAKE=make
# Makefile for Unix/Xenix rz and sz programs
# Some targets may not be up to date
CC=cc
OFLAG= -O
# Retain K&R C on macOS without passing Clang options to the PDP compiler.
HOSTFLAGS=case "`uname -s`" in Darwin) hostflags="-std=gnu89 -Wno-deprecated-non-prototype" ;; *) hostflags= ;; esac

# Native default; the historical "all" target packaged a source release.
RZLINKS=rb rx rc
SZLINKS=sb sx zcommand zcommandi
TEXTLINKS=tounix todos unmac tomac tocpm unparity
all: rz sz undos crc minirb ${RZLINKS} ${SZLINKS} ${TEXTLINKS}

rz: rz.c rbsb.c crctab.c zm.c zmr.c zmodem.h
	@${HOSTFLAGS}; \
	case "`uname -m`" in \
	pdp11|pdp-11) flags="-i -DSMALL -DMD=2 -Dstrchr=index -DV7" ;; \
	*) flags="-DPOSIX -DMD=2" ;; \
	esac; \
	${CC} $$hostflags ${CFLAGS} ${RFLAGS} ${OFLAG} $$flags rz.c -o rz

sz: sz.c rbsb.c crctab.c zm.c zmr.c zmodem.h
	@${HOSTFLAGS}; \
	case "`uname -m`" in \
	pdp11|pdp-11) flags="-i -DSMALL -DV7 -DNFGVMIN" ;; \
	*) flags="-DPOSIX" ;; \
	esac; \
	${CC} $$hostflags ${CFLAGS} ${RFLAGS} ${OFLAG} $$flags sz.c -o sz

${RZLINKS}: rz
	rm -f $@
	ln rz $@

${SZLINKS}: sz
	rm -f $@
	ln sz $@

${TEXTLINKS}: undos
	rm -f $@
	ln undos $@

crc: crc.c
	@${HOSTFLAGS}; \
	${CC} $$hostflags ${CFLAGS} ${OFLAG} -o crc crc.c

minirb: minirb.c ../pdp11_unistd.h
	@${HOSTFLAGS}; \
	${CC} $$hostflags ${CFLAGS} ${OFLAG} -o minirb minirb.c

nothing:
	@echo
	@echo "Please study the #ifdef's in crctab.c, rbsb.c, rz.c and sz.c,"
	@echo "make any necessary hacks for oddball or merged SYSV/BSD systems,"
	@echo "then type 'make SYSTEM' where SYSTEM is one of:"
	@echo
	@echo "	posix	POSIX compliant systems"
	@echo "	aix	AIX systems"
	@echo "	next	NeXtstep v3.x (POSIX)"
	@echo "	odt	SCO Open Desktop"
	@echo "	everest	SCO Open Desktop (elf, strict)"
	@echo "	sysvr4	SYSTEM 5.4 Unix"
	@echo "	sysvr3	SYSTEM 5.3 Unix with mkdir(2), COHERENT 4.2"
	@echo "	sysv	SYSTEM 3/5 Unix"
	@echo "	sysiii  SYS III/V  Older Unix or Xenix compilers"
	@echo "	xenix	Xenix"
	@echo "	x386	386 Xenix"
	@echo "	bsd	Berkeley 4.x BSD, Ultrix, V7"
	@echo "	tandy	Tandy 6000 Xenix"
	@echo "	dnix	DIAB Dnix 5.2"
	@echo "	dnix5r3	DIAB Dnix 5.3"
	@echo "	amiga	3000UX running SVR4"
	@echo "	POSIX	POSIX compliant systems (SCO Open Desktop, strict)"
	@echo
	@echo "	undos	Make the undos, todos, etc. program."
	@echo "	doc	Format the man pages with nroff"
	@echo

dist:doc usenet unixforum sshar shar zoo

usenet:doc
	shar -c -a -n rzsz -o /tmp/rzsz -l64 \
	  COPYING README Makefile undos.c zmodem.h zm.c rz.c rbsb.c \
	 crc.c crctab.c minirb.c mailer.rz zmr.c *.doc gz sz.c *.t 

sshar:doc
	shar -c -a -n rzsz -o /tmp/rzsz -l64 \
	  COPYING README Makefile undos.c zmodem.h zm.c rz.c rbsb.c \
	 crc.c crctab.c mailer.rz zmr.c *.doc gz sz.c

shar:doc
	shar -c COPYING README Makefile zmodem.h zm.c \
	 undos.c zmr.c sz.c rz.c crctab.c \
	 mailer.rz crc.c rbsb.c minirb.c *.doc gz *.t >/tmp/rzsz.sh
	 cp /tmp/rzsz.sh /u/t/yam

unixforum: shar
	rm -f /tmp/rzsz.sh.gz
	gzip -9 /tmp/rzsz.sh
	cp /tmp/rzsz.sh.gz /u/t/yam

doc:rz.doc sz.doc crc.doc minirb.doc undos.doc

clean:
	rm -f *.o *.out sz sb sx zcommand zcommandi rz rb rx rc
	rm -f undos tounix todos unmac tomac tocpm unparity
	rm -f crc minirb

minirb.doc:minirb.1
	nroff -man minirb.1 | col  >minirb.doc

rz.doc:rz.1 servers.mi
	nroff -man rz.1 | col  >rz.doc

sz.doc:sz.1 servers.mi
	nroff -man sz.1 | col  >sz.doc

crc.doc:crc.1
	nroff -man crc.1 | col  >crc.doc

undos.doc:undos.1
	nroff -man undos.1 | col  >undos.doc

zoo: doc
	-rm -f /tmp/rzsz.zoo
	zoo ah /tmp/rzsz COPYING README Makefile zmodem.h zm.c sz.c rz.c \
	 undos.c mailer.rz crctab.c rbsb.c *.doc \
	 zmr.c crc.c gz *.t minirb.c
	touch /tmp/rzsz.zoo
	chmod og-w /tmp/rzsz.zoo
	mv /tmp/rzsz.zoo /u/t/yam
	-rm -f rzsz.zip
	zip rzsz readme mailer.rz makefile zmodem.h zm.c sz.c rz.c
	zip rzsz undos.c crctab.c rbsb.c *.doc file_id.diz
	zip rzsz zmr.c crc.c gz *.t minirb.c
	mv rzsz.zip /u/t/yam

tag: doc  xenix
	-rm -f /tmp/rzsz
	tar cvf /tmp/rzsz COPYING README Makefile zmodem.h zm.c sz.c rz.c \
	 mailer.rz crctab.c rbsb.c *.doc \
	 undos.c zmr.c crc.c gz *.t minirb.c rz sz crc undos
	gzip -9 /tmp/rzsz
	mv /tmp/rzsz.gz /u/t/yam/rzsz.tag

tar:doc
	tar cvf /tmp/rzsz.tar COPYING README Makefile zmodem.h zm.c sz.c rz.c \
	 undos.c mailer.rz crctab.c rbsb.c \
	 zmr.c crc.c *.1 gz *.t minirb.c

tags:
	ctags sz.c rz.c zm.c zmr.c rbsb.c

.PRECIOUS:rz sz

xenix:
	/usr/ods30/bin/cc \
	-I/usr/ods30/usr/include -I/usr/ods30/usr/include/sys \
	-M2 $(CFLAGS) $(RFLAGS) $(OFLAG) -s -DSMALL -DUSG -DNFGVMIN -DREADCHECK sz.c -lx -o sz
	size sz; file sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi
	/usr/ods30/bin/cc \
	 -I/usr/ods30/usr/include -I/usr/ods30/usr/include/sys \
	-M2 $(CFLAGS) $(RFLAGS) $(OFLAG) -s -DUSG -DMD rz.c -o rz
	size rz; file rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	/usr/ods30/bin/cc \
	 -I/usr/ods30/usr/include -I/usr/ods30/usr/include/sys \
	-M2 $(CFLAGS) $(OFLAG) -s undos.c -o undos
	size undos; file undos
	-rm -f tounix todos unmac tomac tocpm unparity
	ln undos tounix
	ln undos todos
	ln undos unmac
	ln undos tomac
	ln undos tocpm
	ln undos unparity

x386:
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DUSG -DMD rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DUSG -DNFGVMIN -DREADCHECK sz.c -lx -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

sysv:
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DUSG -DMD -DOLD rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DUSG -DSV -DNFGVMIN -DOLD sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

sysiii:
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DUSG -DOLD rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DSV -DUSG -DNFGVMIN -DOLD sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

sysvr3:
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DUSG -DMD=2 rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DSV -DUSG -DNFGVMIN sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

sysvr4:
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DUSG -DMD=2 rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DSV -DUSG sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

odt:
	cc -O -n $(RFLAGS) -DUSG -DMD=2 rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	cc -O -n $(RFLAGS) -DUSG -DREADCHECK sz.c -lx -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

everest:
	cc -b elf -w 3 -O3 $(RFLAGS) -DUSG -DMD=2 rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	cc -b elf -w 3 $(RFLAGS) -O3 -DUSG -DREADCHECK sz.c -lx -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

posix:
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DPOSIX -DMD=2 rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DPOSIX sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

POSIX:
	@echo "Well, stricter, as in *safer sex* ..."
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DPOSIX -DMD=2 -DCOMPL rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DPOSIX -DCOMPL sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi


bsd:
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DMD=2 -Dstrchr=index -DV7 rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DV7 -DNFGVMIN sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

tandy:
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAGS) -n -DUSG -DMD -DT6K sz.c -lx -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAGS) -n -DUSG -DMD -DT6K rz.c -lx -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc

dnix:
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DUSG -DMD rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DSV -DUSG -DNFGVMIN -DREADCHECK sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

dnix5r3:
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DUSG -DMD=2 rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(RFLAGS) $(OFLAG) -DUSG -DSV -DNFGVMIN -DREADCHECK sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi


amiga:
	$(CC) $(CFLAGS) $(OFLAG) -DUSG -DNFGVMIN -g rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) $(CFLAGS) $(OFLAG) -DUSG -DSV -DNFGVMIN -g sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

aix:
	@echo "As of July 26 1996, ..."
	@echo "IBM sez if you have the very latest PTFs, 'make posix' will work."
	@echo ""

next:
	LIBS=-lposix
	$(CC) -g -posix $(OFLAG) -DPOSIX -DMD=2 rz.c -o rz
	size rz
	-rm -f rb rx rc
	ln rz rb
	ln rz rx
	ln rz rc
	$(CC) -g -posix $(OFLAG) -DPOSIX sz.c -o sz
	size sz
	-rm -f sb sx zcommand zcommandi
	ln sz sb
	ln sz sx
	ln sz zcommand
	ln sz zcommandi

undos:	undos.c ../pdp11_unistd.h
	@${HOSTFLAGS}; \
	${CC} $$hostflags ${CFLAGS} ${OFLAG} undos.c -o undos
	-rm -f tounix todos unmac tomac tocpm unparity
	ln undos tounix
	ln undos todos
	ln undos unmac
	ln undos tomac
	ln undos tocpm
	ln undos unparity


lint:
	lint -DUSG -DSV -DOLD sz.c >/tmp/sz.fluff
	lint -DUSG -DSV -DOLD rz.c >/tmp/rz.fluff
