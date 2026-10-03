CFLAGS = -O
LDC0FLAGS= -i
LDC1FLAGS= -n

all: c0 c1 cvopt
c0: c00.o c01.o c02.o c03.o c04.o c05.o ansi.o
	${CC} ${LDC0FLAGS} ${CFLAGS} -o c0 c00.o c01.o c02.o c03.o c04.o \
	c05.o ansi.o

c00.o c01.o c02.o c03.o c04.o c05.o ansi.o: c0.h

c1: c10.o c11.o c12.o c13.o table.o
	${CC} ${LDC1FLAGS} ${CFLAGS} -o c1 c10.o c11.o c12.o c13.o table.o

c10.o c11.o c12.o c13.o: c1.h

table.o: optable cvopt
	./cvopt < optable > junk.c
	${CC} -S junk.c
	sed 's/\.data/\.text/' < junk.s > junk.i
	as -o table.o junk.i
	rm -f junk.i junk.c junk.s

cvopt:	cvopt.c
	${CC} ${CFLAGS} -o cvopt cvopt.c

install: c0 c1
	-mv /lib/c0 /lib/oc0
	-mv /lib/c1 /lib/oc1
	install -s -m 755 -o root -g wheel c0 /lib/c0
	install -s -m 755 -o root -g wheel c1 /lib/c1

restore:
	mv /lib/oc0 /lib/c0
	mv /lib/oc1 /lib/c1

lint:
	lint -haxc -I. c0?.c > lint.c0
	lint -haxc -I. c1?.c > lint.c1

clean:
	rm -f *.o c0 c1 cvopt lint.c?
