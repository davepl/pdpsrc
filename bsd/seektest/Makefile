CC=cc
CFLAGS=-O
PDPFLAGS=-t1p -B../tools/
BINDIR=/usr/local

all: seektest

seektest: seektest.c
	${CC} ${PDPFLAGS} ${CFLAGS} -o seektest seektest.c

install: seektest
	install -c -m 755 seektest ${BINDIR}/seektest

clean:
	rm -f seektest *.o
