CC=cc
CFLAGS=-O
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

all: segsieve

segsieve: segsieve.c sieve.c
	$(CC) $(CFLAGS) -o segsieve sieve.c segsieve.c

clean:
	rm -f segsieve *.o

install: FORCE
	@/bin/sh -c 'if test -f segsieve; then \
		test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
		${INSTALL} -c -m 755 segsieve "${DESTDIR}${BINDIR}/segsieve" || exit 1; \
	else echo "segsieve not built; skipped"; fi'

FORCE:
