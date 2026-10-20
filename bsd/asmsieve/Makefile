# Install already compiled programs without rebuilding them.
BINDIR=/usr/bin
DESTDIR=
INSTALL=install

AS=as
CC=cc
LDFLAGS=

all: sieve

sieve: sieve.o
	${CC} ${LDFLAGS} -o sieve sieve.o

sieve.o: sieve.asm
	${AS} -o sieve.o sieve.asm

clean:
	rm -f sieve sieve.o

install: FORCE
	@/bin/sh -c 'test -d "${DESTDIR}${BINDIR}" || mkdir -p "${DESTDIR}${BINDIR}" || exit 1; \
	if test -f sieve; then \
		${INSTALL} -c -m 755 sieve "${DESTDIR}${BINDIR}/asmsieve" || exit 1; \
	else \
		echo "sieve not built; skipped"; \
	fi'

FORCE:
